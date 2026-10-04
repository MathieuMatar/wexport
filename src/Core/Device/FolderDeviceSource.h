#pragma once
// A "device" backed by a local folder: used by "I already copied the WhatsApp
// folder to this PC" and by the automated tests.

#include <filesystem>

#include "IDeviceSource.h"

namespace ck {

class FolderDeviceSource final : public IDeviceSource {
public:
    explicit FolderDeviceSource(std::filesystem::path root, std::string displayName = "This PC");

    std::string DisplayName() const override { return displayName_; }
    std::vector<DeviceEntry> Storages() override;
    std::vector<DeviceEntry> List(const DeviceEntry& folder) override;
    std::unique_ptr<IReadStream> Open(const DeviceEntry& file) override;
    bool IsConnected() override;
    bool TryReconnect() override { return IsConnected(); }

    const std::filesystem::path& Root() const { return root_; }

private:
    std::filesystem::path root_;
    std::string displayName_;
};

}  // namespace ck
