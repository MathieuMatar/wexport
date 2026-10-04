#include <fstream>
#include <sstream>

#include <sqlite3.h>

#include "Core/Archive/ChatsJsWriter.h"
#include "Core/Archive/ExportFolder.h"
#include "Core/Archive/MembersBuilder.h"
#include "Core/Archive/Vcf.h"
#include "Core/Copy/Copier.h"
#include "Test.h"

using namespace ck;
namespace fs = std::filesystem;

namespace {

void WriteFile(const fs::path& p, const std::string& data) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary);
    f << data;
}

std::string ReadFile(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void Exec(const fs::path& db, const std::string& sql) {
    sqlite3* h = nullptr;
    sqlite3_open(db.string().c_str(), &h);
    char* err = nullptr;
    if (sqlite3_exec(h, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
        std::string e = err ? err : "?";
        sqlite3_free(err);
        sqlite3_close(h);
        throw t::Failure{"sql: " + e};
    }
    sqlite3_close(h);
}

const char* kSampleJson = R"({"96171111111@s.whatsapp.net": {"name": "Alice", "messages": {
 "1": {"from_me": false, "media": false, "meta": false, "data": "hi there", "reactions": {"You": "x"}},
 "2": {"from_me": true, "media": true, "meta": false, "data": "WhatsApp/Media/WhatsApp Images/IMG-1.jpg", "mime": "image/jpeg", "sticker": false},
 "3": {"from_me": false, "media": true, "meta": true, "data": "The media is missing", "mime": "media"},
 "4": {"from_me": false, "media": true, "meta": false, "data": "WhatsApp/Media/WhatsApp Voice Notes/202601/PTT-1.opus", "mime": "audio/ogg; codecs=opus"},
 "5": {"from_me": false, "media": true, "meta": false, "data": "WhatsApp/Media/WhatsApp Video/VID-1.mp4", "mime": "video/mp4"},
 "6": {"from_me": false, "media": true, "meta": false, "data": "WhatsApp/Media/WhatsApp Documents/a.pdf", "mime": "application/pdf"},
 "7": {"from_me": false, "media": true, "meta": false, "sticker": true, "data": "WhatsApp/Media/WhatsApp Stickers/s.webp", "mime": "image/webp"}}},
 "120363023708369742@g.us": {"name": "Group", "messages": {"9": {"from_me": false, "media": false, "data": "x"}}},
 "96179999999@s.whatsapp.net": {"name": "Empty", "messages": {}},
 "000000000000000": {"name": "WhatsApp Calls", "messages": {"1": {"meta": true, "data": "A voice call"}, "2": {"meta": true}}}})";

}  // namespace

TEST(ChatsJsIsByteExact) {
    auto dir = t::TempDir("chatsjs");
    WriteFile(dir / "chats.json", kSampleJson);
    auto r = WriteChatsJs(dir / "chats.json", dir / "chats.js", CancelToken());
    CHECK(r.ok);
    CHECK_EQ(ReadFile(dir / "chats.js"), std::string("window.CHATS_JSON = ") + kSampleJson + ";\n");
    CHECK(!fs::exists(dir / "chats.js.partial"));
    CHECK_EQ(r.stats.chats, 2u);
    CHECK_EQ(r.stats.groups, 1u);
    CHECK_EQ(r.stats.messages, 8u);
    CHECK_EQ(r.stats.photos, 1u);
    CHECK_EQ(r.stats.audio, 1u);
    CHECK_EQ(r.stats.videos, 1u);
    CHECK_EQ(r.stats.documents, 1u);
    CHECK_EQ(r.stats.stickers, 1u);
    CHECK_EQ(r.stats.missingMedia, 1u);
    CHECK_EQ(r.stats.calls, 2u);
    CHECK_EQ(r.stats.mediaPaths, r.stats.mediaPathsUnderMediaDir);
}

