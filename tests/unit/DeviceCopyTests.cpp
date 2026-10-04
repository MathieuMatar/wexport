#include <fstream>
#include <sstream>

#include "Core/Copy/Copier.h"
#include "Core/Copy/CopyPlan.h"
#include "Core/Device/FolderDeviceSource.h"
#include "Core/Device/WhatsAppLocator.h"
#include "Core/Pipeline/CopySession.h"
#include "Core/Util/FileSystem.h"
#include "Test.h"

using namespace ck;
namespace fs = std::filesystem;

namespace {

void WriteFile(const fs::path& p, const std::string& data) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary);
    f << data;
}

std::string ReadFile(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void MakePhone(const fs::path& root, const std::string& waPath) {
    fs::path wa = root / waPath;
    WriteFile(wa / "Databases/msgstore.db.crypt15", std::string(300, 'm'));
    WriteFile(wa / "Databases/msgstore-2024-01-01.1.db.crypt14", "old");
    WriteFile(wa / "Backups/wa.db.crypt15", std::string(200, 'w'));
    WriteFile(wa / "Backups/stickers.db.crypt15", "s");
    WriteFile(wa / "Media/WhatsApp Images/IMG-1.jpg", std::string(5000, 'i'));
    WriteFile(wa / "Media/WhatsApp Images/Sent/IMG-2.jpg", std::string(3000, 'j'));
    WriteFile(wa / "Media/WhatsApp Images/Private/.nomedia", "");
    WriteFile(wa / "Media/.Statuses/s.jpg", "status");
    WriteFile(wa / "Media/WhatsApp Documents/a.pdf", "pdf");
    WriteFile(wa / "accounts/x", "no");
}

// Wraps a folder source and "unplugs" after a number of bytes.
class FlakySource : public IDeviceSource {
public:
    FlakySource(fs::path root, std::uint64_t failAfter) : inner_(std::move(root)), failAfter_(failAfter) {}
    std::string DisplayName() const override { return "Flaky"; }
    std::vector<DeviceEntry> Storages() override { Check(); return inner_.Storages(); }
    std::vector<DeviceEntry> List(const DeviceEntry& d) override { Check(); return inner_.List(d); }
    std::unique_ptr<IReadStream> Open(const DeviceEntry& f) override;
    bool IsConnected() override { return connected_; }
    bool TryReconnect() override {
        if (++reconnectPolls_ >= 2) connected_ = true;
        return connected_;
    }
    void Check() {
        if (!connected_) throw DeviceError(DeviceErrorKind::Disconnected, "unplugged");
    }
    void Consume(std::size_t n) {
        Check();
        read_ += n;
        if (!tripped_ && read_ > failAfter_) {
            tripped_ = true;
            connected_ = false;
            throw DeviceError(DeviceErrorKind::Disconnected, "unplugged");
        }
    }
    int reconnectPolls_ = 0;

private:
    FolderDeviceSource inner_;
    std::uint64_t failAfter_, read_ = 0;
    bool connected_ = true, tripped_ = false;
};

class FlakyStream : public IReadStream {
public:
    FlakyStream(std::unique_ptr<IReadStream> in, FlakySource& s) : in_(std::move(in)), s_(s) {}
    std::size_t Read(void* b, std::size_t n) override {
        std::size_t got = in_->Read(b, std::min<std::size_t>(n, 1000));
        s_.Consume(got);
        return got;
    }

private:
    std::unique_ptr<IReadStream> in_;
    FlakySource& s_;
};

std::unique_ptr<IReadStream> FlakySource::Open(const DeviceEntry& f) {
    Check();
    return std::make_unique<FlakyStream>(inner_.Open(f), *this);
}

}  // namespace

