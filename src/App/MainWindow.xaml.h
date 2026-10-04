#pragma once

#include "MainWindow.g.h"

#include "Core/Copy/Copier.h"
#include "Core/Copy/CopyPlan.h"
#include "Core/Device/IDeviceSource.h"
#include "Core/Device/WhatsAppLocator.h"
#include "Core/Device/WpdDeviceSource.h"
#include "Core/Pipeline/BuildArchive.h"
#include "Core/Util/Cancel.h"
#include "Core/Util/Log.h"

namespace winrt::ChatKeeper::implementation
{
    enum class Step { Welcome = 0, KeyGuide, Phone, Copy, Contacts, Key, Build, Done };

    // Everything the wizard has learned so far. Touched only on the UI thread,
    // except where a background job holds its own copy.
    struct WizardState
    {
        // Where the data comes from (a phone over WPD, or a folder on this PC).
        std::shared_ptr<ck::IDeviceSource> source;
        bool fromFolder = false;
        std::vector<ck::WhatsAppFolder> folders;  // WhatsApp and/or WhatsApp Business
        std::optional<ck::WhatsAppFolder> folder;
        ck::BackupInfo backup;
        bool staleAccepted = false;
        std::string phoneName;

        // Where it goes.
        std::filesystem::path saveLocation;
        std::filesystem::path exportDir;
        bool copied = false;

        // Contacts.
        std::optional<std::filesystem::path> vcf;
        std::string countryCode;

        std::shared_ptr<ck::Logger> log = std::make_shared<ck::Logger>();
        ck::BuildResult result;
    };

    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow();

        // Navigation (XAML handlers)
        void OnBack(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnNext(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnCancel(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);

        // Step 0
        void OnStart(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        winrt::fire_and_forget OnAlreadyCopied(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);

        // Step 2
        void OnPhoneYes(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnPhoneNo(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnPhoneListSelection(IInspectable const&, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
        void OnPhoneUseSelected(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnPhoneAppChoice(IInspectable const&, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
        void OnGoToStep1(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnStaleContinue(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);

        // Step 3
        winrt::fire_and_forget OnChangeLocation(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        winrt::fire_and_forget OnStartCopy(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);

        // Step 4
        void OnContactsTab(Microsoft::UI::Xaml::Controls::SelectorBar const&,
                           Microsoft::UI::Xaml::Controls::SelectorBarSelectionChangedEventArgs const&);
        winrt::fire_and_forget OnChooseVcf(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnSkipContacts(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        winrt::fire_and_forget OnFindVcfOnPhone(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        winrt::fire_and_forget OnPhoneVcfSelected(IInspectable const&,
                                                  Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);

        // Step 5
        void OnKeyShowToggle(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        winrt::fire_and_forget OnKeyPasteButton(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnKeyClear(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);

        // Step 6 / 7
        void OnBuildRetry(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnOpenInBrowser(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OnOpenFolder(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);

    private:
        // MainWindow.xaml.cpp: shell, navigation, helpers
        void SetUpWindow();
        void BuildStepIndicator();
        void GoTo(Step step);
        void UpdateNavigation();
        bool CanGoNext() const;
        void SetBusy(bool busy);
        HWND WindowHandle();
        winrt::Windows::Foundation::IAsyncOperation<winrt::Windows::Storage::StorageFolder> PickFolder();
        winrt::Windows::Foundation::IAsyncOperation<bool> AskAsync(winrt::hstring title, winrt::hstring body,
                                                                  winrt::hstring primary, winrt::hstring close);
        void OnClosing(Microsoft::UI::Windowing::AppWindow const&, Microsoft::UI::Windowing::AppWindowClosingEventArgs const&);
        // Runs `fn` on the UI thread (from any thread).
        void Ui(std::function<void()> fn);

        // MainWindow.Phone.cpp
        void EnterPhoneStep();
        winrt::fire_and_forget WatchForPhones();
        void ShowDevices(std::vector<ck::WpdDeviceInfo> const& devices);
        winrt::fire_and_forget UsePhone(ck::WpdDeviceInfo info);
        winrt::fire_and_forget UseFolder(std::filesystem::path folder);
        Windows::Foundation::IAsyncAction ShowFolderResult();
        Windows::Foundation::IAsyncAction SelectWhatsAppFolder(size_t index);
        void ResetPhoneStep();

        // MainWindow.Copy.cpp
        void EnterCopyStep();
        void UpdateCopyFolderText();
        std::filesystem::path PlannedExportDir();
        void ShowCopyProgress(ck::CopyProgress const& p);

        // MainWindow.Contacts.cpp
        void EnterContactsStep();
        void SetVcf(std::filesystem::path const& file, std::wstring const& shownName);
        std::string CountryCodeFromBox();

        // MainWindow.Key.cpp
        void BuildKeyBoxes();
        void OnKeyBoxChanged(int index);
        void FillKey(std::string_view text, int startBox);
        std::string KeyDigits() const;
        void UpdateKeyStatus();
        void ClearKey();

        // MainWindow.Build.cpp
        void EnterBuildStep();
        winrt::fire_and_forget RunBuild();
        void SetBuildStage(ck::BuildStage stage);
        void ShowBuildProgress(ck::BuildProgress const& p);
        winrt::fire_and_forget EnterDoneStep();

        Step m_step = Step::Welcome;
        WizardState m_state;
        bool m_busy = false;
        bool m_closeRequested = false;
        ck::CancelToken m_cancel;
        std::atomic<int> m_watchGeneration{ 0 };
        std::vector<ck::WpdDeviceInfo> m_devices;
        std::optional<ck::WpdDeviceInfo> m_rejectedDevice;
        bool m_phoneConfirmed = false;
        bool m_folderChoiceUpdating = false;
        std::vector<Microsoft::UI::Xaml::Controls::PasswordBox> m_keyBoxes;
        bool m_keyUpdating = false;
        bool m_keyHidden = false;
        std::vector<ck::DeviceEntry> m_phoneVcfs;
    };
}

namespace winrt::ChatKeeper::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}

// Localized strings.
winrt::hstring S(std::wstring_view key);
// S(key) with {0}, {1}, ... replaced.
winrt::hstring SF(std::wstring_view key, std::initializer_list<std::wstring> args);
std::wstring W(std::string_view utf8);
