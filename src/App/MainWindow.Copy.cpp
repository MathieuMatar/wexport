// Step 3: choose where to save, count, check space and OneDrive, then copy.

#include "pch.h"

#include "MainWindow.xaml.h"

#include "Core/Archive/ExportFolder.h"
#include "Core/Pipeline/CopySession.h"
#include "Core/Util/FileSystem.h"
#include "Core/Util/Strings.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
namespace fs = std::filesystem;

namespace winrt::ChatKeeper::implementation
{
    fs::path MainWindow::PlannedExportDir()
    {
        if (!m_state.exportDir.empty() && m_state.exportDir.parent_path() == m_state.saveLocation) return m_state.exportDir;
        std::time_t now = std::time(nullptr);
        std::tm tm{};
        localtime_s(&tm, &now);
        return m_state.saveLocation / ck::PathFromUtf8(ck::MakeExportFolderName(m_state.phoneName, tm));
    }

    void MainWindow::UpdateCopyFolderText()
    {
        CopyLocation().Text(m_state.saveLocation.native());
        CopyFolderName().Text(SF(L"Copy_FolderName", { PlannedExportDir().filename().native() }));
    }

    void MainWindow::EnterCopyStep()
    {
        UpdateCopyFolderText();
        CopySpace().IsOpen(false);
        CopyStart().IsEnabled(!m_busy);
        CopyChange().IsEnabled(!m_busy);
        UpdateNavigation();
    }

    winrt::fire_and_forget MainWindow::OnChangeLocation(IInspectable const&, RoutedEventArgs const&)
    {
        auto strong = get_strong();
        if (m_busy) co_return;
        auto folder = co_await PickFolder();
        if (!folder) co_return;
        fs::path chosen(std::wstring(folder.Path()));
        if (chosen != m_state.saveLocation)
        {
            m_state.saveLocation = chosen;
            m_state.exportDir.clear();
            m_state.copied = false;
            CopyDone().IsOpen(false);
        }
        CopySpace().IsOpen(false);
        UpdateCopyFolderText();
        UpdateNavigation();
    }

    void MainWindow::ShowCopyProgress(ck::CopyProgress const& p)
    {
        double fraction = p.bytesTotal ? static_cast<double>(p.bytesDone) / static_cast<double>(p.bytesTotal) : 0.0;
        CopyProgress().IsIndeterminate(false);
        CopyProgress().Value(fraction);
        std::wstring text(SF(L"Copy_Progress", { W(ck::FormatCount(p.filesDone)), W(ck::FormatCount(p.filesTotal)),
                                                W(ck::FormatBytes(p.bytesDone)), W(ck::FormatBytes(p.bytesTotal)) }));
        std::string left = ck::FormatTimeLeft(p.secondsLeft);
        if (!left.empty()) text += L" · " + W(left);
        CopyProgressText().Text(text);
        CopyCurrentFile().Text(W(p.currentFile));
    }

