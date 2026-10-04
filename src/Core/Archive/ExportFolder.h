#pragma once
// Moving media into place for the viewer (§6.2) and cleaning up (§8).

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "../Util/Log.h"

namespace ck {

inline constexpr const char* kViewerMediaDir = "media";
inline constexpr const char* kChatsJs = "chats.js";
inline constexpr const char* kMembersJs = "members.js";
inline constexpr const char* kIndexHtml = "index.html";
inline constexpr const char* kLogFile = "export-log.txt";
inline constexpr const char* kReadmeFile = "README.txt";
inline constexpr const char* kRenamedFilesList = "renamed-files.txt";

// Before copying or running the exporter: if an earlier run already moved the
// media to media\, move it back to WhatsApp\Media so the exporter finds it.
std::optional<std::string> EnsureMediaForExporter(const std::filesystem::path& exportDir, Logger* log);

// After the exporter: WhatsApp\Media -> media (same-drive rename). The
// exporter's own extra folders (WhatsApp\thumbnails, WhatsApp\vCards) go to
// data\, and the then-empty WhatsApp\ folder is removed.
std::optional<std::string> MoveMediaForViewer(const std::filesystem::path& exportDir, Logger* log);

// Copies the supplied viewer unchanged.
std::optional<std::string> CopyViewer(const std::filesystem::path& viewerHtml, const std::filesystem::path& exportDir);

void WriteReadme(const std::filesystem::path& exportDir);

// After success: removes _work\, a leftover WhatsApp\ folder if empty, and
// every *.partial file. Touches nothing else.
void CleanupAfterSuccess(const std::filesystem::path& exportDir, Logger* log);

// After failure or cancel: keeps the copied data (_work\*.crypt15, the media
// in either location) and removes partial outputs.
void CleanupAfterFailure(const std::filesystem::path& exportDir, Logger* log);

// Lists the paths CleanupAfterSuccess would delete (for tests and the log).
std::vector<std::filesystem::path> PathsRemovedOnSuccess(const std::filesystem::path& exportDir);

}  // namespace ck
