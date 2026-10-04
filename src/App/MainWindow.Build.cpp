// Step 6 (unlock and build) and step 7 (done: summary and preview).

#include "pch.h"

#include "MainWindow.xaml.h"

#include "AppInfo.h"
#include "Core/Archive/ExportFolder.h"
#include "Core/Util/Strings.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
namespace fs = std::filesystem;

namespace winrt::ChatKeeper::implementation
{
    namespace
    {
        constexpr std::array<const wchar_t*, 4> kStageNames = { L"Build_Stage1", L"Build_Stage2", L"Build_Stage3",
                                                               L"Build_Stage4" };
        constexpr wchar_t kVirtualHost[] = L"archive.local";
    }

    void MainWindow::EnterBuildStep()
    {
        auto stages = BuildStages();
        if (stages.Children().Size() == 0)
        {
            for (auto name : kStageNames)
            {
                StackPanel row;
                row.Orientation(Orientation::Horizontal);
                row.Spacing(12);
                Grid icon;
                icon.Width(20);
                icon.Height(20);
                ProgressRing ring;
                ring.Width(18);
                ring.Height(18);
                ring.IsActive(false);
                ring.Visibility(Visibility::Collapsed);
                FontIcon glyph;
                glyph.Glyph(L"");  // empty circle
                glyph.FontSize(16);
                icon.Children().Append(ring);
                icon.Children().Append(glyph);
                TextBlock text;
                text.Text(S(name));
                text.VerticalAlignment(VerticalAlignment::Center);
                row.Children().Append(icon);
                row.Children().Append(text);
                stages.Children().Append(row);
            }
        }
        if (m_state.result.status != ck::BuildResult::Status::Ok) RunBuild();
        else UpdateNavigation();
    }

    void MainWindow::SetBuildStage(ck::BuildStage stage)
    {
        int current = static_cast<int>(stage);
        auto rows = BuildStages().Children();
        for (uint32_t i = 0; i < rows.Size(); ++i)
        {
            auto row = rows.GetAt(i).as<StackPanel>();
            auto icon = row.Children().GetAt(0).as<Grid>();
            auto ring = icon.Children().GetAt(0).as<ProgressRing>();
            auto glyph = icon.Children().GetAt(1).as<FontIcon>();
            auto text = row.Children().GetAt(1).as<TextBlock>();
            bool active = static_cast<int>(i) == current;
            bool done = static_cast<int>(i) < current;
            ring.IsActive(active);
            ring.Visibility(active ? Visibility::Visible : Visibility::Collapsed);
            glyph.Visibility(active ? Visibility::Collapsed : Visibility::Visible);
            glyph.Glyph(done ? L"" : L"");  // check mark / empty circle
            text.FontWeight(active ? Microsoft::UI::Text::FontWeights::SemiBold() : Microsoft::UI::Text::FontWeights::Normal());
            text.Opacity(done || active ? 1.0 : 0.6);
        }
    }

    void MainWindow::ShowBuildProgress(ck::BuildProgress const& p)
    {
        SetBuildStage(p.stage);
        if (p.fraction)
        {
            BuildProgress().IsIndeterminate(false);
            BuildProgress().Value(*p.fraction);
        }
        else
        {
            BuildProgress().IsIndeterminate(true);
        }
        if (!p.detail.empty()) BuildStatus().Text(W(p.detail));
    }

    void MainWindow::OnBuildRetry(IInspectable const&, RoutedEventArgs const&)
    {
        if (m_state.result.status == ck::BuildResult::Status::WrongKey)
        {
            GoTo(Step::Key);
            return;
        }
        RunBuild();
    }

    winrt::fire_and_forget MainWindow::RunBuild()
    {
        auto strong = get_strong();
        auto dispatcher = DispatcherQueue();
        if (m_busy) co_return;
        auto key = ck::NormalizeKey(KeyDigits());
        if (!key || m_state.exportDir.empty())
        {
            GoTo(Step::Key);
            co_return;
        }

        BuildError().IsOpen(false);
        BuildDetails().Visibility(Visibility::Collapsed);
        BuildStatus().Text(L"");
        BuildProgress().Visibility(Visibility::Visible);
        BuildProgress().IsIndeterminate(true);
        SetBuildStage(ck::BuildStage::Unlocking);
        m_cancel = ck::CancelToken();
        SetBusy(true);

        ck::BuildOptions options;
        options.exportDir = m_state.exportDir;
        options.exporterExe = ::ChatKeeper::ExporterPath();
        options.viewerHtml = ::ChatKeeper::ViewerPath();
        options.key = *key;
        ck::SecureClear(*key);
        options.vcf = m_state.vcf;
        options.countryCode = m_state.vcf ? m_state.countryCode : "";
        auto cancel = m_cancel;
        auto log = m_state.log;
        log->Open(m_state.exportDir / ck::kLogFile);

        co_await winrt::resume_background();
        ck::BuildResult result;
        try
        {
            result = ck::RunBuild(std::move(options), cancel,
                                  [this](ck::BuildProgress const& p) { Ui([this, p] { ShowBuildProgress(p); }); }, *log);
        }
        catch (std::exception const& e)
        {
            result.status = ck::BuildResult::Status::BuildFailed;
            result.message = "Something went wrong while building the archive.";
            result.details = e.what();
        }
        co_await wil::resume_foreground(dispatcher);

        m_state.result = result;
        SetBusy(false);
        CancelButton().IsEnabled(true);
        if (result.status == ck::BuildResult::Status::Ok)
        {
            ClearKey();  // no longer needed
            SetBuildStage(ck::BuildStage::Finished);
            BuildProgress().IsIndeterminate(false);
            BuildProgress().Value(1.0);
            GoTo(Step::Done);
            co_return;
        }

        BuildProgress().Visibility(Visibility::Collapsed);
        std::wstring message;
        switch (result.status)
        {
        case ck::BuildResult::Status::WrongKey:
            KeyWrong().IsOpen(true);
            GoTo(Step::Key);  // copied data is kept
            co_return;
        case ck::BuildResult::Status::Cancelled: message = S(L"Build_Cancelled"); break;
        default:
            message = W(result.message) + L" " +
                      std::wstring(SF(L"Build_LogSaved", { (m_state.exportDir / ck::kLogFile).native() }));
            break;
        }
        BuildError().Message(message);
        BuildRetry().Content(box_value(S(L"Build_TryAgain.Content")));
        BuildError().IsOpen(true);
        if (!result.details.empty())
        {
            BuildDetailsText().Text(W(result.details));
            BuildDetails().Visibility(Visibility::Visible);
        }
        UpdateNavigation();
    }

