#pragma once
// What to copy from the phone (§4.2), worked out before anything is copied so
// the totals are exact.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "../Device/WhatsAppLocator.h"
#include "../Util/Cancel.h"

namespace ck {

// Paths inside the export folder (§7). Forward slashes; joined with fs::path.
inline constexpr const char* kWorkDir = "_work";
inline constexpr const char* kMessagesBackup = "_work/msgstore.db.crypt15";
inline constexpr const char* kContactsBackup = "_work/wa.db.crypt15";
inline constexpr const char* kExporterMediaDir = "WhatsApp/Media";
inline constexpr const char* kExporterMediaRoot = "WhatsApp";

// Media sub-folders that aren't copied: any folder or file whose name starts
// with one of these prefixes (".Statuses", ".trash", ".Links", ".nomedia", ...).
const std::vector<std::string>& SkippedMediaPrefixes();
bool IsSkippedMediaName(const std::string& name);

struct CopyItem {
    DeviceEntry source;
    std::string sourcePath;  // "Media/WhatsApp Images/IMG-1.jpg" (phone-relative, for logs)
    std::string destPath;    // "WhatsApp/Media/WhatsApp Images/IMG-1.jpg" (sanitized)
};

struct RenamedFile {
    std::string phonePath;
    std::string savedAs;
};

struct CopyPlan {
    std::vector<CopyItem> items;  // database files first, then media
    std::uint64_t totalBytes = 0;
    std::vector<RenamedFile> renamed;  // names Windows didn't accept
    bool hasContactsBackup = false;
};

using PlanProgress = std::function<void(std::uint64_t filesFound, std::uint64_t bytesFound)>;

CopyPlan BuildCopyPlan(IDeviceSource& source, const BackupInfo& backup, const CancelToken& cancel,
                       const PlanProgress& progress = {});

}  // namespace ck