TEST(LocatorFindsModernLocationAndBusiness) {
    auto phone = t::TempDir("phone-modern");
    MakePhone(phone, "Android/media/com.whatsapp/WhatsApp");
    MakePhone(phone, "Android/media/com.whatsapp.w4b/WhatsApp Business");
    FolderDeviceSource src(phone);
    auto folders = FindWhatsAppFolders(src);
    CHECK_EQ(folders.size(), 2u);
    CHECK_EQ(folders[0].PathText(), std::string("Android/media/com.whatsapp/WhatsApp"));
    CHECK(!folders[0].business);
    CHECK(folders[1].business);
    auto info = InspectBackup(src, folders[0]);
    CHECK(info.messages.has_value());
    CHECK_EQ(info.messages->name, std::string("msgstore.db.crypt15"));
    CHECK(info.contacts.has_value());
    CHECK(info.media.has_value());
    CHECK(!info.hasOlderFormatOnly);
}

TEST(LocatorLegacyAndDirectFolder) {
    auto phone = t::TempDir("phone-legacy");
    MakePhone(phone, "WhatsApp");
    FolderDeviceSource src(phone);
    auto folders = FindWhatsAppFolders(src);
    CHECK_EQ(folders.size(), 1u);
    CHECK_EQ(folders[0].PathText(), std::string("WhatsApp"));
    // The user may pick the WhatsApp folder itself.
    FolderDeviceSource direct(phone / "WhatsApp");
    auto f2 = FindWhatsAppFolders(direct);
    CHECK_EQ(f2.size(), 1u);
    CHECK(f2[0].path.empty());
    CHECK(InspectBackup(direct, f2[0]).messages.has_value());
}

TEST(LocatorCrypt14Only) {
    auto phone = t::TempDir("phone-14");
    WriteFile(phone / "WhatsApp/Databases/msgstore.db.crypt14", "x");
    WriteFile(phone / "WhatsApp/Media/WhatsApp Images/a.jpg", "x");
    FolderDeviceSource src(phone);
    auto folders = FindWhatsAppFolders(src);
    CHECK_EQ(folders.size(), 1u);
    auto info = InspectBackup(src, folders[0]);
    CHECK(!info.messages);
    CHECK(info.hasOlderFormatOnly);
    auto empty = t::TempDir("phone-none");
    FolderDeviceSource none(empty);
    CHECK(FindWhatsAppFolders(none).empty());
}

TEST(NewestCrypt15Wins) {
    auto phone = t::TempDir("phone-newest");
    MakePhone(phone, "WhatsApp");
    WriteFile(phone / "WhatsApp/Databases/msgstore-2026-01-01.1.db.crypt15", "older");
    SetModifiedTime(phone / "WhatsApp/Databases/msgstore-2026-01-01.1.db.crypt15", 1700000000);
    SetModifiedTime(phone / "WhatsApp/Databases/msgstore.db.crypt15", 1790000000);
    FolderDeviceSource src(phone);
    auto info = InspectBackup(src, FindWhatsAppFolders(src)[0]);
    CHECK_EQ(info.messages->name, std::string("msgstore.db.crypt15"));
    CHECK(IsBackupStale(1700000000, 1790000000));
    CHECK(!IsBackupStale(1790000000 - 3600, 1790000000));
    CHECK(!IsBackupStale(0, 1790000000));
}

TEST(CopyPlanOrderAndSkips) {
    auto phone = t::TempDir("phone-plan");
    MakePhone(phone, "WhatsApp");
    WriteFile(phone / "WhatsApp/Media/WhatsApp Documents/what?.pdf", "q");
    FolderDeviceSource src(phone);
    auto f = FindWhatsAppFolders(src)[0];
    auto plan = BuildCopyPlan(src, InspectBackup(src, f), CancelToken());
    CHECK_EQ(plan.items[0].destPath, std::string("_work/msgstore.db.crypt15"));
    CHECK_EQ(plan.items[1].destPath, std::string("_work/wa.db.crypt15"));
    CHECK(plan.hasContactsBackup);
    CHECK_EQ(plan.items.size(), 2u + 4u);
    for (const auto& i : plan.items) {
        CHECK(i.destPath.find(".Statuses") == std::string::npos);
        CHECK(i.destPath.find(".nomedia") == std::string::npos);
        CHECK(i.destPath.find("accounts") == std::string::npos);
        CHECK(i.destPath.find("stickers.db") == std::string::npos);
    }
    CHECK_EQ(plan.totalBytes, 300u + 200u + 5000u + 3000u + 3u + 1u);
    CHECK_EQ(plan.renamed.size(), 1u);
    CHECK_EQ(plan.renamed[0].savedAs, std::string("WhatsApp/Media/WhatsApp Documents/what_.pdf"));
}

