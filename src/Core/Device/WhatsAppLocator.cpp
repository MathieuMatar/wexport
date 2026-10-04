#include "WhatsAppLocator.h"

#include "../Util/Strings.h"

namespace ck {

namespace {

struct Candidate {
    std::vector<std::string> path;
    bool business;
};

const std::vector<Candidate>& Candidates() {
    static const std::vector<Candidate> kCandidates = {
        {{"Android", "media", "com.whatsapp", "WhatsApp"}, false},
        {{"WhatsApp"}, false},
        {{"Android", "media", "com.whatsapp.w4b", "WhatsApp Business"}, true},
        {{"WhatsApp Business"}, true},
    };
    return kCandidates;
}

bool LooksLikeWhatsAppFolder(const std::vector<DeviceEntry>& children) {
    return FindChild(children, "Databases", true) || FindChild(children, "Media", true);
}

}  // namespace

std::string WhatsAppFolder::PathText() const {
    std::string s;
    for (const auto& p : path) {
        if (!s.empty()) s += '/';
        s += p;
    }
    return s;
}

std::vector<WhatsAppFolder> FindWhatsAppFolders(IDeviceSource& source) {
    std::vector<WhatsAppFolder> found;
    for (const auto& storage : source.Storages()) {
        auto rootChildren = source.List(storage);
        if (LooksLikeWhatsAppFolder(rootChildren)) {
            WhatsAppFolder f{storage, storage.name, {}, storage,
                             StartsWith(ToLowerAscii(storage.name), "whatsapp business")};
            found.push_back(f);
            continue;
        }
        bool standardFound = false, businessFound = false;
        for (const auto& c : Candidates()) {
            if ((c.business && businessFound) || (!c.business && standardFound)) continue;
            std::optional<DeviceEntry> dir;
            try {
                dir = ResolvePath(source, storage, c.path);
            } catch (const DeviceError& e) {
                if (e.kind == DeviceErrorKind::Disconnected) throw;
                continue;
            }
            if (!dir) continue;
            auto children = source.List(*dir);
            if (!LooksLikeWhatsAppFolder(children)) continue;
            found.push_back({storage, storage.name, c.path, *dir, c.business});
            (c.business ? businessFound : standardFound) = true;
        }
    }
    return found;
}

BackupInfo InspectBackup(IDeviceSource& source, const WhatsAppFolder& folder) {
    BackupInfo info;
    auto children = source.List(folder.folder);
    bool anyOlder = false;

    if (auto databases = FindChild(children, "Databases", true)) {
        for (const auto& f : source.List(*databases)) {
            if (f.isDirectory) continue;
            std::string lower = ToLowerAscii(f.name);
            if (!StartsWith(lower, "msgstore")) continue;
            if (EndsWith(lower, ".crypt15")) {
                bool better = !info.messages || f.modified > info.messages->modified ||
                              (f.modified == info.messages->modified && lower == "msgstore.db.crypt15");
                if (better) info.messages = f;
            } else if (lower.find(".crypt") != std::string::npos) {
                anyOlder = true;
            }
        }
    }
    if (auto backups = FindChild(children, "Backups", true)) {
        for (const auto& f : source.List(*backups))
            if (!f.isDirectory && EqualsIgnoreCase(f.name, "wa.db.crypt15")) info.contacts = f;
    }
    info.media = FindChild(children, "Media", true);
    info.hasOlderFormatOnly = !info.messages && anyOlder;
    return info;
}

bool IsBackupStale(std::int64_t modified, std::int64_t now) {
    return modified > 0 && now - modified > 24 * 3600;
}

}  // namespace ck
