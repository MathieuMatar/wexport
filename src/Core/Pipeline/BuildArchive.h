#pragma once
// Step 6: unlock the backup with wtsexporter, build the viewer files, clean up.

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

#include "../Archive/ChatsJsWriter.h"
#include "../Exporter/ExporterOutput.h"
#include "../Util/Cancel.h"
#include "../Util/Log.h"

namespace ck {

enum class BuildStage { Unlocking, Reading, Building, CleaningUp, Finished };

struct BuildProgress {
    BuildStage stage = BuildStage::Unlocking;
    std::optional<double> fraction;  // within the stage
    std::string detail;
};

struct BuildOptions {
    std::filesystem::path exportDir;
    std::filesystem::path exporterExe;
    std::filesystem::path viewerHtml;
    std::string key;  // cleared by RunBuild
    std::optional<std::filesystem::path> vcf;
    std::string countryCode;
};

struct BuildResult {
    enum class Status { Ok, WrongKey, ExporterFailed, BuildFailed, Cancelled } status = Status::BuildFailed;
    std::string message;  // plain language
    std::string details;  // technical (last lines of output), never contains the key
    ArchiveStats stats;
    std::uint64_t folderBytes = 0;
    bool membersWritten = false;
};

BuildResult RunBuild(BuildOptions options, const CancelToken& cancel,
                     const std::function<void(const BuildProgress&)>& onProgress, Logger& log);

}  // namespace ck
