#include "Copier.h"

#include <chrono>
#include <fstream>
#include <vector>

#include "../Util/FileSystem.h"
#include "../Util/Strings.h"

namespace ck {

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

namespace {

constexpr std::size_t kBufferSize = 1 << 20;

bool AlreadyCopied(const fs::path& dest, std::uint64_t size) {
    std::error_code ec;
    auto existing = fs::file_size(LongPath(dest), ec);
    return !ec && existing == size;
}

}  // namespace

CopyResult RunCopy(IDeviceSource& source, const CopyPlan& plan, const fs::path& exportDir,
                   const CancelToken& cancel, const CopyProgressFn& onProgress, Logger* log,
                   std::size_t startIndex) {
    CopyResult result;
    CopyProgress p;
    p.filesTotal = plan.items.size();
    p.bytesTotal = plan.totalBytes;
    for (std::size_t i = 0; i < startIndex && i < plan.items.size(); ++i) {
        p.bytesDone += plan.items[i].source.size;
        ++p.filesDone;
    }

    std::vector<char> buffer(kBufferSize);
    auto started = Clock::now();
    auto lastReport = Clock::time_point{};
    std::uint64_t bytesThisRun = 0;

    auto report = [&](bool force) {
        if (!onProgress) return;
        auto now = Clock::now();
        if (!force && now - lastReport < std::chrono::milliseconds(100)) return;
        lastReport = now;
        double secs = std::chrono::duration<double>(now - started).count();
        p.bytesPerSecond = secs > 1 ? bytesThisRun / secs : 0;
        p.secondsLeft = p.bytesPerSecond > 0 ? (p.bytesTotal - p.bytesDone) / p.bytesPerSecond : -1;
        onProgress(p);
    };

    for (std::size_t i = startIndex; i < plan.items.size(); ++i) {
        const auto& item = plan.items[i];
        if (cancel.IsCancelled()) {
            result.status = CopyResult::Status::Cancelled;
            result.resumeIndex = i;
            return result;
        }
        p.currentFile = item.source.name;
        fs::path dest = exportDir / PathFromUtf8(item.destPath);

        if (AlreadyCopied(dest, item.source.size)) {
            ++result.skippedFiles;
            ++p.filesDone;
            p.bytesDone += item.source.size;
            report(false);
            continue;
        }
        report(false);

        fs::path temp = dest;
        temp += kPartialSuffix;
        std::error_code ec;
        fs::create_directories(LongPath(dest.parent_path()), ec);

        std::uint64_t fileBytes = 0;
        try {
            auto in = source.Open(item.source);
            std::ofstream out(LongPath(temp), std::ios::binary | std::ios::trunc);
            if (!out) throw std::runtime_error("Cannot create the file on this PC");
            for (;;) {
                if (cancel.IsCancelled()) {
                    out.close();
                    fs::remove(LongPath(temp), ec);
                    p.bytesDone -= fileBytes;
                    result.status = CopyResult::Status::Cancelled;
                    result.resumeIndex = i;
                    report(true);
                    return result;
                }
                std::size_t n = in->Read(buffer.data(), buffer.size());
                if (n == 0) break;
                out.write(buffer.data(), static_cast<std::streamsize>(n));
                if (!out) throw std::runtime_error("Write failed (is the disk full?)");
                fileBytes += n;
                bytesThisRun += n;
                p.bytesDone += n;
                report(false);
            }
            out.close();
            if (!out) throw std::runtime_error("Write failed (is the disk full?)");
            if (auto err = RenameWithRetry(temp, dest)) throw std::runtime_error("Rename failed: " + *err);
            SetModifiedTime(dest, item.source.modified);
            // A size mismatch is reported but kept: MTP sizes are sometimes stale.
            if (fileBytes != item.source.size && log)
                log->Warn("Size differs for " + item.sourcePath + ": phone said " +
                          std::to_string(item.source.size) + ", copied " + std::to_string(fileBytes));
            p.bytesDone += item.source.size - std::min<std::uint64_t>(item.source.size, fileBytes);
            ++result.copiedFiles;
            result.copiedBytes += fileBytes;
        } catch (const DeviceError& e) {
            fs::remove(LongPath(temp), ec);
            p.bytesDone -= fileBytes;
            if (e.kind == DeviceErrorKind::Disconnected || !source.IsConnected()) {
                if (log) log->Warn("Phone disconnected while copying " + item.sourcePath);
                result.status = CopyResult::Status::Disconnected;
                result.resumeIndex = i;
                report(true);
                return result;
            }
            if (log) log->Warn("Could not copy " + item.sourcePath + ": " + e.what());
            result.failed.push_back({item.sourcePath, e.what()});
            p.bytesDone += item.source.size;
        } catch (const std::exception& e) {
            fs::remove(LongPath(temp), ec);
            p.bytesDone -= fileBytes;
            if (!source.IsConnected()) {
                result.status = CopyResult::Status::Disconnected;
                result.resumeIndex = i;
                report(true);
                return result;
            }
            if (log) log->Warn("Could not copy " + item.sourcePath + ": " + e.what());
            result.failed.push_back({item.sourcePath, e.what()});
            p.bytesDone += item.source.size;
        }
        ++p.filesDone;
        report(false);
    }
    p.currentFile.clear();
    report(true);
    result.resumeIndex = plan.items.size();
    return result;
}

}  // namespace ck
