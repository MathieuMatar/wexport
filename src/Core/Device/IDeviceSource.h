#pragma once
// Read-only view of a phone (or a folder shaped like one). The app never
// writes to, renames or deletes anything through this interface.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace ck {

struct DeviceEntry {
    std::string id;          // opaque, source-specific (WPD object id, or a UTF-8 path)
    std::string name;        // file or folder name as shown on the phone
    bool isDirectory = false;
    std::uint64_t size = 0;
    std::int64_t modified = 0;  // Unix seconds, 0 if unknown
};

enum class DeviceErrorKind { Disconnected, NotFound, Io };

struct DeviceError : std::runtime_error {
    DeviceErrorKind kind;
    DeviceError(DeviceErrorKind k, const std::string& what) : std::runtime_error(what), kind(k) {}
};

class IReadStream {
public:
    virtual ~IReadStream() = default;
    // Returns 0 at end of file. Throws DeviceError.
    virtual std::size_t Read(void* buffer, std::size_t size) = 0;
};

class IDeviceSource {
public:
    virtual ~IDeviceSource() = default;

    virtual std::string DisplayName() const = 0;

    // Top-level storages ("Internal storage", "SD card"). A folder source has one.
    virtual std::vector<DeviceEntry> Storages() = 0;

    // Children of a folder. Throws DeviceError.
    virtual std::vector<DeviceEntry> List(const DeviceEntry& folder) = 0;

    // Opens a file for reading. Throws DeviceError.
    virtual std::unique_ptr<IReadStream> Open(const DeviceEntry& file) = 0;

    // False once the device is unplugged (or the folder vanished).
    virtual bool IsConnected() = 0;

    // Called after a disconnect; returns true once the same device is back
    // and usable again. Entries may have to be looked up again afterwards.
    virtual bool TryReconnect() = 0;
};

// Walks `path` (folder names) down from `start`, matching names case-insensitively.
std::optional<DeviceEntry> ResolvePath(IDeviceSource& source, const DeviceEntry& start,
                                       const std::vector<std::string>& path);

std::optional<DeviceEntry> FindChild(const std::vector<DeviceEntry>& children, std::string_view name,
                                     bool wantDirectory);

}  // namespace ck
