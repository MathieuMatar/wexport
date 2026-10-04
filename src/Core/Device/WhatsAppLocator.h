#pragma once
// Finds the WhatsApp folder on a phone (§4.1) and reads what's in it.

#include <optional>
#include <string>
#include <vector>

#include "IDeviceSource.h"

namespace ck {

struct WhatsAppFolder {
    DeviceEntry storage;
    std::string storageName;
    std::vector<std::string> path;  // folder names from the storage root
    DeviceEntry folder;
    bool business = false;

    std::string PathText() const;  // "Android/media/com.whatsapp/WhatsApp"
};

struct BackupInfo {
    std::optional<DeviceEntry> messages;  // newest msgstore*.crypt15 in Databases/
    std::optional<DeviceEntry> contacts;  // Backups/wa.db.crypt15
    std::optional<DeviceEntry> media;     // Media/
    bool hasOlderFormatOnly = false;      // .crypt14 or older, but no .crypt15
};

// Search order: Android/media/com.whatsapp/WhatsApp, WhatsApp, then the two
// WhatsApp Business locations. Every storage is searched. A folder source whose
// root *is* a WhatsApp folder (has Databases/ or Media/) is returned as-is.
std::vector<WhatsAppFolder> FindWhatsAppFolders(IDeviceSource& source);

BackupInfo InspectBackup(IDeviceSource& source, const WhatsAppFolder& folder);

// True if `modified` (Unix seconds) is more than 24 h before `now`.
bool IsBackupStale(std::int64_t modified, std::int64_t now);

}  // namespace ck
