#include "BuildArchive.h"

#include "../Archive/ExportFolder.h"
#include "../Archive/MembersBuilder.h"
#include "../Copy/CopyPlan.h"
#include "../Exporter/ExporterCommand.h"
#include "../Exporter/ProcessRunner.h"
#include "../Util/FileSystem.h"
#include "../Util/Strings.h"

namespace ck {

namespace fs = std::filesystem;

namespace {

bool Exists(const fs::path& p) {
    std::error_code ec;
    return fs::exists(LongPath(p), ec);
}

}  // namespace

BuildResult RunBuild(BuildOptions o, const CancelToken& cancel,
                     const std::function<void(const BuildProgress&)>& onProgress, Logger& log) {
    BuildResult r;
    KeepAwake awake;
    auto progress = [&](BuildStage stage, std::optional<double> f, std::string detail) {
        if (onProgress) onProgress({stage, f, std::move(detail)});
    };
    auto fail = [&](BuildResult::Status status, std::string message, std::string details = {}) {
        CleanupAfterFailure(o.exportDir, &log);
        r.status = status;
        r.message = std::move(message);
        r.details = RedactSecrets(details, {o.key});
        log.Error(r.message);
        if (!r.details.empty()) log.Error("Details:\n" + r.details);
        log.ClearSecrets();
        SecureClear(o.key);
        return r;
    };

    log.AddSecret(o.key);
    const fs::path& dir = o.exportDir;
    std::error_code ec;
    fs::create_directories(LongPath(dir / kDataDir), ec);
    fs::create_directories(LongPath(dir / kWorkDir), ec);

    if (!Exists(dir / PathFromUtf8(kMessagesBackup)))
        return fail(BuildResult::Status::BuildFailed,
                    "The copied backup (msgstore.db.crypt15) is missing from the export folder. Copy it again.");
    if (!Exists(o.exporterExe))
        return fail(BuildResult::Status::BuildFailed, "The exporter is missing from the app folder. Reinstall the app.",
                    PathToUtf8(o.exporterExe));
    if (!Exists(o.viewerHtml))
        return fail(BuildResult::Status::BuildFailed, "The viewer is missing from the app folder. Reinstall the app.",
                    PathToUtf8(o.viewerHtml));
    if (auto err = EnsureMediaForExporter(dir, &log))
        return fail(BuildResult::Status::BuildFailed, "Could not prepare the media folder.", *err);
    fs::remove(LongPath(dir / PathFromUtf8(kChatsJson)), ec);

    // 1-2. Unlock and read (one wtsexporter run).
    progress(BuildStage::Unlocking, std::nullopt, "");
    ExporterOptions eo;
    eo.key = o.key;
    eo.hasContactsBackup = Exists(dir / PathFromUtf8(kContactsBackup));
    eo.vcf = o.vcf;
    eo.countryCode = o.countryCode;
    ProcessSpec spec;
    spec.executable = o.exporterExe;
    spec.args = BuildExporterArgs(eo);
    SecureClear(eo.key);
    spec.workingDirectory = dir;
    spec.extraEnvironment = {{"PYTHONUTF8", "1"}, {"PYTHONIOENCODING", "utf-8"}};
    log.Info("Running " + DescribeExporterArgs(spec.args));

    ExporterOutputParser parser([&](const ExporterStatus& st) {
        bool reading = st.stage != ExporterStage::Starting && st.stage != ExporterStage::Decrypting;
        progress(reading ? BuildStage::Reading : BuildStage::Unlocking, st.fraction, st.line);
    });
    ProcessResult pr = RunProcess(spec, cancel, [&](std::string_view chunk) { parser.Feed(chunk); });
    for (auto& a : spec.args) SecureClear(a);
    parser.Finish();
    log.Info("Exporter output:\n" + parser.Tail(200));
    log.Info("Exporter exit code " + std::to_string(pr.exitCode));

    if (pr.cancelled || cancel.IsCancelled()) return fail(BuildResult::Status::Cancelled, "Cancelled.");
    if (pr.startFailed)
        return fail(BuildResult::Status::ExporterFailed, "The exporter could not be started.", pr.startError);
    switch (ClassifyExporterResult(pr.exitCode, parser.FullText())) {
        case ExporterFailure::None:
            break;
        case ExporterFailure::WrongKey:
            return fail(BuildResult::Status::WrongKey,
                        "That key doesn't open this backup. Check for typos and try again.", parser.Tail());
        case ExporterFailure::NotABackup:
            return fail(BuildResult::Status::ExporterFailed,
                        "The backup file looks damaged or incomplete. Make a fresh backup and copy it again.",
                        parser.Tail());
        default:
            return fail(BuildResult::Status::ExporterFailed, "Something went wrong while reading your backup.",
                        parser.Tail());
    }
    if (!Exists(dir / PathFromUtf8(kChatsJson)))
        return fail(BuildResult::Status::ExporterFailed, "The exporter finished but wrote no messages.",
                    parser.Tail());

    // 3. Build the archive.
    progress(BuildStage::Building, std::nullopt, "Writing chats.js");
    ChatsJsResult js = WriteChatsJs(dir / PathFromUtf8(kChatsJson), dir / kChatsJs, cancel);
    if (cancel.IsCancelled()) return fail(BuildResult::Status::Cancelled, "Cancelled.");
    if (!js.ok) return fail(BuildResult::Status::BuildFailed, "Could not build the archive.", js.error);
    r.stats = js.stats;
    log.Info("chats.js: " + std::to_string(js.jsBytes) + " bytes, " + std::to_string(js.stats.chats) + " chats, " +
             std::to_string(js.stats.messages) + " messages, " + std::to_string(js.escapedSeparators) +
             " line separators escaped");
    if (js.stats.mediaPaths > 0 && js.stats.mediaPathsUnderMediaDir < js.stats.mediaPaths)
        log.Warn(std::to_string(js.stats.mediaPaths - js.stats.mediaPathsUnderMediaDir) +
                 " media paths don't contain WhatsApp/Media/; the viewer falls back to media/<file name>");

    progress(BuildStage::Building, 0.4, "Moving media into place");
    if (auto err = MoveMediaForViewer(dir, &log))
        return fail(BuildResult::Status::BuildFailed,
                    "Could not move the media folder into place. Close any window showing the export folder and "
                    "try again.",
                    *err);

    progress(BuildStage::Building, 0.7, "Group members");
    MembersInput mi;
    mi.messagesDb = dir / PathFromUtf8(kMessagesDb);
    if (Exists(dir / PathFromUtf8(kContactsDb))) mi.contactsDb = dir / PathFromUtf8(kContactsDb);
    mi.vcf = o.vcf;
    mi.countryCode = o.countryCode;
    r.membersWritten = WriteMembersJs(mi, dir / kMembersJs, &log);

    progress(BuildStage::Building, 0.9, "Viewer");
    if (auto err = CopyViewer(o.viewerHtml, dir))
        return fail(BuildResult::Status::BuildFailed, "Could not copy the viewer into the export folder.", *err);
    WriteReadme(dir);

    // 4. Clean up, only once chats.js is validated and media\ is in place.
    progress(BuildStage::CleaningUp, std::nullopt, "");
    if (!Exists(dir / kChatsJs) || !Exists(dir / kViewerMediaDir))
        return fail(BuildResult::Status::BuildFailed, "The archive is incomplete.", "chats.js or media\\ missing");
    CleanupAfterSuccess(dir, &log);

    r.folderBytes = FolderSize(dir);
    r.status = BuildResult::Status::Ok;
    log.Info("Archive finished: " + std::to_string(r.folderBytes) + " bytes");
    log.ClearSecrets();
    SecureClear(o.key);
    progress(BuildStage::Finished, 1.0, "");
    return r;
}

}  // namespace ck
