#pragma once
// App identity in one place. The name is a placeholder: change it here, in
// tools/make_resw.py (AppName) and in installer/ChatKeeper.iss.

#include <filesystem>
#include <string>

namespace ChatKeeper
{
    inline constexpr wchar_t kAppName[] = L"ChatKeeper";
    inline constexpr wchar_t kAppVersion[] = L"0.1.0";

    // Folder holding ChatKeeper.exe.
    std::filesystem::path AppFolder();

    // exporter\wtsexporter.exe and viewer\index.html, shipped next to the exe.
    std::filesystem::path ExporterPath();
    std::filesystem::path ViewerPath();

    // %LOCALAPPDATA%\ChatKeeper
    std::filesystem::path LocalDataFolder();
}
