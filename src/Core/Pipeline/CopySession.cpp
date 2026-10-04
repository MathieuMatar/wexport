#include "CopySession.h"

#include <chrono>
#include <fstream>
#include <thread>

#include "../Archive/ExportFolder.h"
#include "../Util/FileSystem.h"
#include "../Util/Strings.h"

namespace ck {

namespace {

std::optional<WhatsAppFolder> Relocate(IDeviceSource& source, const WhatsAppFolder& previous) {
    for (const auto& f : FindWhatsAppFolders(source))
        if (f.path == previous.path && f.storageName == previous.storageName) return f;
    for (const auto& f : FindWhatsAppFolders(source))
        if (f.path == previous.path) return f;
    return std::nullopt;
}

}  // namespace

CopySessionResult CopyWithReconnect(IDeviceSource& source, const WhatsAppFolder& folder, CopyPlan plan,
                                    const std::filesystem::path& exportDir, const CancelToken& cancel,
                                    const CopySessionCallbacks& cb, Logger* log) {
    CopySessionResult out;
    KeepAwake awake;
    WhatsAppFolder current = folder;
    std::size_t start = 0;
    for (;;) {
        CopyResult r = RunCopy(source, plan, exportDir, cancel, cb.progress, log, start);
        out.copiedFiles += r.copiedFiles;
        out.copiedBytes += r.copiedBytes;
        out.skippedFiles += r.skippedFiles;
        out.failed.insert(out.failed.end(), r.failed.begin(), r.failed.end());
        if (r.status != CopyResult::Status::Disconnected) {
            out.status = r.status;
            break;
        }
        if (cb.connectionChanged) cb.connectionChanged(true);
        bool back = false;
        while (!cancel.IsCancelled()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            try {
                if (source.TryReconnect()) {
                    if (auto f = Relocate(source, current)) {
                        current = *f;
                        BackupInfo info = InspectBackup(source, current);
                        plan = BuildCopyPlan(source, info, cancel);
                        back = true;
                        break;
                    }
                }
            } catch (const std::exception& e) {
                if (log) log->Info(std::string("Still waiting for the phone: ") + e.what());
            }
        }
        if (cb.connectionChanged) cb.connectionChanged(false);
        if (!back) {
            out.status = CopyResult::Status::Cancelled;
            break;
        }
        if (log) log->Info("Phone reconnected; resuming copy");
        start = 0;  // re-enumerated: finished files are skipped by size
        // Failures from before the disconnect are retried.
        out.failed.clear();
    }
    out.plan = std::move(plan);
    if (log)
        log->Info("Copy finished: " + std::to_string(out.copiedFiles) + " copied, " +
                  std::to_string(out.skippedFiles) + " already there, " + std::to_string(out.failed.size()) +
                  " failed");
    return out;
}

void WriteRenamedList(const CopyPlan& plan, const std::filesystem::path& exportDir) {
    if (plan.renamed.empty()) return;
    std::ofstream f(LongPath(exportDir / kRenamedFilesList), std::ios::binary | std::ios::trunc);
    f << "These names aren't allowed on Windows, so the files were saved under a slightly different name.\r\n"
         "The viewer may show them as missing; the files themselves are in the media folder.\r\n\r\n";
    for (const auto& r : plan.renamed) {
        std::string saved = r.savedAs;
        if (StartsWith(saved, "WhatsApp/Media/")) saved = "media/" + saved.substr(15);
        f << r.phonePath << "\r\n    -> " << saved << "\r\n";
    }
}

}  // namespace ck
