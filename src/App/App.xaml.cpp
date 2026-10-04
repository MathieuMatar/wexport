#include "pch.h"

#include "App.xaml.h"
#include "AppInfo.h"
#include "MainWindow.xaml.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace winrt::ChatKeeper::implementation
{
    App::App()
    {
        // WebView2 keeps its profile next to the exe by default, which isn't
        // writable under Program Files. Keep it in the user's local app data,
        // and turn off the browser's own background network traffic.
        std::filesystem::path data = ::ChatKeeper::LocalDataFolder() / L"WebView2";
        std::error_code ec;
        std::filesystem::create_directories(data, ec);
        SetEnvironmentVariableW(L"WEBVIEW2_USER_DATA_FOLDER", data.c_str());
        SetEnvironmentVariableW(L"WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS",
                                L"--disable-background-networking --disable-component-update "
                                L"--disable-domain-reliability --no-pings --disable-sync");

#if defined _DEBUG && !defined DISABLE_XAML_GENERATED_BREAK_ON_UNHANDLED_EXCEPTION
        UnhandledException([](IInspectable const&, UnhandledExceptionEventArgs const& e)
        {
            if (IsDebuggerPresent())
            {
                auto errorMessage = e.Message();
                __debugbreak();
            }
        });
#endif
    }

    void App::OnLaunched([[maybe_unused]] LaunchActivatedEventArgs const& e)
    {
        window = make<MainWindow>();
        window.Activate();
    }
}
