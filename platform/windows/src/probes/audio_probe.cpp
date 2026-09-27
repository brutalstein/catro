#include "../text.hpp"
#include "../windows_translation.hpp"

#include <catro/platform/windows/capability_service.hpp>

#include <Windows.h>
// Defines the property keys and format subtypes below in this translation unit.
#include <initguid.h>
#include <mmdeviceapi.h>
// Needs the property key macros mmdeviceapi.h brings in.
#include <functiondiscoverykeys_devpkey.h>
#include <mmreg.h>
#include <ks.h>
#include <ksmedia.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace catro::platform::windows {
namespace {

using Microsoft::WRL::ComPtr;

// Keeps the first native failure code for the probe record.
struct Errors {
    std::optional<std::int64_t> first;
    bool any = false;

    void remember(std::int64_t code) {
        any = true;
        if (!first) {
            first = code;
        }
    }
};

std::optional<std::wstring> device_id(IMMDevice& device, Errors& errors) {
    wchar_t* id = nullptr;
    if (const auto result = device.GetId(&id); FAILED(result)) {
        errors.remember(result);
        return std::nullopt;
    }
    std::wstring value(id);
    CoTaskMemFree(id);
    return value;
}

// The engine format's container size names the sample layout; other encodings are not described.
std::optional<caps::SampleFormat> sample_format(const WAVEFORMATEXTENSIBLE& format, std::size_t size) {
    auto tag = format.Format.wFormatTag;
    if (tag == WAVE_FORMAT_EXTENSIBLE) {
        if (size < sizeof(WAVEFORMATEXTENSIBLE) ||
            format.Format.cbSize < sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) {
            return std::nullopt;
        }
        if (format.SubFormat == KSDATAFORMAT_SUBTYPE_PCM) {
            tag = WAVE_FORMAT_PCM;
        } else if (format.SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) {
            tag = WAVE_FORMAT_IEEE_FLOAT;
        } else {
            return std::nullopt;
        }
    }
    if (tag == WAVE_FORMAT_IEEE_FLOAT && format.Format.wBitsPerSample == 32) {
        return caps::SampleFormat::pcm_f32;
    }
    if (tag != WAVE_FORMAT_PCM) {
        return std::nullopt;
    }
    switch (format.Format.wBitsPerSample) {
    case 16:
        return caps::SampleFormat::pcm_s16;
    case 24:
        return caps::SampleFormat::pcm_s24;
    case 32:
        return caps::SampleFormat::pcm_s32;
    default:
        return std::nullopt;
    }
}

// Reads the property store only; no audio client is activated, so no stream opens and no
// microphone consent is requested.
void read_properties(IMMDevice& device, NativeAudioEndpoint& endpoint, Errors& errors) {
    ComPtr<IPropertyStore> store;
    if (const auto result = device.OpenPropertyStore(STGM_READ, &store); FAILED(result)) {
        errors.remember(result);
        return;
    }
    PROPVARIANT value;
    PropVariantInit(&value);
    if (SUCCEEDED(store->GetValue(PKEY_Device_FriendlyName, &value)) && value.vt == VT_LPWSTR &&
        value.pwszVal != nullptr) {
        endpoint.name = utf8(value.pwszVal);
    }
    PropVariantClear(&value);
    if (SUCCEEDED(store->GetValue(PKEY_AudioEngine_DeviceFormat, &value)) && value.vt == VT_BLOB &&
        value.blob.pBlobData != nullptr && value.blob.cbSize >= sizeof(WAVEFORMATEX)) {
        WAVEFORMATEXTENSIBLE format{};
        std::memcpy(&format, value.blob.pBlobData, std::min<std::size_t>(value.blob.cbSize, sizeof(format)));
        endpoint.channels = format.Format.nChannels;
        endpoint.sample_rate_hz = format.Format.nSamplesPerSec;
        endpoint.sample_format = sample_format(format, value.blob.cbSize);
    }
    PropVariantClear(&value);
}

std::optional<NativeAudioEndpoint> describe(IMMDevice& device, const std::wstring& id, Errors& errors) {
    ComPtr<IMMEndpoint> endpoint;
    EDataFlow flow{};
    DWORD state = 0;
    if (const auto result = device.QueryInterface(IID_PPV_ARGS(&endpoint)); FAILED(result)) {
        errors.remember(result);
        return std::nullopt;
    }
    if (const auto result = endpoint->GetDataFlow(&flow); FAILED(result)) {
        errors.remember(result);
        return std::nullopt;
    }
    if (const auto result = device.GetState(&state); FAILED(result)) {
        errors.remember(result);
        return std::nullopt;
    }
    NativeAudioEndpoint native{
        .id = utf8(id),
        .direction = flow == eCapture ? caps::AudioDirection::input : caps::AudioDirection::output,
        .state = state == DEVICE_STATE_ACTIVE     ? NativeEndpointState::active
                 : state == DEVICE_STATE_DISABLED ? NativeEndpointState::disabled
                                                  : NativeEndpointState::unplugged,
    };
    read_properties(device, native, errors);
    return native;
}

// Maps each default endpoint to its roles; absent when a default could not be determined.
std::optional<std::map<std::wstring, std::vector<caps::AudioRole>>> default_roles(IMMDeviceEnumerator& devices,
                                                                                 Errors& errors) {
    constexpr std::array roles{
        std::pair{eConsole, caps::AudioRole::console},
        std::pair{eMultimedia, caps::AudioRole::multimedia},
        std::pair{eCommunications, caps::AudioRole::communications},
    };
    std::map<std::wstring, std::vector<caps::AudioRole>> defaults;
    for (const auto flow : {eRender, eCapture}) {
        for (const auto& [role, mapped] : roles) {
            ComPtr<IMMDevice> device;
            const auto result = devices.GetDefaultAudioEndpoint(flow, role, &device);
            if (result == E_NOTFOUND) {
                continue;
            }
            if (FAILED(result)) {
                errors.remember(result);
                return std::nullopt;
            }
            auto id = device_id(*device.Get(), errors);
            if (!id) {
                return std::nullopt;
            }
            defaults[*id].push_back(mapped);
        }
    }
    return defaults;
}

std::optional<std::vector<NativeAudioEndpoint>> enumerate_endpoints(Errors& errors) {
    ComPtr<IMMDeviceEnumerator> devices;
    if (const auto result =
            CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&devices));
        FAILED(result)) {
        errors.remember(result);
        return std::nullopt;
    }
    ComPtr<IMMDeviceCollection> collection;
    if (const auto result = devices->EnumAudioEndpoints(
            eAll, DEVICE_STATE_ACTIVE | DEVICE_STATE_DISABLED | DEVICE_STATE_UNPLUGGED, &collection);
        FAILED(result)) {
        errors.remember(result);
        return std::nullopt;
    }
    UINT count = 0;
    if (const auto result = collection->GetCount(&count); FAILED(result)) {
        errors.remember(result);
        return std::nullopt;
    }

    const auto defaults = default_roles(*devices.Get(), errors);
    std::vector<NativeAudioEndpoint> endpoints;
    for (UINT index = 0; index < count; ++index) {
        ComPtr<IMMDevice> device;
        if (const auto result = collection->Item(index, &device); FAILED(result)) {
            errors.remember(result);
            continue;
        }
        const auto id = device_id(*device.Get(), errors);
        if (!id) {
            continue;
        }
        auto endpoint = describe(*device.Get(), *id, errors);
        if (!endpoint) {
            continue;
        }
        if (defaults) {
            const auto found = defaults->find(*id);
            endpoint->default_roles = found == defaults->end() ? std::vector<caps::AudioRole>{} : found->second;
        }
        endpoints.push_back(std::move(*endpoint));
    }
    return endpoints;
}

