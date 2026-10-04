#pragma once
// Phones over USB via Windows Portable Devices (MTP). Windows only.
// Read-only: only enumeration, property reads and GetStream(STGM_READ).

#ifdef _WIN32

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "IDeviceSource.h"

namespace ck {

struct WpdDeviceInfo {
    std::wstring pnpId;
    std::string friendlyName;  // "Galaxy J6"
    std::string manufacturer;  // "Samsung"
    std::string description;   // often the model
    int storageCount = -1;     // -1 = could not open; 0 = locked / not in File transfer mode
};

// Lists connected portable devices and how many storages each exposes.
// Must be called on an MTA thread (or one with COM initialized).
std::vector<WpdDeviceInfo> EnumerateWpdDevices();

class WpdDeviceSource final : public IDeviceSource {
public:
    explicit WpdDeviceSource(WpdDeviceInfo info);
    ~WpdDeviceSource() override;

    std::string DisplayName() const override { return info_.friendlyName; }
    std::vector<DeviceEntry> Storages() override;
    std::vector<DeviceEntry> List(const DeviceEntry& folder) override;
    std::unique_ptr<IReadStream> Open(const DeviceEntry& file) override;
    bool IsConnected() override;
    bool TryReconnect() override;

    const WpdDeviceInfo& Info() const { return info_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    WpdDeviceInfo info_;
};

}  // namespace ck

#endif
