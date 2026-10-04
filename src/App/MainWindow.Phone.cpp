// Step 2: connect the phone, confirm it, find WhatsApp and check the backup.
// Also the "I already copied the WhatsApp folder" path (a folder source).

#include "pch.h"

#include "MainWindow.xaml.h"

#include "Core/Device/FolderDeviceSource.h"
#include "Core/Util/Strings.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
namespace fs = std::filesystem;

namespace winrt::ChatKeeper::implementation
{
    namespace
    {
        std::wstring DeviceLabel(ck::WpdDeviceInfo const& d)
        {
            std::wstring label = W(d.friendlyName);
            std::string maker = d.manufacturer;
            if (!maker.empty() && ck::ToLowerAscii(d.friendlyName).find(ck::ToLowerAscii(maker)) == std::string::npos)
                label += L" · " + W(maker);
            return label;
        }

        bool SameDevice(ck::WpdDeviceInfo const& a, ck::WpdDeviceInfo const& b)
        {
            return _wcsicmp(a.pnpId.c_str(), b.pnpId.c_str()) == 0;
        }

        std::wstring LongDate(std::int64_t unix)
        {
            std::time_t t = static_cast<std::time_t>(unix);
            std::tm tm{};
            localtime_s(&tm, &t);
            SYSTEMTIME st{};
            st.wYear = static_cast<WORD>(tm.tm_year + 1900);
            st.wMonth = static_cast<WORD>(tm.tm_mon + 1);
            st.wDay = static_cast<WORD>(tm.tm_mday);
            wchar_t date[80] = {};
            GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_LONGDATE, &st, nullptr, date, 80, nullptr);
            return date;
        }

