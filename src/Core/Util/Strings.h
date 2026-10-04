#pragma once
// Text helpers shared by the whole app. All std::string values are UTF-8.

#include <cstdint>
#include <ctime>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace ck {

std::string ToUtf8(std::wstring_view text);
std::wstring ToWide(std::string_view utf8);

// Path <-> UTF-8 without going through the ANSI code page on Windows.
std::string PathToUtf8(const std::filesystem::path& path);
std::filesystem::path PathFromUtf8(std::string_view utf8);

// Accepts the key the way people type or paste it: spaces, dashes, newlines,
// any case. Returns 64 lower-case hex digits, or nothing if it isn't a key.
std::optional<std::string> NormalizeKey(std::string_view input);

// Keeps only the hex digits of the input, lower-cased (for the 16-box editor).
std::string HexDigitsOnly(std::string_view input);

// Makes a single path component that Windows accepts: replaces <>:"/\|?* and
// control characters with '_', trims trailing dots and spaces, and avoids the
// reserved device names (CON, NUL, COM1...). Never returns an empty string.
std::string SanitizeFileName(std::string_view name);

// "WhatsApp Export Galaxy J6 2026-10-04 23-31"
std::string MakeExportFolderName(std::string_view phoneName, const std::tm& localTime);

std::string FormatBytes(std::uint64_t bytes);      // "6.4 GB"
std::string FormatCount(std::uint64_t n);          // "8,910"
std::string FormatTimeLeft(double seconds);        // "about 12 min left"

std::string ToLowerAscii(std::string_view text);
bool StartsWith(std::string_view text, std::string_view prefix);
bool EndsWith(std::string_view text, std::string_view suffix);
bool EqualsIgnoreCase(std::string_view a, std::string_view b);
std::string Trim(std::string_view text);

// Overwrites the string's buffer before clearing it (used for the key).
void SecureClear(std::string& secret);

}  // namespace ck
