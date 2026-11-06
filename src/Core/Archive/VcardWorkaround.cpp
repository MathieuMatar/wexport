#include "VcardWorkaround.h"

#include <sqlite3.h>

#include "../Util/FileSystem.h"
#include "../Util/Strings.h"

namespace ck {

namespace fs = std::filesystem;

namespace {

bool HasTable(sqlite3* db, const char* name) {
    sqlite3_stmt* st = nullptr;
    bool found = false;
    if (sqlite3_prepare_v2(db, "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?", -1, &st, nullptr) ==
        SQLITE_OK) {
        sqlite3_bind_text(st, 1, name, -1, SQLITE_STATIC);
        found = sqlite3_step(st) == SQLITE_ROW;
    }
    sqlite3_finalize(st);
    return found;
}

}  // namespace

VcardWorkaroundResult MakeVcardWorkaroundDb(const fs::path& decryptedDb, const fs::path& copy,
                                            VcardWorkaroundLevel level) {
    VcardWorkaroundResult r;
    std::error_code ec;
    fs::create_directories(LongPath(copy.parent_path()), ec);
    fs::copy_file(LongPath(decryptedDb), LongPath(copy), fs::copy_options::overwrite_existing, ec);
    if (ec) {
        r.error = "Cannot copy the message database: " + ec.message();
        return r;
    }
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(PathToUtf8(copy).c_str(), &db, SQLITE_OPEN_READWRITE, nullptr) != SQLITE_OK) {
        r.error = std::string("Cannot open the copy: ") + (db ? sqlite3_errmsg(db) : "?");
        sqlite3_close(db);
        return r;
    }
    if (!HasTable(db, "message_vcard")) {
        sqlite3_close(db);
        r.error = "No message_vcard table";
        return r;
    }
    // The exporter's message query files a chat under the phone number when
    // chat.jid_row_id is a LID listed in jid_map; its vCard query doesn't.
    const char* sql =
        level == VcardWorkaroundLevel::LidChats && HasTable(db, "jid_map") && HasTable(db, "chat") &&
                HasTable(db, "message")
            ? "DELETE FROM message_vcard WHERE message_row_id IN ("
              " SELECT message._id FROM message JOIN chat ON chat._id = message.chat_row_id"
              " WHERE chat.jid_row_id IN (SELECT lid_row_id FROM jid_map WHERE jid_row_id IS NOT NULL))"
            : "DELETE FROM message_vcard";
    char* err = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
        r.error = err ? err : "delete failed";
        sqlite3_free(err);
        sqlite3_close(db);
        return r;
    }
    r.removed = sqlite3_changes(db);
    sqlite3_close(db);
    r.ok = true;
    return r;
}

}  // namespace ck