TEST(ChatsJsEscapesRawLineSeparators) {
    auto dir = t::TempDir("chatsjs-sep");
    std::string json = "{\"a\": {\"name\": \"x\xE2\x80\xA8y\xE2\x80\xA9z\xE2\x82\xAC\", \"messages\": {}}}";
    WriteFile(dir / "chats.json", json);
    auto r = WriteChatsJs(dir / "chats.json", dir / "chats.js", CancelToken());
    CHECK(r.ok);
    CHECK_EQ(r.escapedSeparators, 2u);
    CHECK_EQ(ReadFile(dir / "chats.js"),
             std::string("window.CHATS_JSON = {\"a\": {\"name\": \"x\\u2028y\\u2029z\xE2\x82\xAC\", \"messages\": {}}};\n"));
}

TEST(EscapeHandlesChunkBoundaries) {
    std::string input = "a\xE2\x80\xA8" "b\xE2\x80\xA9" "c\xE2\x82\xAC" "d\xE2";
    for (size_t split = 0; split <= input.size(); ++split) {
        std::string carry, out;
        std::uint64_t n = 0;
        out += EscapeLineSeparators(std::string_view(input).substr(0, split), carry, false, &n);
        out += EscapeLineSeparators(std::string_view(input).substr(split), carry, false, &n);
        out += EscapeLineSeparators({}, carry, true, &n);
        CHECK_EQ(out, std::string("a\\u2028b\\u2029c\xE2\x82\xAC" "d\xE2"));
        CHECK_EQ(n, 2u);
    }
}

TEST(ChatsJsLargeFile) {
    auto dir = t::TempDir("chatsjs-large");
    {
        std::ofstream f(dir / "chats.json", std::ios::binary);
        f << "{\"9611@s.whatsapp.net\": {\"name\": \"Big\", \"messages\": {";
        for (int i = 0; i < 300000; ++i) {
            if (i) f << ",";
            f << "\"" << i << "\": {\"from_me\": false, \"media\": false, \"data\": \"message number " << i
              << " \\u00e9\\u2028\"}";
        }
        f << "}}}";
    }
    auto r = WriteChatsJs(dir / "chats.json", dir / "chats.js", CancelToken());
    CHECK(r.ok);
    CHECK_EQ(r.stats.messages, 300000u);
    CHECK_EQ(r.jsBytes, r.jsonBytes + std::string("window.CHATS_JSON = ;\n").size());
}

TEST(ChatsJsRejectsInvalidJson) {
    auto dir = t::TempDir("chatsjs-bad");
    WriteFile(dir / "chats.json", "{\"a\": {\"messages\": {\"1\": ");
    auto r = WriteChatsJs(dir / "chats.json", dir / "chats.js", CancelToken());
    CHECK(!r.ok);
    CHECK(!fs::exists(dir / "chats.js"));
    auto missing = WriteChatsJs(dir / "nope.json", dir / "chats.js", CancelToken());
    CHECK(!missing.ok);
}

TEST(VcfParsing) {
    std::string vcf =
        "\xEF\xBB\xBF" "BEGIN:VCARD\r\nVERSION:2.1\r\n"
        "N;CHARSET=UTF-8;ENCODING=QUOTED-PRINTABLE:;P=C3=A8re Hanna;;;\r\n"
        "FN;CHARSET=UTF-8;ENCODING=QUOTED-PRINTABLE:P=C3=A8re =\r\nHanna\r\n"
        "TEL;CELL:03 366 160\r\nTEL;HOME:+961 1 234567\r\nEND:VCARD\r\n"
        "BEGIN:VCARD\r\nVERSION:3.0\r\nN:Doe;John;;;\r\nTEL;TYPE=CELL:0033 6 12 34 56 78\r\nEND:VCARD\r\n"
        "BEGIN:VCARD\r\nVERSION:3.0\r\nFN:Folded\r\n Name\r\nitem1.TEL:71 111 111\r\nEND:VCARD\r\n"
        "BEGIN:VCARD\r\nVERSION:3.0\r\nFN:No phone\r\nEND:VCARD\r\n";
    auto c = ParseVcf(vcf);
    CHECK_EQ(c.size(), 3u);
    CHECK_EQ(c[0].name, std::string("P\xC3\xA8re Hanna"));
    CHECK_EQ(c[0].phones.size(), 2u);
    CHECK_EQ(c[1].name, std::string("John Doe"));
    CHECK_EQ(c[2].name, std::string("FoldedName"));
    CHECK_EQ(c[2].phones[0], std::string("71 111 111"));
}

