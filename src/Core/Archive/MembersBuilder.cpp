#include "MembersBuilder.h"

#include <algorithm>
#include <fstream>
#include <set>

#include <nlohmann/json.hpp>
#include <sqlite3.h>

#include "../Util/FileSystem.h"
#include "../Util/Strings.h"
#include "Vcf.h"

namespace ck {

namespace {

class Db {
public:
    explicit Db(const std::filesystem::path& path) {
        std::string uri = PathToUtf8(path);
        if (sqlite3_open_v2(uri.c_str(), &db_, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
            error_ = db_ ? sqlite3_errmsg(db_) : "cannot open";
            sqlite3_close(db_);
            db_ = nullptr;
        }
    }
    ~Db() { sqlite3_close(db_); }
    Db(const Db&) = delete;
    Db& operator=(const Db&) = delete;

    explicit operator bool() const { return db_ != nullptr; }
    const std::string& Error() const { return error_; }

    bool HasTable(const std::string& name) {
        bool found = false;
        Query("SELECT 1 FROM sqlite_master WHERE type='table' AND name='" + name + "'",
              [&](sqlite3_stmt*) { found = true; });
        return found;
    }
    std::set<std::string> Columns(const std::string& table) {
        std::set<std::string> cols;
        Query("PRAGMA table_info(" + table + ")", [&](sqlite3_stmt* st) { cols.insert(Text(st, 1)); });
        return cols;
    }
    template <typename Fn>
    bool Query(const std::string& sql, Fn&& onRow) {
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &st, nullptr) != SQLITE_OK) {
            error_ = sqlite3_errmsg(db_);
            return false;
        }
        int rc;
        while ((rc = sqlite3_step(st)) == SQLITE_ROW) onRow(st);
        sqlite3_finalize(st);
        if (rc != SQLITE_DONE) error_ = sqlite3_errmsg(db_);
        return rc == SQLITE_DONE;
    }
    static std::string Text(sqlite3_stmt* st, int col) {
        auto p = sqlite3_column_text(st, col);
        return p ? reinterpret_cast<const char*>(p) : "";
    }

private:
    sqlite3* db_ = nullptr;
    std::string error_;
};

std::string UserPart(const std::string& jid) { return jid.substr(0, jid.find('@')); }
std::string ServerPart(const std::string& jid) {
    auto at = jid.find('@');
    return at == std::string::npos ? "" : jid.substr(at + 1);
}

bool AllDigits(const std::string& s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; });
}

struct RawMember {
    std::string group;
    std::string phone;
    int rank;
};

bool ReadNewSchema(Db& db, std::vector<RawMember>& out, Logger* log) {
    auto cols = db.Columns("group_participant_user");
    if (!cols.count("group_jid_row_id") || !cols.count("user_jid_row_id")) return false;
    auto jidCols = db.Columns("jid");
    if (!jidCols.count("_id") || !jidCols.count("user") || !jidCols.count("server")) return false;
    bool hasRank = cols.count("rank") > 0;
    bool hasMap = db.HasTable("jid_map") && db.Columns("jid_map").count("lid_row_id") &&
                  db.Columns("jid_map").count("jid_row_id");

    std::string sql = "SELECT g.user, g.server, p.user, p.server, " + std::string(hasRank ? "gpu.rank" : "0") +
                      ", " + (hasMap ? "m.user, m.server" : "NULL, NULL") +
                      " FROM group_participant_user gpu"
                      " JOIN jid g ON g._id = gpu.group_jid_row_id"
                      " JOIN jid p ON p._id = gpu.user_jid_row_id" +
                      (hasMap ? " LEFT JOIN jid_map jm ON jm.lid_row_id = p._id"
                                " LEFT JOIN jid m ON m._id = jm.jid_row_id"
                              : "");
    bool ok = db.Query(sql, [&](sqlite3_stmt* st) {
        std::string gUser = Db::Text(st, 0), gServer = Db::Text(st, 1);
        std::string pUser = Db::Text(st, 2), pServer = Db::Text(st, 3);
        int rank = sqlite3_column_int(st, 4);
        std::string mUser = Db::Text(st, 5), mServer = Db::Text(st, 6);
        if (gServer != "g.us" || gUser.empty()) return;
        if (pUser.empty()) return;  // the user themselves
        std::string phone;
        if (pServer == "s.whatsapp.net") phone = pUser;
        else if (pServer == "lid" && mServer == "s.whatsapp.net") phone = mUser;
        if (!AllDigits(phone)) phone.clear();
        out.push_back({gUser, phone, std::clamp(rank, 0, 2)});
    });
    if (!ok && log) log->Warn("Reading group_participant_user failed: " + db.Error());
    return ok;
}

bool ReadOldSchema(Db& db, std::vector<RawMember>& out, Logger* log) {
    auto cols = db.Columns("group_participants");
    if (!cols.count("gjid") || !cols.count("jid")) return false;
    bool hasAdmin = cols.count("admin") > 0;
    bool ok = db.Query(std::string("SELECT gjid, jid, ") + (hasAdmin ? "admin" : "0") + " FROM group_participants",
                       [&](sqlite3_stmt* st) {
                           std::string g = Db::Text(st, 0), p = Db::Text(st, 1);
                           if (ServerPart(g) != "g.us" || p.empty()) return;
                           std::string phone = ServerPart(p) == "s.whatsapp.net" ? UserPart(p) : "";
                           if (!AllDigits(phone)) phone.clear();
                           out.push_back({UserPart(g), phone, std::clamp(sqlite3_column_int(st, 2), 0, 2)});
                       });
    if (!ok && log) log->Warn("Reading group_participants failed: " + db.Error());
    return ok;
}

