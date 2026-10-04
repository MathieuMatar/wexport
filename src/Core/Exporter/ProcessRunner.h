#pragma once
// Runs a child process with its output captured, no console window, and a
// cancel that kills the whole process tree.

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "../Util/Cancel.h"

namespace ck {

struct ProcessSpec {
    std::filesystem::path executable;
    std::vector<std::string> args;
    std::filesystem::path workingDirectory;
    std::vector<std::pair<std::string, std::string>> extraEnvironment;
};

struct ProcessResult {
    int exitCode = -1;
    bool cancelled = false;
    bool startFailed = false;
    std::string startError;
};

// `onOutput` is called from reader threads (one per pipe, serialized by a
// mutex) with raw chunks of stdout/stderr.
ProcessResult RunProcess(const ProcessSpec& spec, const CancelToken& cancel,
                         const std::function<void(std::string_view)>& onOutput);

}  // namespace ck
