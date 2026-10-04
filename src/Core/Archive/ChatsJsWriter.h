#pragma once
// chats.js = "window.CHATS_JSON = " + chats.json (byte for byte, except raw
// U+2028/U+2029 escaped) + ";\n"  (§6.1). Streams; never builds a DOM.

#include <cstdint>
#include <filesystem>
#include <string>

#include "../Util/Cancel.h"

namespace ck {

inline constexpr const char* kChatsJsPrefix = "window.CHATS_JSON = ";
inline constexpr const char* kChatsJsSuffix = ";\n";

struct ArchiveStats {
    std::uint64_t chats = 0;     // chats with at least one message (excluding the call log)
    std::uint64_t groups = 0;
    std::uint64_t messages = 0;  // excluding calls
    std::uint64_t photos = 0, videos = 0, audio = 0, documents = 0, stickers = 0;
    std::uint64_t calls = 0;
    std::uint64_t missingMedia = 0;
    std::uint64_t mediaPaths = 0;               // media messages with a path
    std::uint64_t mediaPathsUnderMediaDir = 0;  // ... containing "WhatsApp/Media/"
};

struct ChatsJsResult {
    bool ok = false;
    std::string error;
    ArchiveStats stats;
    std::uint64_t jsonBytes = 0;
    std::uint64_t jsBytes = 0;
    std::uint64_t escapedSeparators = 0;
};

// Validates the JSON (SAX pass) and gathers the summary numbers.
bool ScanChatsJson(const std::filesystem::path& json, ArchiveStats& stats, std::string& error,
                   const CancelToken& cancel);

// Writes chats.js via a ".partial" file and renames it into place, then checks
// the size. Validates the JSON first.
ChatsJsResult WriteChatsJs(const std::filesystem::path& json, const std::filesystem::path& js,
                           const CancelToken& cancel);

// The streaming escape, exposed for tests. `carry` holds a partial UTF-8
// sequence between calls; call with final=true at the end.
std::string EscapeLineSeparators(std::string_view chunk, std::string& carry, bool final,
                                 std::uint64_t* escapedCount = nullptr);

}  // namespace ck