std::map<std::string, std::string> WaNames(const std::filesystem::path& waDb, Logger* log) {
    std::map<std::string, std::string> names;
    std::error_code ec;
    if (!std::filesystem::exists(waDb, ec)) return names;
    Db db(waDb);
    if (!db || !db.HasTable("wa_contacts")) return names;
    auto cols = db.Columns("wa_contacts");
    if (!cols.count("jid")) return names;
    std::string display = cols.count("display_name") ? "display_name" : "NULL";
    std::string wa = cols.count("wa_name") ? "wa_name" : "NULL";
    bool ok = db.Query("SELECT jid, " + display + ", " + wa + " FROM wa_contacts", [&](sqlite3_stmt* st) {
        std::string jid = Db::Text(st, 0);
        if (ServerPart(jid) != "s.whatsapp.net") return;
        std::string name = Trim(Db::Text(st, 1));
        if (name.empty()) name = Trim(Db::Text(st, 2));
        if (!name.empty()) names.emplace(UserPart(jid), name);
    });
    if (!ok && log) log->Warn("Reading wa_contacts failed: " + db.Error());
    return names;
}

}  // namespace

void SortMembers(std::vector<GroupMember>& members) {
    std::stable_sort(members.begin(), members.end(), [](const GroupMember& a, const GroupMember& b) {
        if (a.rank != b.rank) return a.rank > b.rank;
        bool an = !a.name.empty(), bn = !b.name.empty();
        if (an != bn) return an;
        bool ap = !a.phone.empty(), bp = !b.phone.empty();
        if (!an && ap != bp) return ap;  // hidden numbers last
        std::string al = ToLowerAscii(a.name), bl = ToLowerAscii(b.name);
        if (al != bl) return al < bl;
        return a.phone < b.phone;
    });
}

std::optional<GroupMembers> BuildGroupMembers(const MembersInput& input, Logger* log) {
    Db db(input.messagesDb);
    if (!db) {
        if (log) log->Warn("Cannot open msgstore.db for members: " + db.Error());
        return std::nullopt;
    }
    std::vector<RawMember> raw;
    bool ok = false;
    if (db.HasTable("group_participant_user") && db.HasTable("jid")) ok = ReadNewSchema(db, raw, log);
    if (!ok && db.HasTable("group_participants")) {
        raw.clear();
        ok = ReadOldSchema(db, raw, log);
    }
    if (!ok) {
        if (log) log->Warn("No group members table found; members.js not written");
        return std::nullopt;
    }

    std::map<std::string, std::string> vcfNames, waNames;
    if (input.vcf) vcfNames = VcfPhoneBook(*input.vcf, input.countryCode);
    if (input.contactsDb) waNames = WaNames(*input.contactsDb, log);

    GroupMembers groups;
    for (const auto& m : raw) {
        GroupMember gm;
        gm.phone = m.phone;
        gm.rank = m.rank;
        if (!m.phone.empty()) {
            if (auto it = vcfNames.find(m.phone); it != vcfNames.end()) gm.name = it->second;
            else if (auto it2 = waNames.find(m.phone); it2 != waNames.end()) gm.name = it2->second;
        }
        groups[m.group].push_back(std::move(gm));
    }
    for (auto& [id, members] : groups) SortMembers(members);
    if (log) log->Info("Group members: " + std::to_string(groups.size()) + " groups, " + std::to_string(raw.size()) +
                       " participants");
    return groups;
}

std::string MembersToJs(const GroupMembers& groups) {
    nlohmann::ordered_json root = nlohmann::ordered_json::object();
    for (const auto& [id, members] : groups) {
        auto arr = nlohmann::ordered_json::array();
        for (const auto& m : members) arr.push_back({{"n", m.name}, {"p", m.phone}, {"a", m.rank}});
        root[id] = std::move(arr);
    }
    // ensure_ascii keeps the file 7-bit (no U+2028 surprises in a script).
    return "window.GROUP_MEMBERS = " +
           root.dump(-1, ' ', true, nlohmann::ordered_json::error_handler_t::replace) + ";\n";
}

bool WriteMembersJs(const MembersInput& input, const std::filesystem::path& out, Logger* log) {
    auto groups = BuildGroupMembers(input, log);
    if (!groups) return false;
    std::filesystem::path temp = out;
    temp += ".partial";
    {
        std::ofstream f(LongPath(temp), std::ios::binary | std::ios::trunc);
        f << MembersToJs(*groups);
        if (!f) {
            if (log) log->Warn("Cannot write members.js");
            return false;
        }
    }
    if (auto err = RenameWithRetry(temp, out)) {
        if (log) log->Warn("Cannot finish members.js: " + *err);
        return false;
    }
    return true;
}

}  // namespace ck