TEST(PhoneNormalization) {
    CHECK_EQ(NormalizePhone("03 366 160", "961"), std::string("9613366160"));
    CHECK_EQ(NormalizePhone("+961 3 366 160", "961"), std::string("9613366160"));
    CHECK_EQ(NormalizePhone("00961 3 366160", "1"), std::string("9613366160"));
    CHECK_EQ(NormalizePhone("71 111 111", "961"), std::string("96171111111"));
    CHECK_EQ(NormalizePhone("(415) 555-0100", "1"), std::string("4155550100"));
    CHECK_EQ(NormalizePhone("abc", "961"), std::string(""));
}

TEST(VcfDetection) {
    auto dir = t::TempDir("vcf");
    WriteFile(dir / "ok.vcf", "\xEF\xBB\xBF\r\nbegin:vcard\r\n");
    WriteFile(dir / "bad.vcf", "hello");
    CHECK(LooksLikeVcf(dir / "ok.vcf"));
    CHECK(!LooksLikeVcf(dir / "bad.vcf"));
    CHECK(!LooksLikeVcf(dir / "missing.vcf"));
}

TEST(MembersNewSchemaWithLid) {
    auto dir = t::TempDir("members-new");
    fs::path db = dir / "msgstore.db";
    Exec(db, R"(
        CREATE TABLE jid (_id INTEGER PRIMARY KEY, user TEXT, server TEXT, raw_string TEXT);
        CREATE TABLE jid_map (lid_row_id INTEGER PRIMARY KEY, jid_row_id INTEGER);
        CREATE TABLE group_participant_user (_id INTEGER PRIMARY KEY, group_jid_row_id INTEGER, user_jid_row_id INTEGER, rank INTEGER);
        INSERT INTO jid VALUES (1, '', 's.whatsapp.net', ''), (2, '9613366160', 's.whatsapp.net', ''),
          (3, '96171111111', 's.whatsapp.net', ''), (4, '120363023708369742', 'g.us', ''),
          (5, '888', 'lid', ''), (6, '999', 'lid', ''), (7, '96172222222', 's.whatsapp.net', ''),
          (8, '96173333333', 's.whatsapp.net', '');
        INSERT INTO jid_map VALUES (5, 7);
        INSERT INTO group_participant_user VALUES (1, 4, 1, 0), (2, 4, 3, 1), (3, 4, 2, 2), (4, 4, 5, 0), (5, 4, 6, 0), (6, 4, 8, 0);
    )");
    fs::path wa = dir / "wa.db";
    Exec(wa, R"(
        CREATE TABLE wa_contacts (jid TEXT, display_name TEXT, wa_name TEXT);
        INSERT INTO wa_contacts VALUES ('96171111111@s.whatsapp.net', 'Alice WA', NULL),
          ('96172222222@s.whatsapp.net', NULL, 'Bob Push'), ('9613366160@s.whatsapp.net', 'Hanna WA', NULL);
    )");
    WriteFile(dir / "c.vcf", "BEGIN:VCARD\nFN:P\xC3\xA8re Hanna\nTEL:03 366 160\nEND:VCARD\n");

    MembersInput in{db, wa, dir / "c.vcf", "961"};
    auto groups = BuildGroupMembers(in, nullptr);
    CHECK(groups.has_value());
    auto& m = (*groups)["120363023708369742"];
    CHECK_EQ(m.size(), 5u);  // the user themselves is skipped
    CHECK_EQ(m[0].name, std::string("P\xC3\xA8re Hanna"));  // vcf wins over wa.db
    CHECK_EQ(m[0].rank, 2);
    CHECK_EQ(m[1].name, std::string("Alice WA"));
    CHECK_EQ(m[1].rank, 1);
    CHECK_EQ(m[2].name, std::string("Bob Push"));  // lid mapped via jid_map
    CHECK_EQ(m[2].phone, std::string("96172222222"));
    CHECK_EQ(m[3].name, std::string(""));
    CHECK_EQ(m[3].phone, std::string("96173333333"));
    CHECK_EQ(m[4].phone, std::string(""));  // hidden lid

    CHECK(WriteMembersJs(in, dir / "members.js", nullptr));
    std::string js = ReadFile(dir / "members.js");
    CHECK(js.rfind("window.GROUP_MEMBERS = {\"120363023708369742\":[{\"n\":\"P\\u00e8re Hanna\",\"p\":\"9613366160\",\"a\":2}", 0) == 0);
    CHECK(js.size() > 2 && js.substr(js.size() - 2) == ";\n");
}

