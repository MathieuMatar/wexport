// Step 5: the 64-digit key in 16 boxes of 4, like WhatsApp shows it.
// The key only ever lives in these controls and, during the exporter run,
// in memory. It is never written to disk or logged.

#include "pch.h"

#include "MainWindow.xaml.h"

#include "Core/Util/Strings.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

namespace winrt::ChatKeeper::implementation
{
    void MainWindow::BuildKeyBoxes()
    {
        auto grid = KeyGrid();
        for (int c = 0; c < 4; ++c) grid.ColumnDefinitions().Append(ColumnDefinition());
        for (int r = 0; r < 4; ++r) grid.RowDefinitions().Append(RowDefinition());
        for (int i = 0; i < 16; ++i)
        {
            PasswordBox box;
            box.MaxLength(4);
            box.Width(96);
            box.FontFamily(Media::FontFamily(L"Consolas"));
            box.FontSize(18);
            box.PasswordRevealMode(PasswordRevealMode::Visible);
            box.PlaceholderText(L"0000");
            box.IsPasswordRevealButtonEnabled(false);
            Automation::AutomationProperties::SetName(box, std::to_wstring(i * 4 + 1) + L"–" + std::to_wstring(i * 4 + 4));
            Grid::SetRow(box, i / 4);
            Grid::SetColumn(box, i % 4);
            box.PasswordChanged([this, i](IInspectable const&, RoutedEventArgs const&) { OnKeyBoxChanged(i); });
            // Pasting anywhere fills all the boxes (from that box on).
            box.Paste([this, i](IInspectable const&, TextControlPasteEventArgs const& e) {
                e.Handled(true);
                [](com_ptr<MainWindow> self, int start) -> fire_and_forget {
                    auto content = Windows::ApplicationModel::DataTransfer::Clipboard::GetContent();
                    if (!content.Contains(Windows::ApplicationModel::DataTransfer::StandardDataFormats::Text())) co_return;
                    hstring text = co_await content.GetTextAsync();
                    co_await wil::resume_foreground(self->DispatcherQueue());
                    std::string digits = ck::HexDigitsOnly(ck::ToUtf8(std::wstring(text)));
                    // A whole key pasted into a later box still means the whole key.
                    self->FillKey(digits, digits.size() >= 64 ? 0 : start);
                }(get_strong(), i);
            });
            grid.Children().Append(box);
            m_keyBoxes.push_back(box);
        }
    }

    std::string MainWindow::KeyDigits() const
    {
        std::string all;
        for (const auto& box : m_keyBoxes) all += ck::ToUtf8(std::wstring(box.Password()));
        return all;
    }

    void MainWindow::FillKey(std::string_view digits, int startBox)
    {
        m_keyUpdating = true;
        size_t pos = 0;
        int last = startBox;
        for (int i = startBox; i < 16 && pos < digits.size(); ++i)
        {
            std::string part(digits.substr(pos, 4));
            pos += part.size();
            m_keyBoxes[i].Password(ck::ToWide(part));
            last = i;
        }
        m_keyUpdating = false;
        m_keyBoxes[std::min(last + (pos >= 4 && digits.size() % 4 == 0 && last < 15 ? 1 : 0), 15)].Focus(FocusState::Programmatic);
        UpdateKeyStatus();
    }

    void MainWindow::OnKeyBoxChanged(int index)
    {
        if (m_keyUpdating) return;
        auto box = m_keyBoxes[index];
        std::string raw = ck::ToUtf8(std::wstring(box.Password()));
        std::string clean = ck::HexDigitsOnly(raw);
        if (clean != raw)
        {
            m_keyUpdating = true;
            box.Password(ck::ToWide(clean));
            m_keyUpdating = false;
        }
        if (clean.size() == 4 && index < 15) m_keyBoxes[index + 1].Focus(FocusState::Keyboard);
        KeyWrong().IsOpen(false);
        UpdateKeyStatus();
    }

    void MainWindow::UpdateKeyStatus()
    {
        std::string digits = KeyDigits();
        bool valid = ck::NormalizeKey(digits).has_value();
        KeyStatus().Text(valid ? S(L"Key_Valid") : SF(L"Key_Count", { std::to_wstring(digits.size()) }));
        UpdateNavigation();
    }

    void MainWindow::OnKeyShowToggle(IInspectable const&, RoutedEventArgs const&)
    {
        m_keyHidden = !m_keyHidden;
        for (auto& box : m_keyBoxes)
            box.PasswordRevealMode(m_keyHidden ? PasswordRevealMode::Hidden : PasswordRevealMode::Visible);
        KeyShow().Content(box_value(m_keyHidden ? S(L"Key_ShowText") : S(L"Key_HideText")));
    }

    winrt::fire_and_forget MainWindow::OnKeyPasteButton(IInspectable const&, RoutedEventArgs const&)
    {
        auto strong = get_strong();
        auto content = Windows::ApplicationModel::DataTransfer::Clipboard::GetContent();
        if (!content.Contains(Windows::ApplicationModel::DataTransfer::StandardDataFormats::Text())) co_return;
        hstring text = co_await content.GetTextAsync();
        co_await wil::resume_foreground(DispatcherQueue());
        FillKey(ck::HexDigitsOnly(ck::ToUtf8(std::wstring(text))), 0);
    }

    void MainWindow::ClearKey()
    {
        m_keyUpdating = true;
        for (auto& box : m_keyBoxes) box.Password(L"");
        m_keyUpdating = false;
        UpdateKeyStatus();
    }

    void MainWindow::OnKeyClear(IInspectable const&, RoutedEventArgs const&)
    {
        ClearKey();
        if (!m_keyBoxes.empty()) m_keyBoxes[0].Focus(FocusState::Programmatic);
    }
}
