#include "CopyPlan.h"

#include "../Util/Strings.h"

namespace ck {

const std::vector<std::string>& SkippedMediaPrefixes() {
    static const std::vector<std::string> kPrefixes = {"."};
    return kPrefixes;
}

bool IsSkippedMediaName(const std::string& name) {
    for (const auto& p : SkippedMediaPrefixes())
        if (StartsWith(name, p)) return true;
    return false;
}

namespace {

void WalkMedia(IDeviceSource& source, const DeviceEntry& dir, const std::string& phonePath,
               const std::string& destPath, CopyPlan& plan, const CancelToken& cancel,
               const PlanProgress& progress) {
    cancel.ThrowIfCancelled();
    for (const auto& child : source.List(dir)) {
        if (IsSkippedMediaName(child.name)) continue;
        std::string safe = SanitizeFileName(child.name);
        std::string childPhone = phonePath + "/" + child.name;
        std::string childDest = destPath + "/" + safe;
        if (safe != child.name) plan.renamed.push_back({childPhone, childDest});
        if (child.isDirectory) {
            WalkMedia(source, child, childPhone, childDest, plan, cancel, progress);
        } else {
            plan.items.push_back({child, childPhone, childDest});
            plan.totalBytes += child.size;
            if (progress && plan.items.size() % 200 == 0) progress(plan.items.size(), plan.totalBytes);
        }
    }
}

}  // namespace

CopyPlan BuildCopyPlan(IDeviceSource& source, const BackupInfo& backup, const CancelToken& cancel,
                       const PlanProgress& progress) {
    CopyPlan plan;
    if (backup.messages) {
        plan.items.push_back({*backup.messages, "Databases/" + backup.messages->name, kMessagesBackup});
        plan.totalBytes += backup.messages->size;
    }
    if (backup.contacts) {
        plan.items.push_back({*backup.contacts, "Backups/" + backup.contacts->name, kContactsBackup});
        plan.totalBytes += backup.contacts->size;
        plan.hasContactsBackup = true;
    }
    if (backup.media) WalkMedia(source, *backup.media, "Media", kExporterMediaDir, plan, cancel, progress);
    if (progress) progress(plan.items.size(), plan.totalBytes);
    return plan;
}

}  // namespace ck
