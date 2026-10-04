#pragma once
// The wtsexporter command line (§5.1). Paths are relative to the export
// folder, which is the process's working directory.

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ck {

inline constexpr const char* kPinnedExporterVersion = "0.13.0";
inline constexpr const char* kChatsJson = "_work/chats.json";
inline constexpr const char* kDataDir = "data";
inline constexpr const char* kMessagesDb = "data/msgstore.db";
inline constexpr const char* kContactsDb = "data/wa.db";

struct ExporterOptions {
    std::string key;  // 64 hex digits
    bool hasContactsBackup = false;
    std::optional<std::filesystem::path> vcf;
    std::string countryCode;  // digits only; used with the vcf
};

// Arguments after the executable. Never contains --check-update or -c, and
// -k always has a value.
std::vector<std::string> BuildExporterArgs(const ExporterOptions& options);

// For logs: the same arguments with the key replaced.
std::string DescribeExporterArgs(const std::vector<std::string>& args);

// Windows command-line quoting that round-trips through CommandLineToArgvW
// and the MSVC/Python argument parser.
std::string QuoteWindowsArg(const std::string& arg);
std::string JoinWindowsCommandLine(const std::string& exe, const std::vector<std::string>& args);

}  // namespace ck