    winrt::fire_and_forget MainWindow::EnterDoneStep()
    {
        auto strong = get_strong();
        const auto& r = m_state.result;
        const auto& s = r.stats;
        auto n = [](std::uint64_t v) { return W(ck::FormatCount(v)); };
        std::wstring summary;
        summary += SF(L"Done_Chats", { n(s.chats), n(s.groups), n(s.messages) }) + L"\n";
        summary += SF(L"Done_Media", { n(s.photos), n(s.videos), n(s.audio), n(s.documents), n(s.stickers) }) + L"\n";
        summary += SF(L"Done_Calls", { n(s.calls) }) + L"\n";
        if (s.missingMedia) summary += SF(L"Done_Missing", { n(s.missingMedia) }) + L"\n";
        summary += SF(L"Done_Size", { W(ck::FormatBytes(r.folderBytes)), m_state.exportDir.native() });
        DoneSummary().Text(summary);
        UpdateNavigation();

        try
        {
            co_await Preview().EnsureCoreWebView2Async();
            auto core = Preview().CoreWebView2();
            core.SetVirtualHostNameToFolderMapping(kVirtualHost, m_state.exportDir.native(),
                                                   Microsoft::Web::WebView2::Core::CoreWebView2HostResourceAccessKind::Allow);
            core.Settings().IsStatusBarEnabled(false);
            core.Settings().AreDefaultContextMenusEnabled(true);
            // Stay inside the archive: anything else opens outside the app.
            core.NavigationStarting([](auto const&, Microsoft::Web::WebView2::Core::CoreWebView2NavigationStartingEventArgs const& e) {
                std::wstring uri(e.Uri());
                if (uri.rfind(L"https://archive.local/", 0) != 0 && uri.rfind(L"about:", 0) != 0 && uri.rfind(L"data:", 0) != 0)
                {
                    e.Cancel(true);
                    ShellExecuteW(nullptr, L"open", uri.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                }
            });
            core.NewWindowRequested([dir = m_state.exportDir](auto const&, Microsoft::Web::WebView2::Core::CoreWebView2NewWindowRequestedEventArgs const& e) {
                e.Handled(true);
                std::wstring uri(e.Uri());
                const std::wstring prefix = L"https://archive.local/";
                if (uri.rfind(prefix, 0) == 0)
                {
                    // Open the media file itself (e.g. a document) with its default app.
                    std::wstring rel = uri.substr(prefix.size());
                    std::wstring decoded(rel.size() + 1, L'\0');
                    DWORD len = static_cast<DWORD>(decoded.size());
                    if (SUCCEEDED(UrlUnescapeW(rel.data(), decoded.data(), &len, 0))) decoded.resize(len);
                    else decoded = rel;
                    fs::path local = (dir / fs::path(decoded).make_preferred()).lexically_normal();
                    if (ck::PathToUtf8(local).rfind(ck::PathToUtf8(dir), 0) == 0)
                        ShellExecuteW(nullptr, L"open", local.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                }
            });
            Preview().Source(Windows::Foundation::Uri(L"https://archive.local/index.html"));
        }
        catch (hresult_error const& e)
        {
            m_state.log->Warn("Preview unavailable: " + ck::ToUtf8(std::wstring(e.message())));
        }
    }

    void MainWindow::OnOpenInBrowser(IInspectable const&, RoutedEventArgs const&)
    {
        auto index = m_state.exportDir / ck::kIndexHtml;
        ShellExecuteW(nullptr, L"open", index.c_str(), nullptr, m_state.exportDir.c_str(), SW_SHOWNORMAL);
    }

    void MainWindow::OnOpenFolder(IInspectable const&, RoutedEventArgs const&)
    {
        ShellExecuteW(nullptr, L"explore", m_state.exportDir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
}
