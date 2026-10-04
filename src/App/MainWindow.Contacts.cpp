// Step 4: optional contacts file (.vcf) and the default country code.

#include "pch.h"

#include "MainWindow.xaml.h"

#include "Core/Archive/Vcf.h"
#include "Core/Copy/CopyPlan.h"
#include "Core/Util/CountryCodes.h"
#include "Core/Util/FileSystem.h"
#include "Core/Util/Strings.h"

#include <fstream>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
namespace fs = std::filesystem;

namespace winrt::ChatKeeper::implementation
{
    namespace
    {
        std::wstring CountryItem(ck::Country const& c) { return W(c.name) + L" (+" + W(c.code) + L")"; }
    }

    void MainWindow::EnterContactsStep()
    {
        auto box = CountryCode();
        if (box.Items().Size() == 0)
        {
            std::string region = ck::UserRegionCode();
            int selected = -1, i = 0;
            for (const auto& c : ck::Countries())
            {
                box.Items().Append(box_value(hstring(CountryItem(c))));
                if (!region.empty() && ck::EqualsIgnoreCase(c.iso2, region)) selected = i;
                ++i;
            }
            if (selected >= 0) box.SelectedIndex(selected);
        }
        UpdateNavigation();
    }

    std::string MainWindow::CountryCodeFromBox()
    {
        // The box is editable: take the code in "(+961)" if present, else the digits typed.
        std::string text = ck::ToUtf8(std::wstring(CountryCode().Text()));
        if (text.empty())
        {
            if (auto item = CountryCode().SelectedItem()) text = ck::ToUtf8(std::wstring(unbox_value<hstring>(item)));
        }
        auto plus = text.rfind("(+");
        if (plus != std::string::npos) text = text.substr(plus + 2);
        std::string digits;
        for (char c : text)
        {
            if (c >= '0' && c <= '9') digits += c;
            else if (!digits.empty()) break;
        }
        return digits.size() <= 4 ? digits : "";
    }

    void MainWindow::OnContactsTab(SelectorBar const& sender, SelectorBarSelectionChangedEventArgs const&)
    {
        bool phone = sender.SelectedItem() && sender.Items().Size() > 1 && sender.SelectedItem() == sender.Items().GetAt(1);
        ContactsHowToGoogle().Visibility(phone ? Visibility::Collapsed : Visibility::Visible);
        ContactsHowToPhone().Visibility(phone ? Visibility::Visible : Visibility::Collapsed);
    }

    void MainWindow::SetVcf(fs::path const& file, std::wstring const& shownName)
    {
        ContactsError().IsOpen(false);
        if (!ck::LooksLikeVcf(file))
        {
            ContactsError().Message(S(L"Contacts_NotVcf"));
            ContactsError().IsOpen(true);
            return;
        }
        m_state.vcf = file;
        ContactsChosen().Message(SF(L"Contacts_Chosen", { shownName }));
        ContactsChosen().IsOpen(true);
        m_state.log->Info("Contacts file chosen");
        UpdateNavigation();
    }

    winrt::fire_and_forget MainWindow::OnChooseVcf(IInspectable const&, RoutedEventArgs const&)
    {
        auto strong = get_strong();
        Windows::Storage::Pickers::FileOpenPicker picker;
        picker.as<::IInitializeWithWindow>()->Initialize(WindowHandle());
        picker.SuggestedStartLocation(Windows::Storage::Pickers::PickerLocationId::Downloads);
        picker.FileTypeFilter().Append(L".vcf");
        auto file = co_await picker.PickSingleFileAsync();
        if (!file) co_return;
        fs::path path(std::wstring(file.Path()));
        SetVcf(path, path.native());
    }

    void MainWindow::OnSkipContacts(IInspectable const&, RoutedEventArgs const&)
    {
        m_state.vcf.reset();
        ContactsChosen().IsOpen(false);
        GoTo(Step::Key);
    }

    winrt::fire_and_forget MainWindow::OnFindVcfOnPhone(IInspectable const&, RoutedEventArgs const&)
    {
        auto strong = get_strong();
        auto dispatcher = DispatcherQueue();
        ContactsError().IsOpen(false);
        auto source = m_state.source;
        if (!source || m_state.fromFolder)
        {
            ContactsError().Message(S(L"Contacts_NoPhone"));
            ContactsError().IsOpen(true);
            co_return;
        }
        ContactsBrowsePhone().IsEnabled(false);
        std::vector<ck::DeviceEntry> found;
        std::vector<std::wstring> labels;
        co_await winrt::resume_background();
        try
        {
            // The Contacts app exports to the storage root; people also move it to Download or Documents.
            for (const auto& storage : source->Storages())
            {
                std::vector<std::pair<ck::DeviceEntry, std::string>> dirs = { { storage, "" } };
                for (const char* sub : { "Download", "Downloads", "Documents", "Contacts" })
                    if (auto d = ck::ResolvePath(*source, storage, { sub })) dirs.push_back({ *d, std::string(sub) + "/" });
                for (const auto& [dir, prefix] : dirs)
                {
                    for (const auto& f : source->List(dir))
                    {
                        if (f.isDirectory || !ck::EndsWith(ck::ToLowerAscii(f.name), ".vcf")) continue;
                        found.push_back(f);
                        labels.push_back(W(storage.name + "/" + prefix + f.name));
                    }
                }
            }
        }
        catch (...)
        {
        }
        co_await wil::resume_foreground(dispatcher);
        ContactsBrowsePhone().IsEnabled(true);
        m_phoneVcfs = found;
        ContactsPhoneFiles().Items().Clear();
        for (const auto& l : labels) ContactsPhoneFiles().Items().Append(box_value(hstring(l)));
        ContactsPhoneFiles().Visibility(found.empty() ? Visibility::Collapsed : Visibility::Visible);
        if (found.empty())
        {
            ContactsError().Message(S(L"Contacts_NoPhoneFiles"));
            ContactsError().IsOpen(true);
        }
    }

    winrt::fire_and_forget MainWindow::OnPhoneVcfSelected(IInspectable const&, SelectionChangedEventArgs const&)
    {
        auto strong = get_strong();
        auto dispatcher = DispatcherQueue();
        int i = ContactsPhoneFiles().SelectedIndex();
        if (i < 0 || static_cast<size_t>(i) >= m_phoneVcfs.size() || m_state.exportDir.empty() || !m_state.source) co_return;
        auto entry = m_phoneVcfs[i];
        auto source = m_state.source;
        // A file on the phone can't be read in place, so it goes to _work\ (deleted after a successful build).
        fs::path target = m_state.exportDir / ck::kWorkDir / L"contacts-from-phone.vcf";
        bool ok = false;
        co_await winrt::resume_background();
        try
        {
            std::error_code ec;
            fs::create_directories(target.parent_path(), ec);
            auto in = source->Open(entry);
            std::ofstream out(ck::LongPath(target), std::ios::binary | std::ios::trunc);
            char buf[65536];
            while (std::size_t n = in->Read(buf, sizeof buf)) out.write(buf, static_cast<std::streamsize>(n));
            ok = static_cast<bool>(out);
        }
        catch (...)
        {
        }
        co_await wil::resume_foreground(dispatcher);
        if (ok) SetVcf(target, W(entry.name));
        else
        {
            ContactsError().Message(S(L"Contacts_NotVcf"));
            ContactsError().IsOpen(true);
        }
    }
}
