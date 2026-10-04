#ifdef _WIN32

#include "WpdDeviceSource.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <PortableDevice.h>
#include <PortableDeviceApi.h>
#include <propvarutil.h>
#include <wrl/client.h>

#include <algorithm>
#include <initializer_list>
#include <cstdio>

#include "../Util/Strings.h"

#pragma comment(lib, "PortableDeviceGuids.lib")
#pragma comment(lib, "propsys.lib")

namespace ck {

using Microsoft::WRL::ComPtr;

namespace {

constexpr DWORD kBatch = 256;

// COM for the calling thread, if the caller hasn't set it up.
struct ComScope {
    HRESULT hr;
    ComScope() : hr(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
    ~ComScope() {
        if (SUCCEEDED(hr)) CoUninitialize();
    }
};

std::string FromWide(const wchar_t* s) { return s ? ToUtf8(s) : std::string(); }

std::string Hex(HRESULT hr) {
    char buf[16];
    snprintf(buf, sizeof buf, "0x%08lX", static_cast<unsigned long>(hr));
    return buf;
}

bool IsDisconnectError(HRESULT hr) {
    return hr == HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_CONNECTED) || hr == HRESULT_FROM_WIN32(ERROR_GEN_FAILURE) ||
           hr == HRESULT_FROM_WIN32(ERROR_NOT_READY) || hr == HRESULT_FROM_WIN32(ERROR_SEM_TIMEOUT) ||
           hr == HRESULT_FROM_WIN32(ERROR_DEVICE_REMOVED) || hr == HRESULT_FROM_WIN32(ERROR_NO_SUCH_DEVICE) ||
           hr == HRESULT_FROM_WIN32(ERROR_INVALID_HANDLE) ||
           hr == HRESULT_FROM_WIN32(ERROR_BAD_COMMAND) || hr == RPC_E_DISCONNECTED;
}

std::wstring DeviceString(IPortableDeviceManager* mgr, const wchar_t* id,
                          HRESULT (STDMETHODCALLTYPE IPortableDeviceManager::*getter)(LPCWSTR, WCHAR*, DWORD*)) {
    DWORD len = 0;
    if (FAILED((mgr->*getter)(id, nullptr, &len)) || len == 0) return L"";
    std::wstring s(len, L'\0');
    if (FAILED((mgr->*getter)(id, s.data(), &len))) return L"";
    s.resize(wcsnlen(s.c_str(), s.size()));
    return s;
}

ComPtr<IPortableDeviceManager> CreateManager() {
    ComPtr<IPortableDeviceManager> mgr;
    CoCreateInstance(CLSID_PortableDeviceManager, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&mgr));
    return mgr;
}

std::vector<std::wstring> DeviceIds(IPortableDeviceManager* mgr) {
    std::vector<std::wstring> ids;
    if (!mgr) return ids;
    mgr->RefreshDeviceList();
    DWORD count = 0;
    if (FAILED(mgr->GetDevices(nullptr, &count)) || count == 0) return ids;
    std::vector<PWSTR> raw(count);
    if (SUCCEEDED(mgr->GetDevices(raw.data(), &count))) {
        for (DWORD i = 0; i < count; ++i) {
            ids.emplace_back(raw[i]);
            CoTaskMemFree(raw[i]);
        }
    }
    return ids;
}

ComPtr<IPortableDevice> OpenDevice(const std::wstring& pnpId, HRESULT* result = nullptr) {
    ComPtr<IPortableDeviceValues> client;
    CoCreateInstance(CLSID_PortableDeviceValues, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&client));
    if (client) {
        client->SetStringValue(WPD_CLIENT_NAME, L"ChatKeeper");
        client->SetUnsignedIntegerValue(WPD_CLIENT_MAJOR_VERSION, 1);
        client->SetUnsignedIntegerValue(WPD_CLIENT_MINOR_VERSION, 0);
        client->SetUnsignedIntegerValue(WPD_CLIENT_REVISION, 0);
        client->SetUnsignedIntegerValue(WPD_CLIENT_SECURITY_QUALITY_OF_SERVICE, SECURITY_IMPERSONATION);
        client->SetUnsignedIntegerValue(WPD_CLIENT_DESIRED_ACCESS, GENERIC_READ);
    }
    ComPtr<IPortableDevice> device;
    // The free-threaded variant can be used from any MTA thread.
    HRESULT hr = CoCreateInstance(CLSID_PortableDeviceFTM, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&device));
    if (SUCCEEDED(hr)) hr = device->Open(pnpId.c_str(), client.Get());
    if (hr == E_ACCESSDENIED && client) {
        // Some devices refuse read-only opens from older drivers; ask for read/write but never write.
        client->SetUnsignedIntegerValue(WPD_CLIENT_DESIRED_ACCESS, GENERIC_READ | GENERIC_WRITE);
        hr = device->Open(pnpId.c_str(), client.Get());
    }
    if (result) *result = hr;
    return SUCCEEDED(hr) ? device : nullptr;
}

