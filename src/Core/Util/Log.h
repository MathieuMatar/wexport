#pragma once
// Plain-text log written to export-log.txt. Every line is redacted: any
// registered secret, and anything that looks like a 64-digit key, is replaced
// before it reaches memory or disk.

#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace ck {

std::string RedactSecrets(std::string_view text, const std::vector<std::string>& secrets);

class Logger {
public:
    // Lines logged before Open() are kept and written when the file opens.
    void Open(const std::filesystem::path& file);
    void Close();

    void Info(std::string_view message) { Write("INFO", message); }
    void Warn(std::string_view message) { Write("WARN", message); }
    void Error(std::string_view message) { Write("ERROR", message); }

    void AddSecret(std::string secret);
    void ClearSecrets();

    std::vector<std::string> Lines() const;

private:
    void Write(std::string_view level, std::string_view message);

    mutable std::mutex mutex_;
    std::ofstream file_;
    std::vector<std::string> lines_;
    std::vector<std::string> secrets_;
};

}  // namespace ck