TEST(MembersOldSchema) {
    auto dir = t::TempDir("members-old");
    fs::path db = dir / "msgstore.db";
    Exec(db, R"(
        CREATE TABLE group_participants (_id INTEGER PRIMARY KEY, gjid TEXT, jid TEXT, admin INTEGER);
        INSERT INTO group_participants VALUES (1, '1234@g.us', '', 0), (2, '1234@g.us', '96170000000@s.whatsapp.net', 1),
          (3, '1234@g.us', '96170000001@s.whatsapp.net', 0);
    )");
    auto groups = BuildGroupMembers({db, std::nullopt, std::nullopt, ""}, nullptr);
    CHECK(groups.has_value());
    CHECK_EQ((*groups)["1234"].size(), 2u);
    CHECK_EQ((*groups)["1234"][0].rank, 1);
}

TEST(MembersMissingTables) {
    auto dir = t::TempDir("members-none");
    fs::path db = dir / "msgstore.db";
    Exec(db, "CREATE TABLE message (_id INTEGER PRIMARY KEY);");
    MembersInput in{db, std::nullopt, std::nullopt, ""};
    CHECK(!BuildGroupMembers(in, nullptr).has_value());
    CHECK(!WriteMembersJs(in, dir / "members.js", nullptr));
    CHECK(!fs::exists(dir / "members.js"));
    // Columns missing (no rank, no jid_map) still works.
    fs::path db2 = dir / "msgstore2.db";
    Exec(db2, R"(
        CREATE TABLE jid (_id INTEGER PRIMARY KEY, user TEXT, server TEXT);
        CREATE TABLE group_participant_user (_id INTEGER PRIMARY KEY, group_jid_row_id INTEGER, user_jid_row_id INTEGER);
        INSERT INTO jid VALUES (1, '55', 'g.us'), (2, '961', 's.whatsapp.net'), (3, '77', 'lid');
        INSERT INTO group_participant_user VALUES (1, 1, 2), (2, 1, 3);
    )");
    auto g = BuildGroupMembers({db2, std::nullopt, std::nullopt, ""}, nullptr);
    CHECK(g.has_value());
    CHECK_EQ((*g)["55"].size(), 2u);
    CHECK(!fs::exists(dir / "nope.db"));
    CHECK(!BuildGroupMembers({dir / "nope.db", std::nullopt, std::nullopt, ""}, nullptr).has_value());
}

TEST(MemberSorting) {
    std::vector<GroupMember> m = {{"", "1", 0}, {"bob", "2", 0}, {"Alice", "3", 0}, {"", "", 0}, {"Zed", "4", 2}, {"", "5", 1}};
    SortMembers(m);
    CHECK_EQ(m[0].name, std::string("Zed"));
    CHECK_EQ(m[1].phone, std::string("5"));
    CHECK_EQ(m[2].name, std::string("Alice"));
    CHECK_EQ(m[3].name, std::string("bob"));
    CHECK_EQ(m[4].phone, std::string("1"));
    CHECK_EQ(m[5].phone, std::string(""));  // hidden numbers last
}

