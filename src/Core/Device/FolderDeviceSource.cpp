#include "FolderDeviceSource.h"

#include <fstream>

#include "../Util/FileSystem.h"
#include "../Util/Strings.h"

namespace ck {

namespace {

class FileReadStream final : public IReadStream {
public:
    explicit FileReadStream(const std::filesystem::path& path) : in_(LongPath(path), std::ios::binary) {
        if (!in_) throw DeviceError(DeviceErrorKind::NotFound, "Cannot open " + PathToUtf8(path));
    }
    std::size_t Read(void* buffer, std::size_t size) override {
        in_.read(static_cast<char*>(buffer), static_cast<std::streamsize>(size));
        if (in_.bad()) throw DeviceError(DeviceErrorKind::Io, "Read error");
        return static_cast<std::size_t>(in_.gcount());
    }

private:
    std::ifstream in_;
};

DeviceEntry MakeEntry(const std::filesystem::directory_entry& e) {
    DeviceEntry d;
    d.id = PathToUtf8(e.path());
    d.name = PathToUtf8(e.path().filename());
    std::error_code ec;
    d.isDirectory = e.is_directory(ec);
    if (!d.isDirectory) d.size = e.file_size(ec);
    d.modified = ModifiedTime(e.path());
    return d;
}

}  // namespace

FolderDeviceSource::FolderDeviceSource(std::filesystem::path root, std::string displayName)
    : root_(std::move(root)), displayName_(std::move(displayName)) {}

std::vector<DeviceEntry> FolderDeviceSource::Storages() {
    DeviceEntry e;
    e.id = PathToUtf8(root_);
    e.name = PathToUtf8(root_.filename());
    e.isDirectory = true;
    return {e};
}

std::vector<DeviceEntry> FolderDeviceSource::List(const DeviceEntry& folder) {
    std::vector<DeviceEntry> out;
    std::error_code ec;
    std::filesystem::directory_iterator it(LongPath(PathFromUtf8(folder.id)), ec), end;
    if (ec) {
        if (!IsConnected()) throw DeviceError(DeviceErrorKind::Disconnected, "Folder is gone");
        throw DeviceError(DeviceErrorKind::NotFound, "Cannot list " + folder.id + ": " + ec.message());
    }
    for (; it != end; it.increment(ec)) {
        if (ec) break;
        DeviceEntry e = MakeEntry(*it);
        e.id = PathToUtf8(PathFromUtf8(folder.id) / it->path().filename());
        out.push_back(std::move(e));
    }
    return out;
}

std::unique_ptr<IReadStream> FolderDeviceSource::Open(const DeviceEntry& file) {
    if (!IsConnected()) throw DeviceError(DeviceErrorKind::Disconnected, "Folder is gone");
    return std::make_unique<FileReadStream>(PathFromUtf8(file.id));
}

bool FolderDeviceSource::IsConnected() {
    std::error_code ec;
    return std::filesystem::is_directory(root_, ec);
}

}  // namespace ck
