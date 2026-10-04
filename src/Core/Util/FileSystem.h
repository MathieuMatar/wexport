#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ck {

namespace fs = std::filesystem;

// Space needed in the target drive: everything copied + 10% + 500 MB for the
// decrypted databases and the viewer data.
std::uint64_t RequiredSpace(std::uint64_t copyBytes);

// Free bytes for the drive that holds `path` (walks up to an existing parent).
std::optional<std::uint64_t> FreeSpace(const fs::path& path);

// OneDrive-synced roots for the current user (empty off Windows).
std::vector<fs::path> OneDriveRoots();
bool IsInside(const fs::path& path, const fs::path& root);
bool IsInsideAny(const fs::path& path, const std::vector<fs::path>& roots);

// The user's real Desktop folder (FOLDERID_Desktop on Windows, may be redirected).
fs::path DesktopFolder();

// On Windows prefixes absolute paths with \\?\ so paths over MAX_PATH work.
fs::path LongPath(const fs::path& path);

// Sets the file's modification time from Unix seconds. Best effort.
void SetModifiedTime(const fs::path& path, std::int64_t unixSeconds);
std::int64_t ModifiedTime(const fs::path& path);

// Total size of all regular files under `dir`.
std::uint64_t FolderSize(const fs::path& dir);

// Rename with a few retries (antivirus, indexer or an open file can hold a
// handle for a moment). Returns an error message on failure.
std::optional<std::string> RenameWithRetry(const fs::path& from, const fs::path& to, int attempts = 6);

// Keeps the PC (not the display) awake while alive.
class KeepAwake {
public:
    KeepAwake();
    ~KeepAwake();
    KeepAwake(const KeepAwake&) = delete;
    KeepAwake& operator=(const KeepAwake&) = delete;
};

// The two-letter region of the user (e.g. "LB"), from Windows settings.
std::string UserRegionCode();

}  // namespace ck
