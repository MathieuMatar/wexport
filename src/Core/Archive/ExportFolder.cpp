#include "ExportFolder.h"

#include <fstream>

#include "../Copy/Copier.h"
#include "../Copy/CopyPlan.h"
#include "../Exporter/ExporterCommand.h"
#include "../Util/FileSystem.h"
#include "../Util/Strings.h"

namespace ck {

namespace fs = std::filesystem;

namespace {

bool IsDir(const fs::path& p) {
    std::error_code ec;
    return fs::is_directory(LongPath(p), ec);
}

bool IsEmptyDir(const fs::path& p) {
    std::error_code ec;
    return IsDir(p) && fs::is_empty(LongPath(p), ec);
}

// Moves every entry of `from` into `to` (merging folders), then removes `from`.
std::optional<std::string> MergeMove(const fs::path& from, const fs::path& to) {
    std::error_code ec;
    fs::create_directories(LongPath(to), ec);
    for (fs::directory_iterator it(LongPath(from), ec), end; !ec && it != end; it.increment(ec)) {
        fs::path target = to / it->path().filename();
        if (IsDir(it->path()) && IsDir(target)) {
            if (auto err = MergeMove(it->path(), target)) return err;
        } else if (fs::exists(LongPath(target))) {
            // Same file already in place from an earlier attempt: keep the newer copy.
            fs::remove(LongPath(it->path()), ec);
        } else if (auto err = RenameWithRetry(it->path(), target)) {
            return "Cannot move " + PathToUtf8(it->path().filename()) + ": " + *err;
        }
    }
    fs::remove(LongPath(from), ec);
    return std::nullopt;
}

std::optional<std::string> MoveDir(const fs::path& from, const fs::path& to) {
    if (!IsDir(from)) return std::nullopt;
    if (!IsDir(to)) {
        std::error_code ec;
        fs::create_directories(LongPath(to.parent_path()), ec);
        if (!RenameWithRetry(from, to)) return std::nullopt;
    }
    return MergeMove(from, to);
}

}  // namespace

std::optional<std::string> EnsureMediaForExporter(const fs::path& dir, Logger* log) {
    fs::path viewer = dir / kViewerMediaDir;
    fs::path exporter = dir / PathFromUtf8(kExporterMediaDir);
    if (!IsDir(viewer)) return std::nullopt;
    if (log) log->Info("Moving media\\ back to WhatsApp\\Media for the exporter");
    return MoveDir(viewer, exporter);
}

std::optional<std::string> MoveMediaForViewer(const fs::path& dir, Logger* log) {
    fs::path exporterRoot = dir / kExporterMediaRoot;
    fs::path exporterMedia = dir / PathFromUtf8(kExporterMediaDir);
    fs::path viewer = dir / kViewerMediaDir;
    if (IsDir(exporterMedia)) {
        if (auto err = MoveDir(exporterMedia, viewer)) return err;
        if (log) log->Info("Moved WhatsApp\\Media to media\\");
    } else if (!IsDir(viewer)) {
        std::error_code ec;
        fs::create_directories(LongPath(viewer), ec);  // no media at all: the viewer still expects the folder
    }
    // wtsexporter writes <media>/thumbnails and <media>/vCards next to Media.
    // The viewer doesn't use them; keep them with the databases.
    for (const char* extra : {"thumbnails", "vCards"}) {
        fs::path from = exporterRoot / extra;
        if (!IsDir(from)) continue;
        if (IsEmptyDir(from)) {
            std::error_code ec;
            fs::remove(LongPath(from), ec);
        } else if (auto err = MoveDir(from, dir / kDataDir / extra)) {
            if (log) log->Warn("Could not move WhatsApp\\" + std::string(extra) + ": " + *err);
        }
    }
    if (IsEmptyDir(exporterRoot)) {
        std::error_code ec;
        fs::remove(LongPath(exporterRoot), ec);
    }
    return std::nullopt;
}

std::optional<std::string> CopyViewer(const fs::path& viewerHtml, const fs::path& dir) {
    std::error_code ec;
    fs::copy_file(LongPath(viewerHtml), LongPath(dir / kIndexHtml), fs::copy_options::overwrite_existing, ec);
    if (ec) return ec.message();
    return std::nullopt;
}

void WriteReadme(const fs::path& dir) {
    std::ofstream f(LongPath(dir / kReadmeFile), std::ios::binary | std::ios::trunc);
    const char* text =
        "Your WhatsApp archive\r\n"
        "=====================\r\n"
        "\r\n"
        "This folder holds your whole WhatsApp history: messages, photos, videos,\r\n"
        "voice notes, documents, calls and group members.\r\n"
        "\r\n"
        "To read it, double-click index.html. It opens in your web browser\r\n"
        "(Edge, Chrome or Firefox). No internet connection is needed, and it no\r\n"
        "longer needs WhatsApp, your phone or your phone number.\r\n"
        "\r\n"
        "Keep two copies\r\n"
        "---------------\r\n"
        "Copy this whole folder to another drive (a USB stick or external disk)\r\n"
        "or to cloud storage, so you still have it if this computer breaks.\r\n"
        "\r\n"
        "What's inside\r\n"
        "-------------\r\n"
        "  index.html        open this\r\n"
        "  chats.js          your messages (used by index.html)\r\n"
        "  members.js        group members (used by index.html)\r\n"
        "  media\\            photos, videos, voice notes and documents\r\n"
        "  data\\msgstore.db  the full message database (SQLite), unencrypted\r\n"
        "  data\\wa.db        the contacts database (SQLite), unencrypted\r\n"
        "  export-log.txt    what happened while this folder was made\r\n"
        "\r\n"
        "Privacy\r\n"
        "-------\r\n"
        "Everything here is unencrypted. Anyone who has this folder can read\r\n"
        "your messages, so store it somewhere private.\r\n";
    f << text;
}

std::vector<fs::path> PathsRemovedOnSuccess(const fs::path& dir) {
    std::vector<fs::path> out;
    if (IsDir(dir / kWorkDir)) out.push_back(dir / kWorkDir);
    if (IsEmptyDir(dir / kExporterMediaRoot)) out.push_back(dir / kExporterMediaRoot);
    std::error_code ec;
    for (fs::recursive_directory_iterator it(LongPath(dir), ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_regular_file(ec) && EndsWith(PathToUtf8(it->path().filename()), kPartialSuffix)) {
            fs::path rel = it->path().lexically_relative(LongPath(dir));
            if (!StartsWith(PathToUtf8(rel), kWorkDir)) out.push_back(dir / rel);
        }
    }
    return out;
}

void CleanupAfterSuccess(const fs::path& dir, Logger* log) {
    for (const auto& p : PathsRemovedOnSuccess(dir)) {
        std::error_code ec;
        fs::remove_all(LongPath(p), ec);
        if (log) {
            if (ec) log->Warn("Could not delete " + PathToUtf8(p) + ": " + ec.message());
            else log->Info("Deleted " + PathToUtf8(p.lexically_relative(dir)));
        }
    }
}

void CleanupAfterFailure(const fs::path& dir, Logger* log) {
    std::error_code ec;
    for (const char* name : {kChatsJs, kMembersJs, kChatsJson}) {
        fs::path p = dir / PathFromUtf8(name);
        if (fs::remove(LongPath(p), ec) && log) log->Info("Removed partial output " + std::string(name));
    }
    for (fs::recursive_directory_iterator it(LongPath(dir), ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (it->is_regular_file(e2) && EndsWith(PathToUtf8(it->path().filename()), kPartialSuffix))
            fs::remove(it->path(), e2);
    }
}

}  // namespace ck