        // "today at 18:56", "yesterday at 09:10", "12 March 2026 18:56"
        std::wstring BackupDateText(std::int64_t unix, bool* stale)
        {
            std::time_t t = static_cast<std::time_t>(unix), now = std::time(nullptr);
            std::tm tm{}, today{};
            localtime_s(&tm, &t);
            localtime_s(&today, &now);
            wchar_t hm[16];
            wcsftime(hm, 16, L"%H:%M", &tm);
            *stale = ck::IsBackupStale(unix, now);
            if (tm.tm_year == today.tm_year && tm.tm_yday == today.tm_yday) return SF(L"Phone_BackupToday", { hm }).c_str();
            std::time_t y = now - 24 * 3600;
            std::tm yest{};
            localtime_s(&yest, &y);
            if (tm.tm_year == yest.tm_year && tm.tm_yday == yest.tm_yday)
                return SF(L"Phone_BackupYesterday", { hm }).c_str();
            return SF(L"Phone_BackupOn", { LongDate(unix) + L" " + hm }).c_str();
        }
    }

    void MainWindow::ResetPhoneStep()
    {
        m_phoneConfirmed = false;
        m_state.folder.reset();
        m_state.folders.clear();
        m_state.backup = {};
        m_state.staleAccepted = false;
        PhoneResult().Visibility(Visibility::Collapsed);
        PhoneSearching().Visibility(Visibility::Collapsed);
        PhoneStale().IsOpen(false);
        PhoneError().IsOpen(false);
        PhoneAppChoice().Visibility(Visibility::Collapsed);
    }

    void MainWindow::EnterPhoneStep()
    {
        if (m_phoneConfirmed && m_state.folder && !m_state.fromFolder)
        {
            UpdateNavigation();
            return;  // already set up; coming back from a later step
        }
        m_state.fromFolder = false;
        m_state.source.reset();
        ResetPhoneStep();
        ShowDevices({});
        WatchForPhones();
    }

    winrt::fire_and_forget MainWindow::WatchForPhones()
    {
        auto strong = get_strong();
        int generation = ++m_watchGeneration;
        auto dispatcher = DispatcherQueue();
        while (generation == m_watchGeneration && !m_phoneConfirmed)
        {
            co_await winrt::resume_background();
            std::vector<ck::WpdDeviceInfo> devices;
            try
            {
                devices = ck::EnumerateWpdDevices();
            }
            catch (...)
            {
            }
            co_await wil::resume_foreground(dispatcher);
            if (generation != m_watchGeneration || m_phoneConfirmed) break;
            ShowDevices(devices);
            co_await winrt::resume_after(std::chrono::milliseconds(1500));
        }
    }

    void MainWindow::ShowDevices(std::vector<ck::WpdDeviceInfo> const& all)
    {
        std::vector<ck::WpdDeviceInfo> withStorage, locked;
        for (const auto& d : all)
        {
            if (m_rejectedDevice && SameDevice(d, *m_rejectedDevice)) continue;
            (d.storageCount > 0 ? withStorage : locked).push_back(d);
        }

        // Keep the list stable while the user is choosing.
        bool changed = withStorage.size() != m_devices.size();
        for (size_t i = 0; !changed && i < withStorage.size(); ++i)
            changed = !SameDevice(withStorage[i], m_devices[i]);
        m_devices = withStorage;

        PhoneWaiting().Visibility(withStorage.empty() ? Visibility::Visible : Visibility::Collapsed);
        PhoneLocked().IsOpen(withStorage.empty() && !locked.empty());
        PhoneSingle().Visibility(withStorage.size() == 1 ? Visibility::Visible : Visibility::Collapsed);
        PhoneMany().Visibility(withStorage.size() > 1 ? Visibility::Visible : Visibility::Collapsed);
        if (withStorage.size() == 1) PhoneSingleName().Text(DeviceLabel(withStorage[0]));
        if (withStorage.size() > 1 && changed)
        {
            PhoneList().Items().Clear();
            for (const auto& d : withStorage) PhoneList().Items().Append(box_value(DeviceLabel(d)));
            PhoneUseSelected().IsEnabled(false);
        }
    }

    void MainWindow::OnPhoneYes(IInspectable const&, RoutedEventArgs const&)
    {
        if (m_devices.size() == 1) UsePhone(m_devices[0]);
    }

    void MainWindow::OnPhoneNo(IInspectable const&, RoutedEventArgs const&)
    {
        if (m_devices.size() == 1) m_rejectedDevice = m_devices[0];
        ShowDevices({});
        PhoneError().Title(L"");
        PhoneError().Message(S(L"Phone_NotMine"));
        PhoneError().Severity(InfoBarSeverity::Informational);
        PhoneErrorAction().Visibility(Visibility::Collapsed);
        PhoneError().IsOpen(true);
    }

    void MainWindow::OnPhoneListSelection(IInspectable const&, SelectionChangedEventArgs const&)
    {
        PhoneUseSelected().IsEnabled(PhoneList().SelectedIndex() >= 0);
    }

    void MainWindow::OnPhoneUseSelected(IInspectable const&, RoutedEventArgs const&)
    {
        int i = PhoneList().SelectedIndex();
        if (i >= 0 && static_cast<size_t>(i) < m_devices.size()) UsePhone(m_devices[i]);
    }

    winrt::fire_and_forget MainWindow::UsePhone(ck::WpdDeviceInfo info)
    {
        auto strong = get_strong();
        auto dispatcher = DispatcherQueue();
        m_phoneConfirmed = true;
        ++m_watchGeneration;
        ResetPhoneStep();
        m_phoneConfirmed = true;
        PhoneSingle().Visibility(Visibility::Collapsed);
        PhoneMany().Visibility(Visibility::Collapsed);
        PhoneWaiting().Visibility(Visibility::Collapsed);
        PhoneLocked().IsOpen(false);
        PhoneSearching().Visibility(Visibility::Visible);
        SetBusy(true);

        std::shared_ptr<ck::IDeviceSource> source;
        std::vector<ck::WhatsAppFolder> folders;
        std::string error;
        co_await winrt::resume_background();
        try
        {
            source = std::make_shared<ck::WpdDeviceSource>(info);
            folders = ck::FindWhatsAppFolders(*source);
        }
        catch (ck::DeviceError const& e)
        {
            error = e.kind == ck::DeviceErrorKind::Disconnected ? "disconnected" : e.what();
        }
        catch (std::exception const& e)
        {
            error = e.what();
        }
        co_await wil::resume_foreground(dispatcher);

        SetBusy(false);
        PhoneSearching().Visibility(Visibility::Collapsed);
        m_state.source = source;
        m_state.phoneName = info.friendlyName;
        m_state.folders = folders;
        m_state.log->Info("Phone: " + info.friendlyName + " (" + info.manufacturer + ", " + info.description + ")");
        if (!error.empty() || folders.empty())
        {
            m_state.log->Warn("WhatsApp folder not found: " + error);
            PhoneError().Title(L"");
            PhoneError().Severity(InfoBarSeverity::Error);
            PhoneError().Message(error == "disconnected" ? S(L"Phone_Disconnected") : S(L"Phone_NoWhatsApp"));
            PhoneErrorAction().Visibility(Visibility::Collapsed);
            PhoneError().IsOpen(true);
            m_phoneConfirmed = false;
            WatchForPhones();
            co_return;
        }
        co_await ShowFolderResult();
    }

    winrt::fire_and_forget MainWindow::UseFolder(fs::path folder)
    {
        auto strong = get_strong();
        auto dispatcher = DispatcherQueue();
        SetBusy(true);
        std::shared_ptr<ck::IDeviceSource> source;
        std::vector<ck::WhatsAppFolder> folders;
        co_await winrt::resume_background();
        try
        {
            source = std::make_shared<ck::FolderDeviceSource>(folder, "WhatsApp folder");
            folders = ck::FindWhatsAppFolders(*source);
        }
        catch (...)
        {
        }
        co_await wil::resume_foreground(dispatcher);
        SetBusy(false);
        if (folders.empty())
        {
            WelcomeError().Message(S(L"Welcome_NoFolder"));
            WelcomeError().IsOpen(true);
            co_return;
        }
        m_state.fromFolder = true;
        m_state.source = source;
        m_state.phoneName = "";
        m_state.folders = folders;
        m_phoneConfirmed = true;
        m_state.log->Info("Using a folder on this PC: " + ck::PathToUtf8(folder));
        co_await ShowFolderResult();
        if (m_state.folder && m_state.backup.messages)
        {
            GoTo(Step::Copy);
        }
        else
        {
            // Explain on the welcome screen what's wrong with the folder.
            WelcomeError().Message(PhoneError().Message());
            WelcomeError().IsOpen(true);
        }
    }

    Windows::Foundation::IAsyncAction MainWindow::ShowFolderResult()
    {
        auto strong = get_strong();
        auto& folders = m_state.folders;
        bool hasStandard = false, hasBusiness = false;
        for (const auto& f : folders) (f.business ? hasBusiness : hasStandard) = true;

        m_folderChoiceUpdating = true;
        PhoneAppChoice().Items().Clear();
        if (hasStandard && hasBusiness)
        {
            for (const auto& f : folders)
                PhoneAppChoice().Items().Append(box_value(f.business ? S(L"Phone_AppBusiness") : S(L"Phone_AppWhatsApp")));
            PhoneAppChoice().Visibility(Visibility::Visible);
            PhoneAppChoice().SelectedIndex(0);
        }
        else
        {
            PhoneAppChoice().Visibility(Visibility::Collapsed);
        }
        m_folderChoiceUpdating = false;
        PhoneResult().Visibility(Visibility::Visible);
        co_await SelectWhatsAppFolder(0);
    }

    void MainWindow::OnPhoneAppChoice(IInspectable const&, SelectionChangedEventArgs const&)
    {
        int i = PhoneAppChoice().SelectedIndex();
        if (i >= 0 && !m_folderChoiceUpdating) SelectWhatsAppFolder(static_cast<size_t>(i));
    }

    Windows::Foundation::IAsyncAction MainWindow::SelectWhatsAppFolder(size_t index)
    {
        auto strong = get_strong();
        auto dispatcher = DispatcherQueue();
        if (index >= m_state.folders.size() || !m_state.source) co_return;
        auto folder = m_state.folders[index];
        auto source = m_state.source;
        m_state.folder.reset();
        m_state.backup = {};
        m_state.staleAccepted = false;
        PhoneStale().IsOpen(false);
        PhoneError().IsOpen(false);
        PhoneSearching().Visibility(Visibility::Visible);
        SetBusy(true);

        ck::BackupInfo info;
        std::string error;
        co_await winrt::resume_background();
        try
        {
            info = ck::InspectBackup(*source, folder);
        }
        catch (std::exception const& e)
        {
            error = e.what();
        }
        co_await wil::resume_foreground(dispatcher);
        SetBusy(false);
        PhoneSearching().Visibility(Visibility::Collapsed);

        if (!error.empty())
        {
            m_state.log->Warn("Reading the WhatsApp folder failed: " + error);
            PhoneError().Title(L"");
            PhoneError().Severity(InfoBarSeverity::Error);
            PhoneError().Message(S(L"Phone_Disconnected"));
            PhoneErrorAction().Visibility(Visibility::Collapsed);
            PhoneError().IsOpen(true);
            UpdateNavigation();
            co_return;
        }
        m_state.folder = folder;
        m_state.backup = info;
        m_state.log->Info("WhatsApp folder: " + (folder.path.empty() ? std::string("(chosen folder)") : folder.PathText()) +
                          " on " + folder.storageName);

        PhoneFoundText().Text(folder.business ? S(L"Phone_FoundBusiness") : S(L"Phone_Found"));
        PhoneBackupDate().Text(L"");
        if (!info.messages)
        {
            PhoneError().Title(L"");
            PhoneError().Severity(InfoBarSeverity::Error);
            PhoneError().Message(info.hasOlderFormatOnly ? S(L"Phone_NoCrypt15") : S(L"Phone_NoBackup"));
            PhoneErrorAction().Visibility(Visibility::Visible);
            PhoneError().IsOpen(true);
            m_state.log->Warn(info.hasOlderFormatOnly ? "Only .crypt14 (or older) backups" : "No msgstore backup");
        }
        else
        {
            bool stale = false;
            if (info.messages->modified > 0)
            {
                PhoneBackupDate().Text(BackupDateText(info.messages->modified, &stale));
                m_state.log->Info("Backup " + info.messages->name + ", " + std::to_string(info.messages->size) +
                                  " bytes, modified " + std::to_string(info.messages->modified));
            }
            if (stale)
            {
                PhoneStale().Message(SF(L"Phone_Stale", { LongDate(info.messages->modified) }));
                PhoneStale().IsOpen(true);
            }
        }
        UpdateNavigation();
    }

    void MainWindow::OnGoToStep1(IInspectable const&, RoutedEventArgs const&)
    {
        if (m_busy) return;
        m_phoneConfirmed = false;
        ResetPhoneStep();
        GoTo(Step::KeyGuide);
    }

    void MainWindow::OnStaleContinue(IInspectable const&, RoutedEventArgs const&)
    {
        m_state.staleAccepted = true;
        PhoneStale().IsOpen(false);
        UpdateNavigation();
    }
}
