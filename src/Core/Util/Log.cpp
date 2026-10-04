#include "Log.h"

#include <chrono>
#include <cstdio>
#include <ctime>

#include "Strings.h"

namespace ck {

namespace {

bool IsHexChar(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

// Replaces runs of hex digits (optionally split by single spaces or dashes,
// the way WhatsApp shows the key) that add up to 64 or more digits.
std::string RedactKeyLike(std::string_view text) {
    std::string out;
    size_t i = 0;
    while (i < text.size()) {
        if (!IsHexChar(text[i])) { out += text[i++]; continue; }
        size_t j = i, digits = 0, lastHexEnd = i;
        while (j < text.size()) {
            if (IsHexChar(text[j])) { ++digits; ++j; lastHexEnd = j; continue; }
            if ((text[j] == ' ' || text[j] == '-') && j + 1 < text.size() && IsHexChar(text[j + 1])) { ++j; continue; }
            break;
        }
        if (digits >= 64) {
            out += "<key redacted>";
        } else {
            out.append(text.substr(i, lastHexEnd - i));
        }
        i = lastHexEnd;
    }
    return out;
}

std::string Timestamp() {
    std::time_t now = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", &tm);
    return buf;
}

}  // namespace

std::string RedactSecrets(std::string_view text, const std::vector<std::string>& secrets) {
    std::string s(text);
    for (const auto& secret : secrets) {
        if (secret.empty()) continue;
        size_t pos = 0;
        while ((pos = s.find(secret, pos)) != std::string::npos) {
            s.replace(pos, secret.size(), "<key redacted>");
            pos += 14;
        }
    }
    return RedactKeyLike(s);
}

void Logger::Open(const std::filesystem::path& file) {
    std::lock_guard lock(mutex_);
    if (file_.is_open()) file_.close();
    file_.open(file, std::ios::out | std::ios::app | std::ios::binary);
    for (const auto& line : lines_) file_ << line << "\r\n";
    file_.flush();
}

void Logger::Close() {
    std::lock_guard lock(mutex_);
    if (file_.is_open()) file_.close();
}

void Logger::AddSecret(std::string secret) {
    std::lock_guard lock(mutex_);
    secrets_.push_back(std::move(secret));
}

void Logger::ClearSecrets() {
    std::lock_guard lock(mutex_);
    for (auto& s : secrets_) SecureClear(s);
    secrets_.clear();
}

std::vector<std::string> Logger::Lines() const {
    std::lock_guard lock(mutex_);
    return lines_;
}

void Logger::Write(std::string_view level, std::string_view message) {
    std::lock_guard lock(mutex_);
    std::string line = Timestamp() + " [" + std::string(level) + "] " + RedactSecrets(message, secrets_);
    lines_.push_back(line);
    if (lines_.size() > 20000) lines_.erase(lines_.begin(), lines_.begin() + 5000);
    if (file_.is_open()) {
        file_ << line << "\r\n";
        file_.flush();
    }
}

}  // namespace ck
