#include "FileSystem.h"

#include <chrono>
#include <cstdlib>
#include <thread>

#include "Strings.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#include <knownfolders.h>
#else
#include <sys/stat.h>
#include <utime.h>
#endif

namespace ck {

std::uint64_t RequiredSpace(std::uint64_t copyBytes) {
    return copyBytes + copyBytes / 10 + 500ull * 1024 * 1024;
}

std::optional<std::uint64_t> FreeSpace(const fs::path& path) {
    std::error_code ec;
    fs::path p = path;
    while (!p.empty() && !fs::exists(p, ec)) {
        fs::path parent = p.parent_path();
        if (parent == p) break;
        p = parent;
    }
    auto info = fs::space(p.empty() ? fs::current_path(ec) : p, ec);
    if (ec) return std::nullopt;
    return info.available;
}

std::vector<fs::path> OneDriveRoots() {
    std::vector<fs::path> roots;
#ifdef _WIN32
    for (const wchar_t* var : {L"OneDrive", L"OneDriveConsumer", L"OneDriveCommercial"}) {
        wchar_t buf[MAX_PATH * 2];
        DWORD n = GetEnvironmentVariableW(var, buf, static_cast<DWORD>(std::size(buf)));
        if (n > 0 && n < std::size(buf)) roots.emplace_back(std::wstring(buf, n));
    }
    // Every signed-in account also has its folder under HKCU\...\OneDrive\Accounts\*\UserFolder.
    HKEY accounts;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\OneDrive\\Accounts", 0, KEY_READ, &accounts) ==
        ERROR_SUCCESS) {
        wchar_t name[256];
        for (DWORD i = 0;; ++i) {
            DWORD len = static_cast<DWORD>(std::size(name));
            if (RegEnumKeyExW(accounts, i, name, &len, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
            wchar_t folder[MAX_PATH * 2];
            DWORD size = sizeof folder;
            if (RegGetValueW(accounts, name, L"UserFolder", RRF_RT_REG_SZ, nullptr, folder, &size) == ERROR_SUCCESS)
                roots.emplace_back(folder);
        }
        RegCloseKey(accounts);
    }
#endif
    return roots;
}

bool IsInside(const fs::path& path, const fs::path& root) {
    if (root.empty()) return false;
    std::error_code ec;
    fs::path a = fs::weakly_canonical(path, ec);
    if (ec) a = path.lexically_normal();
    fs::path b = fs::weakly_canonical(root, ec);
    if (ec) b = root.lexically_normal();
    auto ai = a.begin();
    for (auto bi = b.begin(); bi != b.end(); ++bi, ++ai) {
        if (bi->empty()) continue;  // trailing separator
        if (ai == a.end()) return false;
#ifdef _WIN32
        if (!EqualsIgnoreCase(PathToUtf8(*ai), PathToUtf8(*bi))) return false;
#else
        if (*ai != *bi) return false;
#endif
    }
    return true;
}

bool IsInsideAny(const fs::path& path, const std::vector<fs::path>& roots) {
    for (const auto& r : roots)
        if (IsInside(path, r)) return true;
    return false;
}

fs::path DesktopFolder() {
#ifdef _WIN32
    PWSTR raw = nullptr;
    fs::path result;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop, KF_FLAG_DEFAULT, nullptr, &raw))) result = raw;
    CoTaskMemFree(raw);
    return result;
#else
    const char* home = std::getenv("HOME");
    return home ? fs::path(home) / "Desktop" : fs::current_path();
#endif
}

fs::path LongPath(const fs::path& path) {
#ifdef _WIN32
    const std::wstring& s = path.native();
    if (s.rfind(LR"(\\?\)", 0) == 0 || !path.is_absolute()) return path;
    if (s.rfind(LR"(\\)", 0) == 0) return fs::path(LR"(\\?\UNC\)" + s.substr(2));
    return fs::path(LR"(\\?\)" + fs::path(s).make_preferred().native());
#else
    return path;
#endif
}

void SetModifiedTime(const fs::path& path, std::int64_t unixSeconds) {
    if (unixSeconds <= 0) return;
#ifdef _WIN32
    HANDLE h = CreateFileW(LongPath(path).c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    ULARGE_INTEGER t;
    t.QuadPart = static_cast<ULONGLONG>(unixSeconds) * 10000000ull + 116444736000000000ull;
    FILETIME ft{t.LowPart, t.HighPart};
    SetFileTime(h, nullptr, nullptr, &ft);
    CloseHandle(h);
#else
    struct utimbuf times{static_cast<time_t>(unixSeconds), static_cast<time_t>(unixSeconds)};
    utime(path.c_str(), &times);
#endif
}

std::int64_t ModifiedTime(const fs::path& path) {
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExW(LongPath(path).c_str(), GetFileExInfoStandard, &data)) return 0;
    ULARGE_INTEGER t;
    t.LowPart = data.ftLastWriteTime.dwLowDateTime;
    t.HighPart = data.ftLastWriteTime.dwHighDateTime;
    return static_cast<std::int64_t>((t.QuadPart - 116444736000000000ull) / 10000000ull);
#else
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return 0;
    return st.st_mtime;
#endif
}

std::uint64_t FolderSize(const fs::path& dir) {
    std::uint64_t total = 0;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(LongPath(dir), fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (it->is_regular_file(e2)) total += it->file_size(e2);
    }
    return total;
}

std::optional<std::string> RenameWithRetry(const fs::path& from, const fs::path& to, int attempts) {
    std::error_code ec;
    for (int i = 0; i < attempts; ++i) {
        fs::rename(LongPath(from), LongPath(to), ec);
        if (!ec) return std::nullopt;
        std::this_thread::sleep_for(std::chrono::milliseconds(250 * (i + 1)));
    }
    return ec.message();
}

KeepAwake::KeepAwake() {
#ifdef _WIN32
    SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED);
#endif
}

KeepAwake::~KeepAwake() {
#ifdef _WIN32
    SetThreadExecutionState(ES_CONTINUOUS);
#endif
}

std::string UserRegionCode() {
#ifdef _WIN32
    GEOID geo = GetUserGeoID(GEOCLASS_NATION);
    wchar_t iso[8] = {};
    if (geo != GEOID_NOT_AVAILABLE && GetGeoInfoW(geo, GEO_ISO2, iso, 8, 0) > 0) return ToUtf8(iso);
    wchar_t locale[LOCALE_NAME_MAX_LENGTH] = {};
    if (GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH) > 0) {
        std::string name = ToUtf8(locale);  // "fr-LB"
        auto dash = name.rfind('-');
        if (dash != std::string::npos && name.size() - dash == 3) return name.substr(dash + 1);
    }
    return "";
#else
    const char* lang = std::getenv("LANG");  // "en_US.UTF-8"
    if (!lang) return "";
    std::string s = lang;
    auto u = s.find('_');
    return u != std::string::npos && s.size() >= u + 3 ? s.substr(u + 1, 2) : "";
#endif
}

}  // namespace ck