TEST(MediaMoveAndResume) {
    auto dir = t::TempDir("media-move");
    WriteFile(dir / "WhatsApp/Media/WhatsApp Images/a.jpg", "a");
    WriteFile(dir / "WhatsApp/Media/WhatsApp Images/Sent/b.jpg", "b");
    WriteFile(dir / "WhatsApp/thumbnails/t.png", "t");
    fs::create_directories(dir / "WhatsApp/vCards");
    CHECK(!MoveMediaForViewer(dir, nullptr));
    CHECK(fs::exists(dir / "media/WhatsApp Images/a.jpg"));
    CHECK(fs::exists(dir / "media/WhatsApp Images/Sent/b.jpg"));
    CHECK(fs::exists(dir / "data/thumbnails/t.png"));
    CHECK(!fs::exists(dir / "WhatsApp"));

    // A retry needs the media back where the exporter looks.
    CHECK(!EnsureMediaForExporter(dir, nullptr));
    CHECK(fs::exists(dir / "WhatsApp/Media/WhatsApp Images/a.jpg"));
    CHECK(!fs::exists(dir / "media"));

    // Both locations at once (an interrupted move) are merged.
    WriteFile(dir / "media/WhatsApp Images/c.jpg", "c");
    CHECK(!EnsureMediaForExporter(dir, nullptr));
    CHECK(fs::exists(dir / "WhatsApp/Media/WhatsApp Images/c.jpg"));
    CHECK(fs::exists(dir / "WhatsApp/Media/WhatsApp Images/a.jpg"));
    CHECK(!MoveMediaForViewer(dir, nullptr));
    CHECK(fs::exists(dir / "media/WhatsApp Images/c.jpg"));
}

TEST(CleanupDeletesOnlyWhatItShould) {
    auto dir = t::TempDir("cleanup");
    WriteFile(dir / "_work/msgstore.db.crypt15", "x");
    WriteFile(dir / "_work/chats.json", "{}");
    WriteFile(dir / "media/WhatsApp Images/a.jpg", "a");
    WriteFile(dir / "media/WhatsApp Images/b.jpg.partial", "b");
    WriteFile(dir / "data/msgstore.db", "d");
    WriteFile(dir / "index.html", "i");
    WriteFile(dir / "chats.js", "c");
    WriteFile(dir / "members.js", "m");
    WriteFile(dir / "export-log.txt", "l");
    WriteFile(dir / "README.txt", "r");
    WriteFile(dir / "my-notes.txt", "user file");
    fs::create_directories(dir / "WhatsApp");
    WriteFile(dir.parent_path() / "outside.partial", "never touched");

    CleanupAfterSuccess(dir, nullptr);
    CHECK(!fs::exists(dir / "_work"));
    CHECK(!fs::exists(dir / "WhatsApp"));
    CHECK(!fs::exists(dir / "media/WhatsApp Images/b.jpg.partial"));
    for (const char* keep : {"media/WhatsApp Images/a.jpg", "data/msgstore.db", "index.html", "chats.js",
                             "members.js", "export-log.txt", "README.txt", "my-notes.txt"})
        CHECK(fs::exists(dir / keep));
    CHECK(fs::exists(dir.parent_path() / "outside.partial"));
}

TEST(CleanupAfterFailureKeepsCopiedData) {
    auto dir = t::TempDir("cleanup-fail");
    WriteFile(dir / "_work/msgstore.db.crypt15", "x");
    WriteFile(dir / "_work/wa.db.crypt15", "x");
    WriteFile(dir / "_work/chats.json", "{");
    WriteFile(dir / "WhatsApp/Media/WhatsApp Images/a.jpg", "a");
    WriteFile(dir / "WhatsApp/Media/WhatsApp Images/b.jpg.partial", "b");
    WriteFile(dir / "chats.js", "half");
    WriteFile(dir / "chats.js.partial", "half");
    WriteFile(dir / "members.js", "half");
    CleanupAfterFailure(dir, nullptr);
    CHECK(fs::exists(dir / "_work/msgstore.db.crypt15"));
    CHECK(fs::exists(dir / "_work/wa.db.crypt15"));
    CHECK(fs::exists(dir / "WhatsApp/Media/WhatsApp Images/a.jpg"));
    CHECK(!fs::exists(dir / "_work/chats.json"));
    CHECK(!fs::exists(dir / "chats.js"));
    CHECK(!fs::exists(dir / "chats.js.partial"));
    CHECK(!fs::exists(dir / "members.js"));
    CHECK(!fs::exists(dir / "WhatsApp/Media/WhatsApp Images/b.jpg.partial"));
}
