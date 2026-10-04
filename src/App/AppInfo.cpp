#include "pch.h"

#include "AppInfo.h"

#include <shlobj.h>

namespace ChatKeeper
{
    std::filesystem::path AppFolder()
    {
        std::wstring buf(MAX_PATH, L'\0');
        for (;;)
        {
            DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
            if (n < buf.size())
            {
                buf.resize(n);
                break;
            }
            buf.resize(buf.size() * 2);
        }
        return std::filesystem::path(buf).parent_path();
    }

    std::filesystem::path ExporterPath() { return AppFolder() / L"exporter" / L"wtsexporter.exe"; }
    std::filesystem::path ViewerPath() { return AppFolder() / L"viewer" / L"index.html"; }

    std::filesystem::path LocalDataFolder()
    {
        PWSTR raw = nullptr;
        std::filesystem::path result;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &raw))) result = raw;
        CoTaskMemFree(raw);
        return result / kAppName;
    }
}
