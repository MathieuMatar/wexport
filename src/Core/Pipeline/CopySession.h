#pragma once
// Step 3: copy with automatic pause/resume when the phone is unplugged.

#include <filesystem>
#include <functional>

#include "../Copy/Copier.h"
#include "../Copy/CopyPlan.h"
#include "../Device/WhatsAppLocator.h"

namespace ck {

struct CopySessionCallbacks {
    CopyProgressFn progress;
    std::function<void(bool waitingForPhone)> connectionChanged;
};

struct CopySessionResult {
    CopyResult::Status status = CopyResult::Status::Completed;
    std::uint64_t copiedFiles = 0, skippedFiles = 0, copiedBytes = 0;
    std::vector<FailedFile> failed;
    CopyPlan plan;  // the final plan (rebuilt after a reconnect)
};

// Copies `plan`. If the device disappears, waits (polling TryReconnect) until it
// is back or the user cancels, finds the WhatsApp folder again by its path,
// re-enumerates, and continues; files already copied are skipped by size.
CopySessionResult CopyWithReconnect(IDeviceSource& source, const WhatsAppFolder& folder, CopyPlan plan,
                                    const std::filesystem::path& exportDir, const CancelToken& cancel,
                                    const CopySessionCallbacks& callbacks, Logger* log);

// Writes renamed-files.txt (phone name -> saved name) when Windows needed changes.
void WriteRenamedList(const CopyPlan& plan, const std::filesystem::path& exportDir);

}  // namespace ck
