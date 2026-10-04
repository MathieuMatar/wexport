#include "pch.h"

#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

#include "AppInfo.h"
#include "Core/Util/FileSystem.h"
#include "Core/Util/Strings.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
namespace fs = std::filesystem;

hstring S(std::wstring_view key)
{
    static Microsoft::Windows::ApplicationModel::Resources::ResourceLoader loader;
    try
    {
        return loader.GetString(key);
    }
    catch (...)
    {
        return hstring(key);
    }
}

hstring SF(std::wstring_view key, std::initializer_list<std::wstring> args)
{
    std::wstring text(S(key));
    size_t i = 0;
    for (const auto& a : args)
    {
        std::wstring token = L"{" + std::to_wstring(i++) + L"}";
        for (size_t pos = text.find(token); pos != std::wstring::npos; pos = text.find(token, pos + a.size()))
            text.replace(pos, token.size(), a);
    }
    return hstring(text);
}

std::wstring W(std::string_view utf8) { return ck::ToWide(utf8); }

namespace winrt::ChatKeeper::implementation
{
    namespace
    {
        constexpr std::array<const wchar_t*, 7> kStepNames = { L"Step_Key", L"Step_Phone", L"Step_Copy", L"Step_Contacts",
                                                              L"Step_Unlock", L"Step_Build", L"Step_Done" };
    }

    MainWindow::MainWindow()
    {
        InitializeComponent();
        SetUpWindow();
        BuildStepIndicator();
        BuildKeyBoxes();
        m_state.saveLocation = ck::DesktopFolder();
        GoTo(Step::Welcome);
    }

    void MainWindow::SetUpWindow()
    {
        Title(::ChatKeeper::kAppName);
        AppTitleText().Text(::ChatKeeper::kAppName);
        ExtendsContentIntoTitleBar(true);
        SetTitleBar(AppTitleBar());

        auto appWindow = AppWindow();
        appWindow.Resize({ 1180, 860 });
        appWindow.SetIcon(L"Assets\\AppIcon.ico");
        appWindow.Closing({ this, &MainWindow::OnClosing });

        // Arabic (and other RTL languages) flip the whole layout.
        if (S(L"FlowDirection") == L"RightToLeft") Root().FlowDirection(FlowDirection::RightToLeft);
    }

    HWND MainWindow::WindowHandle()
    {
        HWND hwnd{};
        this->try_as<::IWindowNative>()->get_WindowHandle(&hwnd);
        return hwnd;
    }

    void MainWindow::Ui(std::function<void()> fn)
    {
        DispatcherQueue().TryEnqueue([strong = get_strong(), fn = std::move(fn)]() { fn(); });
    }

    void MainWindow::BuildStepIndicator()
    {
        auto panel = StepIndicator();
        panel.Children().Clear();
        for (size_t i = 0; i < kStepNames.size(); ++i)
        {
            if (i > 0)
            {
                TextBlock dot;
                dot.Text(L"·");
                dot.VerticalAlignment(VerticalAlignment::Center);
                dot.Margin({ 4, 0, 4, 0 });
                dot.Foreground(Application::Current().Resources().Lookup(box_value(L"TextFillColorTertiaryBrush"))
                                   .as<Media::Brush>());
                panel.Children().Append(dot);
            }
            TextBlock item;
            item.Text(std::to_wstring(i + 1) + L" " + std::wstring(S(kStepNames[i])));
            item.VerticalAlignment(VerticalAlignment::Center);
            panel.Children().Append(item);
        }
    }