std::vector<std::wstring> ChildIds(IPortableDeviceContent* content, const wchar_t* parent, HRESULT* failure) {
    std::vector<std::wstring> ids;
    ComPtr<IEnumPortableDeviceObjectIDs> en;
    HRESULT hr = content->EnumObjects(0, parent, nullptr, &en);
    if (FAILED(hr)) {
        if (failure) *failure = hr;
        return ids;
    }
    for (;;) {
        PWSTR batch[kBatch] = {};
        DWORD fetched = 0;
        hr = en->Next(kBatch, batch, &fetched);
        for (DWORD i = 0; i < fetched; ++i) {
            ids.emplace_back(batch[i]);
            CoTaskMemFree(batch[i]);
        }
        if (FAILED(hr)) {
            if (failure) *failure = hr;
            break;
        }
        if (hr == S_FALSE || fetched == 0) break;
    }
    return ids;
}

std::int64_t VariantDateToUnix(const PROPVARIANT& v) {
    if (v.vt != VT_DATE) return 0;
    SYSTEMTIME local{}, utc{};
    if (!VariantTimeToSystemTime(v.date, &local)) return 0;
    // MTP reports local wall-clock time.
    if (!TzSpecificLocalTimeToSystemTime(nullptr, &local, &utc)) utc = local;
    FILETIME ft{};
    if (!SystemTimeToFileTime(&utc, &ft)) return 0;
    ULARGE_INTEGER t;
    t.LowPart = ft.dwLowDateTime;
    t.HighPart = ft.dwHighDateTime;
    return static_cast<std::int64_t>((t.QuadPart - 116444736000000000ull) / 10000000ull);
}

class WpdReadStream final : public IReadStream {
public:
    explicit WpdReadStream(ComPtr<IStream> s) : stream_(std::move(s)) {}
    std::size_t Read(void* buffer, std::size_t size) override {
        ULONG read = 0;
        HRESULT hr = stream_->Read(buffer, static_cast<ULONG>(std::min<std::size_t>(size, 1u << 30)), &read);
        if (FAILED(hr)) {
            throw DeviceError(IsDisconnectError(hr) ? DeviceErrorKind::Disconnected : DeviceErrorKind::Io,
                              "Read failed (" + Hex(hr) + ")");
        }
        return read;
    }

private:
    ComPtr<IStream> stream_;
};

}  // namespace

std::vector<WpdDeviceInfo> EnumerateWpdDevices() {
    ComScope com;
    std::vector<WpdDeviceInfo> out;
    auto mgr = CreateManager();
    for (const auto& id : DeviceIds(mgr.Get())) {
        WpdDeviceInfo info;
        info.pnpId = id;
        info.friendlyName = ToUtf8(DeviceString(mgr.Get(), id.c_str(), &IPortableDeviceManager::GetDeviceFriendlyName));
        info.manufacturer = ToUtf8(DeviceString(mgr.Get(), id.c_str(), &IPortableDeviceManager::GetDeviceManufacturer));
        info.description = ToUtf8(DeviceString(mgr.Get(), id.c_str(), &IPortableDeviceManager::GetDeviceDescription));
        if (info.friendlyName.empty()) info.friendlyName = info.description.empty() ? "Phone" : info.description;
        if (auto device = OpenDevice(id)) {
            ComPtr<IPortableDeviceContent> content;
            if (SUCCEEDED(device->Content(&content))) {
                HRESULT failure = S_OK;
                auto storages = ChildIds(content.Get(), WPD_DEVICE_OBJECT_ID, &failure);
                info.storageCount = static_cast<int>(storages.size());
            }
            device->Close();
        }
        out.push_back(std::move(info));
    }
    return out;
}

struct WpdDeviceSource::Impl {
    std::mutex mutex;
    ComPtr<IPortableDevice> device;
    ComPtr<IPortableDeviceContent> content;
    ComPtr<IPortableDeviceProperties> properties;
    ComPtr<IPortableDeviceResources> resources;
    ComPtr<IPortableDeviceKeyCollection> keys;

    bool Connect(const std::wstring& pnpId) {
        Reset();
        HRESULT hr = S_OK;
        device = OpenDevice(pnpId, &hr);
        if (!device) return false;
        if (FAILED(device->Content(&content)) || FAILED(content->Properties(&properties)) ||
            FAILED(content->Transfer(&resources))) {
            Reset();
            return false;
        }
        CoCreateInstance(CLSID_PortableDeviceKeyCollection, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&keys));
        for (const auto& k : {WPD_OBJECT_ORIGINAL_FILE_NAME, WPD_OBJECT_NAME, WPD_OBJECT_CONTENT_TYPE,
                              WPD_OBJECT_SIZE, WPD_OBJECT_DATE_MODIFIED})
            keys->Add(k);
        return true;
    }
    void Reset() {
        if (device) device->Close();
        keys.Reset();
        resources.Reset();
        properties.Reset();
        content.Reset();
        device.Reset();
    }
};

