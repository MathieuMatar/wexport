#pragma once
// Copies a CopyPlan into the export folder (§4.3). Resumable: a destination
// file with the same size is skipped. Each file is streamed into a ".partial"
// temp file and renamed when complete, then given the phone's modified time.

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "../Util/Cancel.h"
#include "../Util/Log.h"
#include "CopyPlan.h"

namespace ck {

inline constexpr const char* kPartialSuffix = ".partial";

struct CopyProgress {
    std::uint64_t filesDone = 0, filesTotal = 0;
    std::uint64_t bytesDone = 0, bytesTotal = 0;
    std::string currentFile;
    double bytesPerSecond = 0;  // measured over the bytes actually copied
    double secondsLeft = -1;
};

struct FailedFile {
    std::string phonePath;
    std::string reason;
};

struct CopyResult {
    enum class Status { Completed, Cancelled, Disconnected } status = Status::Completed;
    std::uint64_t copiedFiles = 0, skippedFiles = 0, copiedBytes = 0;
    std::vector<FailedFile> failed;
    std::size_t resumeIndex = 0;  // first item not finished when Disconnected
};

using CopyProgressFn = std::function<void(const CopyProgress&)>;

CopyResult RunCopy(IDeviceSource& source, const CopyPlan& plan, const std::filesystem::path& exportDir,
                   const CancelToken& cancel, const CopyProgressFn& onProgress, Logger* log,
                   std::size_t startIndex = 0);

}  // namespace ck