    winrt::fire_and_forget MainWindow::OnStartCopy(IInspectable const&, RoutedEventArgs const&)
    {
        auto strong = get_strong();
        auto dispatcher = DispatcherQueue();
        if (m_busy || !m_state.source || !m_state.folder) co_return;

        CopyDone().IsOpen(false);
        CopyError().IsOpen(false);
        CopySpace().IsOpen(false);
        CopyFailures().Visibility(Visibility::Collapsed);
        m_state.copied = false;

        fs::path exportDir = PlannedExportDir();
        m_cancel = ck::CancelToken();
        auto cancel = m_cancel;
        auto source = m_state.source;
        auto folder = *m_state.folder;
        auto backup = m_state.backup;
        auto log = m_state.log;

        SetBusy(true);
        CopyStart().IsEnabled(false);
        CopyChange().IsEnabled(false);
        CopyProgressArea().Visibility(Visibility::Visible);
        CopyProgress().IsIndeterminate(true);
        CopyProgressText().Text(SF(L"Copy_Counting", { L"0" }));
        CopyCurrentFile().Text(L"");

        // 1. Count what will be copied.
        ck::CopyPlan plan;
        std::string error;
        bool cancelled = false;
        co_await winrt::resume_background();
        try
        {
            plan = ck::BuildCopyPlan(*source, backup, cancel, [&](std::uint64_t files, std::uint64_t) {
                Ui([this, files] { CopyProgressText().Text(SF(L"Copy_Counting", { W(ck::FormatCount(files)) })); });
            });
        }
        catch (ck::OperationCancelled const&)
        {
            cancelled = true;
        }
        catch (std::exception const& e)
        {
            error = e.what();
        }
        co_await wil::resume_foreground(dispatcher);

        auto finish = [this] {
            SetBusy(false);
            CancelButton().IsEnabled(true);
            CopyStart().IsEnabled(true);
            CopyChange().IsEnabled(true);
        };
        if (cancelled || !error.empty())
        {
            finish();
            CopyProgressArea().Visibility(Visibility::Collapsed);
            if (!error.empty())
            {
                log->Error("Counting files failed: " + error);
                CopyError().Message(SF(L"Copy_Error", { W(error) }));
                CopyError().IsOpen(true);
            }
            co_return;
        }
        log->Info("Copy plan: " + std::to_string(plan.items.size()) + " files, " + std::to_string(plan.totalBytes) +
                  " bytes");

        // 2. Space check: what's already in the export folder doesn't need space again.
        std::uint64_t alreadyThere = 0;
        for (const auto& item : plan.items)
        {
            std::error_code ec;
            auto size = fs::file_size(ck::LongPath(exportDir / ck::PathFromUtf8(item.destPath)), ec);
            if (!ec && size == item.source.size) alreadyThere += size;
        }
        std::uint64_t needed = ck::RequiredSpace(plan.totalBytes - std::min(alreadyThere, plan.totalBytes));
        auto freeBytes = ck::FreeSpace(m_state.saveLocation);
        if (freeBytes && *freeBytes < needed)
        {
            finish();
            CopyProgressArea().Visibility(Visibility::Collapsed);
            CopySpace().Message(SF(L"Copy_NoSpace", { W(ck::FormatBytes(needed)), W(ck::FormatBytes(*freeBytes)) }));
            CopySpace().IsOpen(true);
            log->Warn("Not enough space: need " + std::to_string(needed) + ", free " + std::to_string(*freeBytes));
            co_return;
        }

        // 3. OneDrive warning.
        if (ck::IsInsideAny(m_state.saveLocation, ck::OneDriveRoots()))
        {
            bool chooseAnother = co_await AskAsync(S(L"Copy_OneDriveTitle"), S(L"Copy_OneDrive"), S(L"Copy_ChooseAnother"),
                                                   S(L"Copy_ContinueAnyway"));
            if (chooseAnother)
            {
                finish();
                CopyProgressArea().Visibility(Visibility::Collapsed);
                OnChangeLocation(nullptr, nullptr);
                co_return;
            }
        }

        // 4. Create the export folder and copy.
        std::error_code ec;
        fs::create_directories(ck::LongPath(exportDir), ec);
        if (ec)
        {
            finish();
            CopyProgressArea().Visibility(Visibility::Collapsed);
            CopyError().Message(S(L"Copy_CannotCreate"));
            CopyError().IsOpen(true);
            co_return;
        }
        m_state.exportDir = exportDir;
        log->Open(exportDir / ck::kLogFile);
        log->Info("Export folder: " + ck::PathToUtf8(exportDir));

        ck::CopySessionResult result;
        co_await winrt::resume_background();
        try
        {
            if (auto err = ck::EnsureMediaForExporter(exportDir, log.get())) log->Warn("Media folder: " + *err);
            ck::CopySessionCallbacks cb;
            cb.progress = [this](ck::CopyProgress const& p) { Ui([this, p] { ShowCopyProgress(p); }); };
            cb.connectionChanged = [this](bool waiting) { Ui([this, waiting] { CopyDisconnected().IsOpen(waiting); }); };
            result = ck::CopyWithReconnect(*source, folder, plan, exportDir, cancel, cb, log.get());
            ck::WriteRenamedList(result.plan, exportDir);
        }
        catch (std::exception const& e)
        {
            error = e.what();
        }
        co_await wil::resume_foreground(dispatcher);
        finish();
        CopyDisconnected().IsOpen(false);

        if (!error.empty())
        {
            log->Error("Copy failed: " + error);
            CopyError().Message(SF(L"Copy_Error", { W(error) }));
            CopyError().IsOpen(true);
            co_return;
        }
        if (result.status == ck::CopyResult::Status::Cancelled)
        {
            CopyError().Severity(InfoBarSeverity::Informational);
            CopyError().Message(S(L"Copy_Cancelled"));
            CopyError().IsOpen(true);
            co_return;
        }
        CopyError().Severity(InfoBarSeverity::Error);
        CopyProgress().Value(1.0);
        CopyCurrentFile().Text(L"");
        std::uint64_t files = result.plan.items.size() - result.failed.size();
        CopyDone().Message(SF(L"Copy_Done", { W(ck::FormatCount(files)), W(ck::FormatBytes(result.plan.totalBytes)) }));
        CopyDone().IsOpen(true);
        if (!result.failed.empty())
        {
            std::wstring list;
            for (const auto& f : result.failed) list += W(f.phonePath) + L"  (" + W(f.reason) + L")\n";
            CopyFailureList().Text(list);
            CopyFailures().Header(box_value(SF(L"Copy_Failed", { W(ck::FormatCount(result.failed.size())) })));
            CopyFailures().Visibility(Visibility::Visible);
        }
        // The messages backup is the one file that can't be missing.
        std::error_code e2;
        m_state.copied = fs::exists(ck::LongPath(exportDir / ck::PathFromUtf8(ck::kMessagesBackup)), e2);
        if (!m_state.copied)
        {
            CopyDone().IsOpen(false);
            CopyError().Message(SF(L"Copy_Error", { L"msgstore.db.crypt15" }));
            CopyError().IsOpen(true);
        }
        UpdateNavigation();
    }
}