TEST(CopyResumesBySize) {
    auto phone = t::TempDir("phone-copy");
    MakePhone(phone, "WhatsApp");
    auto out = t::TempDir("export-copy");
    FolderDeviceSource src(phone);
    auto f = FindWhatsAppFolders(src)[0];
    auto plan = BuildCopyPlan(src, InspectBackup(src, f), CancelToken());
    SetModifiedTime(phone / "WhatsApp/Media/WhatsApp Images/IMG-1.jpg", 1700000000);
    plan = BuildCopyPlan(src, InspectBackup(src, f), CancelToken());

    CopyProgress last;
    auto r = RunCopy(src, plan, out, CancelToken(), [&](const CopyProgress& p) { last = p; }, nullptr);
    CHECK(r.status == CopyResult::Status::Completed);
    CHECK_EQ(r.copiedFiles, 5u);
    CHECK_EQ(last.bytesDone, plan.totalBytes);
    CHECK_EQ(ReadFile(out / "WhatsApp/Media/WhatsApp Images/Sent/IMG-2.jpg"), std::string(3000, 'j'));
    CHECK_EQ(ModifiedTime(out / "WhatsApp/Media/WhatsApp Images/IMG-1.jpg"), 1700000000);
    CHECK(fs::exists(out / "_work/msgstore.db.crypt15"));

    auto again = RunCopy(src, plan, out, CancelToken(), {}, nullptr);
    CHECK_EQ(again.copiedFiles, 0u);
    CHECK_EQ(again.skippedFiles, 5u);
}

TEST(CopyCancelLeavesNoPartials) {
    auto phone = t::TempDir("phone-cancel");
    MakePhone(phone, "WhatsApp");
    auto out = t::TempDir("export-cancel");
    FolderDeviceSource src(phone);
    auto plan = BuildCopyPlan(src, InspectBackup(src, FindWhatsAppFolders(src)[0]), CancelToken());
    CancelToken cancel;
    int calls = 0;
    auto r = RunCopy(src, plan, out, cancel, [&](const CopyProgress&) { if (++calls == 1) cancel.Cancel(); }, nullptr);
    CHECK(r.status == CopyResult::Status::Cancelled);
    for (auto& e : fs::recursive_directory_iterator(out))
        CHECK(e.path().extension() != ".partial");
}

TEST(CopyPausesAndResumesAfterUnplug) {
    auto phone = t::TempDir("phone-unplug");
    MakePhone(phone, "WhatsApp");
    auto out = t::TempDir("export-unplug");
    FlakySource src(phone, 4000);
    auto folder = FindWhatsAppFolders(src)[0];
    auto plan = BuildCopyPlan(src, InspectBackup(src, folder), CancelToken());
    std::vector<bool> states;
    CopySessionCallbacks cb;
    cb.connectionChanged = [&](bool waiting) { states.push_back(waiting); };
    auto r = CopyWithReconnect(src, folder, plan, out, CancelToken(), cb, nullptr);
    CHECK(r.status == CopyResult::Status::Completed);
    CHECK_EQ(states.size(), 2u);
    CHECK(states[0] && !states[1]);
    CHECK(r.failed.empty());
    CHECK_EQ(ReadFile(out / "WhatsApp/Media/WhatsApp Images/IMG-1.jpg"), std::string(5000, 'i'));
    CHECK_EQ(ReadFile(out / "WhatsApp/Media/WhatsApp Images/Sent/IMG-2.jpg"), std::string(3000, 'j'));
    CHECK_EQ(ReadFile(out / "_work/msgstore.db.crypt15"), std::string(300, 'm'));
    for (auto& e : fs::recursive_directory_iterator(out))
        CHECK(e.path().extension() != ".partial");
}