WpdDeviceSource::WpdDeviceSource(WpdDeviceInfo info) : impl_(std::make_unique<Impl>()), info_(std::move(info)) {
    ComScope com;
    impl_->Connect(info_.pnpId);
}

WpdDeviceSource::~WpdDeviceSource() {
    ComScope com;
    impl_->Reset();
}

std::vector<DeviceEntry> WpdDeviceSource::Storages() {
    DeviceEntry root;
    root.id = ToUtf8(WPD_DEVICE_OBJECT_ID);
    root.isDirectory = true;
    auto all = List(root);
    std::vector<DeviceEntry> storages;
    for (auto& e : all)
        if (e.isDirectory) storages.push_back(std::move(e));
    return storages;
}

std::vector<DeviceEntry> WpdDeviceSource::List(const DeviceEntry& folder) {
    ComScope com;
    std::lock_guard lock(impl_->mutex);
    if (!impl_->content) throw DeviceError(DeviceErrorKind::Disconnected, "The phone is not connected");
    HRESULT failure = S_OK;
    std::wstring parent = ToWide(folder.id);
    auto ids = ChildIds(impl_->content.Get(), parent.c_str(), &failure);
    if (FAILED(failure) && ids.empty()) {
        bool gone = IsDisconnectError(failure) || !IsConnected();
        throw DeviceError(gone ? DeviceErrorKind::Disconnected : DeviceErrorKind::Io,
                          "Cannot list a folder on the phone (" + Hex(failure) + ")");
    }
    std::vector<DeviceEntry> out;
    out.reserve(ids.size());
    for (const auto& id : ids) {
        ComPtr<IPortableDeviceValues> values;
        HRESULT hr = impl_->properties->GetValues(id.c_str(), impl_->keys.Get(), &values);
        if (FAILED(hr) || !values) {
            if (IsDisconnectError(hr)) throw DeviceError(DeviceErrorKind::Disconnected, "The phone was disconnected");
            continue;
        }
        DeviceEntry e;
        e.id = ToUtf8(id);
        PWSTR name = nullptr;
        if (SUCCEEDED(values->GetStringValue(WPD_OBJECT_ORIGINAL_FILE_NAME, &name)) && name && *name) {
            e.name = FromWide(name);
        } else {
            CoTaskMemFree(name);
            name = nullptr;
            if (SUCCEEDED(values->GetStringValue(WPD_OBJECT_NAME, &name))) e.name = FromWide(name);
        }
        CoTaskMemFree(name);
        GUID type = GUID_NULL;
        values->GetGuidValue(WPD_OBJECT_CONTENT_TYPE, &type);
        e.isDirectory = IsEqualGUID(type, WPD_CONTENT_TYPE_FOLDER) || IsEqualGUID(type, WPD_CONTENT_TYPE_FUNCTIONAL_OBJECT);
        ULONGLONG size = 0;
        if (SUCCEEDED(values->GetUnsignedLargeIntegerValue(WPD_OBJECT_SIZE, &size))) e.size = size;
        PROPVARIANT date;
        PropVariantInit(&date);
        if (SUCCEEDED(values->GetValue(WPD_OBJECT_DATE_MODIFIED, &date))) e.modified = VariantDateToUnix(date);
        PropVariantClear(&date);
        if (!e.name.empty()) out.push_back(std::move(e));
    }
    return out;
}

std::unique_ptr<IReadStream> WpdDeviceSource::Open(const DeviceEntry& file) {
    ComScope com;
    std::lock_guard lock(impl_->mutex);
    if (!impl_->resources) throw DeviceError(DeviceErrorKind::Disconnected, "The phone is not connected");
    ComPtr<IStream> stream;
    DWORD optimal = 0;
    std::wstring id = ToWide(file.id);
    HRESULT hr = impl_->resources->GetStream(id.c_str(), WPD_RESOURCE_DEFAULT, STGM_READ, &optimal, &stream);
    if (FAILED(hr)) {
        bool gone = IsDisconnectError(hr);
        throw DeviceError(gone ? DeviceErrorKind::Disconnected : DeviceErrorKind::Io,
                          "Cannot open the file on the phone (" + Hex(hr) + ")");
    }
    return std::make_unique<WpdReadStream>(std::move(stream));
}

bool WpdDeviceSource::IsConnected() {
    ComScope com;
    auto mgr = CreateManager();
    for (const auto& id : DeviceIds(mgr.Get()))
        if (_wcsicmp(id.c_str(), info_.pnpId.c_str()) == 0) return true;
    return false;
}

bool WpdDeviceSource::TryReconnect() {
    if (!IsConnected()) return false;
    ComScope com;
    std::lock_guard lock(impl_->mutex);
    if (!impl_->Connect(info_.pnpId)) return false;
    HRESULT failure = S_OK;
    return !ChildIds(impl_->content.Get(), WPD_DEVICE_OBJECT_ID, &failure).empty();
}

}  // namespace ck

#endif