void add_issue(std::vector<caps::ProbeIssue>& issues, const std::string& probe_id, caps::IssueCode code) {
    const caps::ProbeIssue issue{probe_id, code};
    if (std::ranges::find(issues, issue) == issues.end()) {
        issues.push_back(issue);
    }
}

} // namespace

caps::ProbeFragment run_audio_probe(const caps::ProbeSpec& spec) {
    const auto started = std::chrono::steady_clock::now();
    const auto apartment = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    Errors errors;
    const auto endpoints = enumerate_endpoints(errors);
    if (SUCCEEDED(apartment)) {
        CoUninitialize();
    }

    caps::ProbeFragment fragment{.probe_id = spec.probe_id, .family = spec.family, .revision = spec.revision};
    if (!endpoints) {
        fragment.outcome = caps::ProbeOutcome::os_failure;
        add_issue(fragment.issues, spec.probe_id, caps::IssueCode::os_failure);
    } else {
        fragment.audio = translate_audio(*endpoints, spec.probe_id, fragment.issues);
        if (errors.any) {
            fragment.outcome = caps::ProbeOutcome::partial;
            add_issue(fragment.issues, spec.probe_id, caps::IssueCode::os_failure);
        }
    }
    fragment.native_error = errors.first;
    fragment.duration = std::max(std::chrono::microseconds{1}, std::chrono::duration_cast<std::chrono::microseconds>(
                                                                   std::chrono::steady_clock::now() - started));
    return fragment;
}

} // namespace catro::platform::windows
