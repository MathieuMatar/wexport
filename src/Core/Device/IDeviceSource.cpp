#include "IDeviceSource.h"

#include "../Util/Strings.h"

namespace ck {

std::optional<DeviceEntry> FindChild(const std::vector<DeviceEntry>& children, std::string_view name,
                                     bool wantDirectory) {
    // Exact match first; MTP is case-sensitive but people's phones aren't consistent.
    for (const auto& c : children)
        if (c.isDirectory == wantDirectory && c.name == name) return c;
    for (const auto& c : children)
        if (c.isDirectory == wantDirectory && EqualsIgnoreCase(c.name, name)) return c;
    return std::nullopt;
}

std::optional<DeviceEntry> ResolvePath(IDeviceSource& source, const DeviceEntry& start,
                                       const std::vector<std::string>& path) {
    DeviceEntry current = start;
    for (const auto& part : path) {
        auto next = FindChild(source.List(current), part, true);
        if (!next) return std::nullopt;
        current = *next;
    }
    return current;
}

}  // namespace ck