    void MainWindow::GoTo(Step step)
    {
        Step previous = m_step;
        m_step = step;
        WelcomePanel().Visibility(step == Step::Welcome ? Visibility::Visible : Visibility::Collapsed);
        KeyGuidePanel().Visibility(step == Step::KeyGuide ? Visibility::Visible : Visibility::Collapsed);
        PhonePanel().Visibility(step == Step::Phone ? Visibility::Visible : Visibility::Collapsed);
        CopyPanel().Visibility(step == Step::Copy ? Visibility::Visible : Visibility::Collapsed);
        ContactsPanel().Visibility(step == Step::Contacts ? Visibility::Visible : Visibility::Collapsed);
        KeyPanel().Visibility(step == Step::Key ? Visibility::Visible : Visibility::Collapsed);
        BuildPanel().Visibility(step == Step::Build ? Visibility::Visible : Visibility::Collapsed);
        DonePanel().Visibility(step == Step::Done ? Visibility::Visible : Visibility::Collapsed);

        if (previous == Step::Phone && step != Step::Phone) ++m_watchGeneration;  // stop polling
        switch (step)
        {
        case Step::Phone: EnterPhoneStep(); break;
        case Step::Copy: EnterCopyStep(); break;
        case Step::Contacts: EnterContactsStep(); break;
        case Step::Key: UpdateKeyStatus(); break;
        case Step::Build: EnterBuildStep(); break;
        case Step::Done: EnterDoneStep(); break;
        default: break;
        }

        // Highlight the current step in the indicator.
        auto children = StepIndicator().Children();
        int current = static_cast<int>(step) - 1;  // Welcome has no number
        for (uint32_t i = 0, n = 0; i < children.Size(); ++i)
        {
            auto tb = children.GetAt(i).try_as<TextBlock>();
            if (!tb || tb.Text() == L"·") continue;
            bool active = static_cast<int>(n) == current;
            bool done = static_cast<int>(n) < current;
            tb.FontWeight(active ? Microsoft::UI::Text::FontWeights::SemiBold() : Microsoft::UI::Text::FontWeights::Normal());
            tb.Opacity(active ? 1.0 : (done ? 0.85 : 0.55));
            ++n;
        }
        UpdateNavigation();
    }

    bool MainWindow::CanGoNext() const
    {
        switch (m_step)
        {
        case Step::Welcome: return true;
        case Step::KeyGuide: return true;
        case Step::Phone:
            return m_state.folder.has_value() && m_state.backup.messages.has_value() &&
                   (!ck::IsBackupStale(m_state.backup.messages->modified, std::time(nullptr)) || m_state.staleAccepted);
        case Step::Copy: return m_state.copied;
        case Step::Contacts: return true;
        case Step::Key: return ck::NormalizeKey(KeyDigits()).has_value();
        case Step::Build: return m_state.result.status == ck::BuildResult::Status::Ok;
        case Step::Done: return true;
        }
        return false;
    }

    void MainWindow::UpdateNavigation()
    {
        BackButton().Visibility(m_step == Step::Welcome || m_busy ? Visibility::Collapsed : Visibility::Visible);
        BackButton().IsEnabled(!m_busy && m_step != Step::Done);
        CancelButton().Visibility(m_busy ? Visibility::Visible : Visibility::Collapsed);
        NextButton().Visibility(m_step == Step::Welcome || m_busy ? Visibility::Collapsed : Visibility::Visible);
        NextButton().IsEnabled(!m_busy && CanGoNext());
        hstring label = S(L"Next");
        if (m_step == Step::KeyGuide) label = S(L"Next_MadeBackup");
        else if (m_step == Step::Key) label = S(L"Next_Unlock");
        else if (m_step == Step::Done) label = S(L"Next_Finish");
        else if (m_step == Step::Contacts && !m_state.vcf) label = S(L"Next_Skip");
        NextButton().Content(box_value(label));
    }

    void MainWindow::SetBusy(bool busy)
    {
        m_busy = busy;
        UpdateNavigation();
    }

