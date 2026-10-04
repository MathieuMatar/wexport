#include "Strings.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace ck {

namespace {

void AppendUtf8(std::string& out, char32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

void AppendWide(std::wstring& out, char32_t cp) {
    if constexpr (sizeof(wchar_t) == 2) {
        if (cp >= 0x10000) {
            cp -= 0x10000;
            out += static_cast<wchar_t>(0xD800 + (cp >> 10));
            out += static_cast<wchar_t>(0xDC00 + (cp & 0x3FF));
            return;
        }
    }
    out += static_cast<wchar_t>(cp);
}

bool IsHex(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

}  // namespace

std::string ToUtf8(std::wstring_view text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        char32_t cp = static_cast<char32_t>(text[i]);
        if constexpr (sizeof(wchar_t) == 2) {
            if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < text.size()) {
                char32_t lo = static_cast<char32_t>(text[i + 1]);
                if (lo >= 0xDC00 && lo <= 0xDFFF) {
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    ++i;
                }
            }
            if (cp >= 0xD800 && cp <= 0xDFFF) cp = 0xFFFD;
        }
        AppendUtf8(out, cp);
    }
    return out;
}

std::wstring ToWide(std::string_view s) {
    std::wstring out;
    out.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        char32_t cp = 0xFFFD;
        size_t len = 1;
        if (c < 0x80) {
            cp = c;
        } else if ((c >> 5) == 0x6) {
            len = 2;
        } else if ((c >> 4) == 0xE) {
            len = 3;
        } else if ((c >> 3) == 0x1E) {
            len = 4;
        }
        if (len > 1) {
            if (i + len > s.size()) {
                len = 1;
            } else {
                cp = c & (0x7F >> len);
                bool ok = true;
                for (size_t k = 1; k < len; ++k) {
                    unsigned char cc = static_cast<unsigned char>(s[i + k]);
                    if ((cc >> 6) != 0x2) { ok = false; break; }
                    cp = (cp << 6) | (cc & 0x3F);
                }
                if (!ok) { cp = 0xFFFD; len = 1; }
            }
        }
        AppendWide(out, cp);
        i += len;
    }
    return out;
}

std::string PathToUtf8(const std::filesystem::path& path) {
#ifdef _WIN32
    return ToUtf8(path.native());
#else
    return path.native();
#endif
}

std::filesystem::path PathFromUtf8(std::string_view utf8) {
#ifdef _WIN32
    return std::filesystem::path(ToWide(utf8));
#else
    return std::filesystem::path(std::string(utf8));
#endif
}

std::optional<std::string> NormalizeKey(std::string_view input) {
    std::string key;
    for (char c : input) {
        if (IsHex(c)) {
            key += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        } else if (c == ' ' || c == '-' || c == '\r' || c == '\n' || c == '\t') {
            continue;
        } else {
            return std::nullopt;
        }
    }
    if (key.size() != 64) return std::nullopt;
    return key;
}

std::string HexDigitsOnly(std::string_view input) {
    std::string out;
    for (char c : input)
        if (IsHex(c)) out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::string SanitizeFileName(std::string_view name) {
    std::string out;
    out.reserve(name.size());
    for (char c : name) {
        unsigned char u = static_cast<unsigned char>(c);
        if (u < 0x20 || c == '<' || c == '>' || c == ':' || c == '"' || c == '/' || c == '\\' ||
            c == '|' || c == '?' || c == '*') {
            out += '_';
        } else {
            out += c;
        }
    }
    while (!out.empty() && (out.back() == '.' || out.back() == ' ')) out.pop_back();
    if (out.empty()) return "_";

    static constexpr std::array<std::string_view, 22> kReserved = {
        "CON",  "PRN",  "AUX",  "NUL",  "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7",
        "COM8", "COM9", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};
    std::string stem = out.substr(0, out.find('.'));
    for (auto r : kReserved)
        if (EqualsIgnoreCase(Trim(stem), r)) return "_" + out;
    return out;
}

std::string MakeExportFolderName(std::string_view phoneName, const std::tm& t) {
    char stamp[64];
    std::snprintf(stamp, sizeof stamp, "%04d-%02d-%02d %02d-%02d", t.tm_year + 1900, t.tm_mon + 1,
                  t.tm_mday, t.tm_hour, t.tm_min);
    std::string name = "WhatsApp Export";
    std::string phone = Trim(phoneName);
    if (!phone.empty()) name += " " + phone;
    name += " ";
    name += stamp;
    return SanitizeFileName(name);
}

std::string FormatBytes(std::uint64_t bytes) {
    static const char* kUnits[] = {"bytes", "KB", "MB", "GB", "TB"};
    if (bytes < 1024) return std::to_string(bytes) + (bytes == 1 ? " byte" : " bytes");
    double v = static_cast<double>(bytes);
    int unit = 0;
    while (v >= 1024.0 && unit < 4) { v /= 1024.0; ++unit; }
    char buf[32];
    std::snprintf(buf, sizeof buf, v < 10 ? "%.1f %s" : "%.0f %s", v, kUnits[unit]);
    return buf;
}

std::string FormatCount(std::uint64_t n) {
    std::string digits = std::to_string(n);
    std::string out;
    int count = 0;
    for (auto it = digits.rbegin(); it != digits.rend(); ++it) {
        if (count && count % 3 == 0) out += ',';
        out += *it;
        ++count;
    }
    std::reverse(out.begin(), out.end());
    return out;
}

std::string FormatTimeLeft(double seconds) {
    if (!(seconds >= 0) || std::isinf(seconds)) return "";
    if (seconds < 60) return "less than a minute left";
    double minutes = std::ceil(seconds / 60.0);
    if (minutes < 60) return "about " + std::to_string(static_cast<int>(minutes)) + " min left";
    int h = static_cast<int>(minutes / 60);
    int m = static_cast<int>(minutes) % 60;
    std::string out = "about " + std::to_string(h) + " h";
    if (m) out += " " + std::to_string(m) + " min";
    return out + " left";
}

std::string ToLowerAscii(std::string_view text) {
    std::string out(text);
    for (char& c : out)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}

bool StartsWith(std::string_view text, std::string_view prefix) {
    return text.substr(0, prefix.size()) == prefix;
}

bool EndsWith(std::string_view text, std::string_view suffix) {
    return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
}

bool EqualsIgnoreCase(std::string_view a, std::string_view b) {
    return a.size() == b.size() && ToLowerAscii(a) == ToLowerAscii(b);
}

std::string Trim(std::string_view text) {
    size_t b = 0, e = text.size();
    while (b < e && std::isspace(static_cast<unsigned char>(text[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(text[e - 1]))) --e;
    return std::string(text.substr(b, e - b));
}

void SecureClear(std::string& secret) {
    volatile char* p = secret.data();
    for (size_t i = 0; i < secret.size(); ++i) p[i] = 0;
    secret.clear();
    secret.shrink_to_fit();
}

}  // namespace ck
