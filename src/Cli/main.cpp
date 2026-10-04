// chatkeeper-cli: runs the whole pipeline (find -> copy -> unlock -> build ->
// clean up) from a folder shaped like the phone's WhatsApp folder. Used by the
// end-to-end tests and handy for support. It never needs a phone.
//
//   chatkeeper-cli --from <folder> --out <parent folder> --key-file <file>
//                  --exporter <wtsexporter> --viewer <index.html>
//                  [--vcf <file> --country-code 961] [--export-dir <exact folder>]

#include <cstdio>
#include <ctime>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "../Core/Archive/ExportFolder.h"
#include "../Core/Device/FolderDeviceSource.h"
#include "../Core/Device/WhatsAppLocator.h"
#include "../Core/Pipeline/BuildArchive.h"
#include "../Core/Exporter/ProcessRunner.h"
#include "../Core/Pipeline/CopySession.h"
#include "../Core/Util/FileSystem.h"
#include "../Core/Util/Strings.h"

using namespace ck;
namespace fs = std::filesystem;

namespace {

int Usage() {
    std::cerr << "usage: chatkeeper-cli --from DIR --out DIR --key-file FILE --exporter EXE --viewer HTML\n"
                 "                      [--vcf FILE --country-code CC] [--export-dir DIR]\n";
    return 2;
}

}  // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t** wargv) {
    std::vector<std::string> argv;
    for (int i = 0; i < argc; ++i) argv.push_back(ToUtf8(wargv[i]));
#else
int main(int argc, char** argv) {
#endif
    // Diagnostic: chatkeeper-cli --exec <exe> [args...] runs a process exactly the way the app runs wtsexporter.
    if (argc >= 3 && std::string(argv[1]) == "--exec") {
        ProcessSpec spec;
        spec.executable = fs::absolute(PathFromUtf8(argv[2]));
        for (int i = 3; i < argc; ++i) spec.args.push_back(argv[i]);
        spec.workingDirectory = fs::current_path();
        spec.extraEnvironment = {{"PYTHONUTF8", "1"}, {"PYTHONIOENCODING", "utf-8"}};
        std::size_t bytes = 0;
        auto r = RunProcess(spec, CancelToken(), [&](std::string_view chunk) {
            bytes += chunk.size();
            std::cout << chunk;
        });
        std::cout << "\n[exec] exit=" << r.exitCode << " bytes=" << bytes << " startFailed=" << r.startFailed << " "
                  << r.startError << std::endl;
        return r.exitCode;
    }
    std::map<std::string, std::string> a;
    for (int i = 1; i + 1 < argc; i += 2) a[argv[i]] = argv[i + 1];
    for (const char* req : {"--from", "--key-file", "--exporter", "--viewer"})
        if (!a.count(req)) return Usage();
    if (!a.count("--out") && !a.count("--export-dir")) return Usage();

    std::ifstream kf(PathFromUtf8(a["--key-file"]));
    std::stringstream ks;
    ks << kf.rdbuf();
    auto key = NormalizeKey(Trim(ks.str()));
    if (!key) {
        std::cerr << "The key file doesn't hold a 64-digit key\n";
        return 2;
    }

    FolderDeviceSource source(PathFromUtf8(a["--from"]), "Test Phone");
    auto folders = FindWhatsAppFolders(source);
    if (folders.empty()) {
        std::cerr << "No WhatsApp folder found\n";
        return 3;
    }
    const auto& folder = folders.front();
    BackupInfo info = InspectBackup(source, folder);
    if (!info.messages) {
        std::cerr << (info.hasOlderFormatOnly ? "Only .crypt14 backups found (end-to-end backup with a key is off)\n"
                                              : "No msgstore .crypt15 backup found\n");
        return 3;
    }
    std::cout << "WhatsApp folder: " << (folder.path.empty() ? "(root)" : folder.PathText())
              << (folder.business ? " [Business]" : "") << "\n";

    fs::path exportDir;
    if (a.count("--export-dir")) {
        exportDir = PathFromUtf8(a["--export-dir"]);
    } else {
        std::time_t now = std::time(nullptr);
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &now);
#else
        localtime_r(&now, &tm);
#endif
        exportDir = PathFromUtf8(a["--out"]) / PathFromUtf8(MakeExportFolderName(source.DisplayName(), tm));
    }
    fs::create_directories(exportDir);

    Logger log;
    log.Open(exportDir / kLogFile);
    CancelToken cancel;

    if (auto err = EnsureMediaForExporter(exportDir, &log)) {
        std::cerr << "Cannot prepare media: " << *err << "\n";
        return 4;
    }
    CopyPlan plan = BuildCopyPlan(source, info, cancel);
    std::cout << "Copying " << FormatCount(plan.items.size()) << " files (" << FormatBytes(plan.totalBytes) << ")\n";
    auto copy = CopyWithReconnect(source, folder, plan, exportDir, cancel, {}, &log);
    WriteRenamedList(copy.plan, exportDir);
    std::cout << "Copied " << copy.copiedFiles << ", skipped " << copy.skippedFiles << ", failed "
              << copy.failed.size() << "\n";

    BuildOptions bo;
    bo.exportDir = exportDir;
    bo.exporterExe = PathFromUtf8(a["--exporter"]);
    if (bo.exporterExe.has_parent_path()) bo.exporterExe = fs::absolute(bo.exporterExe);
    bo.viewerHtml = PathFromUtf8(a["--viewer"]);
    bo.key = *key;
    SecureClear(*key);
    if (a.count("--vcf")) {
        bo.vcf = PathFromUtf8(a["--vcf"]);
        bo.countryCode = a.count("--country-code") ? a["--country-code"] : "";
    }
    BuildStage last = BuildStage::Unlocking;
    auto result = RunBuild(std::move(bo), cancel, [&](const BuildProgress& p) {
        if (p.stage != last || !p.detail.empty()) {
            last = p.stage;
            if (!p.detail.empty()) std::cout << "  " << p.detail << "\n";
        }
    }, log);
    if (result.status != BuildResult::Status::Ok) {
        std::cerr << result.message << "\n" << result.details << "\n";
        return result.status == BuildResult::Status::WrongKey ? 10 : 11;
    }
    const auto& s = result.stats;
    std::cout << "chats=" << s.chats << " groups=" << s.groups << " messages=" << s.messages
              << " photos=" << s.photos << " videos=" << s.videos << " audio=" << s.audio
              << " documents=" << s.documents << " stickers=" << s.stickers << " calls=" << s.calls
              << " missing=" << s.missingMedia << " members=" << (result.membersWritten ? 1 : 0)
              << " bytes=" << result.folderBytes << "\n";
    std::cout << "Export folder: " << PathToUtf8(exportDir) << "\n";
    return 0;
}