    void MainWindow::OnBack(IInspectable const&, RoutedEventArgs const&)
    {
        if (m_busy) return;
        switch (m_step)
        {
        case Step::KeyGuide: GoTo(Step::Welcome); break;
        case Step::Phone: GoTo(Step::KeyGuide); break;
        case Step::Copy: GoTo(m_state.fromFolder ? Step::Welcome : Step::Phone); break;
        case Step::Contacts: GoTo(Step::Copy); break;
        case Step::Key: GoTo(Step::Contacts); break;
        case Step::Build: GoTo(Step::Key); break;
        default: break;
        }
    }

    void MainWindow::OnNext(IInspectable const&, RoutedEventArgs const&)
    {
        if (m_busy || !CanGoNext()) return;
        switch (m_step)
        {
        case Step::KeyGuide: GoTo(Step::Phone); break;
        case Step::Phone: GoTo(Step::Copy); break;
        case Step::Copy: GoTo(Step::Contacts); break;
        case Step::Contacts:
            m_state.countryCode = CountryCodeFromBox();
            GoTo(Step::Key);
            break;
        case Step::Key: GoTo(Step::Build); break;
        case Step::Build: GoTo(Step::Done); break;
        case Step::Done: Close(); break;
        default: break;
        }
    }

    void MainWindow::OnCancel(IInspectable const&, RoutedEventArgs const&)
    {
        m_cancel.Cancel();
        CancelButton().IsEnabled(false);
    }

    void MainWindow::OnStart(IInspectable const&, RoutedEventArgs const&)
    {
        m_state.fromFolder = false;
        GoTo(Step::KeyGuide);
    }

    winrt::fire_and_forget MainWindow::OnAlreadyCopied(IInspectable const&, RoutedEventArgs const&)
    {
        auto strong = get_strong();
        WelcomeError().IsOpen(false);
        auto folder = co_await PickFolder();
        if (!folder) co_return;
        UseFolder(fs::path(std::wstring(folder.Path())));
    }

    Windows::Foundation::IAsyncOperation<Windows::Storage::StorageFolder> MainWindow::PickFolder()
    {
        Windows::Storage::Pickers::FolderPicker picker;
        picker.as<::IInitializeWithWindow>()->Initialize(WindowHandle());
        picker.SuggestedStartLocation(Windows::Storage::Pickers::PickerLocationId::Desktop);
        picker.FileTypeFilter().Append(L"*");
        co_return co_await picker.PickSingleFolderAsync();
    }

    Windows::Foundation::IAsyncOperation<bool> MainWindow::AskAsync(hstring title, hstring body, hstring primary,
                                                                   hstring close)
    {
        ContentDialog dialog;
        dialog.XamlRoot(Root().XamlRoot());
        dialog.FlowDirection(Root().FlowDirection());
        dialog.Title(box_value(title));
        TextBlock text;
        text.Text(body);
        text.TextWrapping(TextWrapping::Wrap);
        dialog.Content(text);
        dialog.PrimaryButtonText(primary);
        dialog.CloseButtonText(close);
        dialog.DefaultButton(ContentDialogButton::Close);
        auto result = co_await dialog.ShowAsync();
        co_return result == ContentDialogResult::Primary;
    }

    void MainWindow::OnClosing(Microsoft::UI::Windowing::AppWindow const&,
                               Microsoft::UI::Windowing::AppWindowClosingEventArgs const& args)
    {
        if (!m_busy || m_closeRequested) return;
        args.Cancel(true);
        [](com_ptr<MainWindow> self) -> fire_and_forget
        {
            bool stop = co_await self->AskAsync(S(L"Leave_Title"), S(L"Leave_Body"), S(L"Leave_Stop"), S(L"Dialog_Cancel"));
            if (!stop) co_return;
            self->m_closeRequested = true;
            self->m_cancel.Cancel();
            // Give the copy/exporter a moment to stop and clean up its temp files.
            for (int i = 0; i < 100 && self->m_busy; ++i)
                co_await winrt::resume_after(std::chrono::milliseconds(100));
            co_await wil::resume_foreground(self->DispatcherQueue());
            self->Close();
        }(get_strong());
    }
}
