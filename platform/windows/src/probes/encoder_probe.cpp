#include "../windows_translation.hpp"

#include <catro/platform/windows/capability_service.hpp>

#include <Windows.h>
#include <dxgi.h>
#include <mfapi.h>
#include <mfobjects.h>
#include <mftransform.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace catro::platform::windows {
namespace {

using Microsoft::WRL::ComPtr;

std::string utf8(std::wstring_view text) {
    if (text.empty()) {
        return {};
    }
    const auto length = static_cast<int>(text.size());
    const auto size = WideCharToMultiByte(CP_UTF8, 0, text.data(), length, nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return {};
    }
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), length, result.data(), size, nullptr, nullptr);
    return result;
}

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

struct Adapters {
    // Hardware transforms are enumerated per adapter so each one is tied to the adapter that owns it.
    std::vector<LUID> hardware;
    // Adapters with an output attached to the desktop: where duplicated frames are produced.
    std::vector<NativeLuid> with_outputs;
};

bool drives_desktop(IDXGIAdapter1& adapter) {
    ComPtr<IDXGIOutput> output;
    for (UINT index = 0; SUCCEEDED(adapter.EnumOutputs(index, &output)); ++index, output.Reset()) {
        DXGI_OUTPUT_DESC description{};
        if (SUCCEEDED(output->GetDesc(&description)) && description.AttachedToDesktop) {
            return true;
        }
    }
    return false;
}

std::optional<Adapters> enumerate_adapters(Errors& errors) {
    ComPtr<IDXGIFactory1> factory;
    if (const auto result = CreateDXGIFactory1(IID_PPV_ARGS(&factory)); FAILED(result)) {
        errors.remember(result);
        return std::nullopt;
    }
    Adapters adapters;
    ComPtr<IDXGIAdapter1> adapter;
    for (UINT index = 0; SUCCEEDED(factory->EnumAdapters1(index, &adapter)); ++index, adapter.Reset()) {
        DXGI_ADAPTER_DESC1 description{};
        if (const auto result = adapter->GetDesc1(&description); FAILED(result)) {
            errors.remember(result);
            continue;
        }
        if ((description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0) {
            adapters.hardware.push_back(description.AdapterLuid);
        }
        if (drives_desktop(*adapter.Get())) {
            adapters.with_outputs.push_back(
                {static_cast<std::uint32_t>(description.AdapterLuid.HighPart), description.AdapterLuid.LowPart});
        }
    }
    return adapters;
}

std::vector<caps::PixelFormat> input_formats(IMFActivate& activate) {
    std::vector<caps::PixelFormat> formats;
    UINT32 size = 0;
    if (FAILED(activate.GetBlobSize(MFT_INPUT_TYPES_Attributes, &size)) || size % sizeof(MFT_REGISTER_TYPE_INFO) != 0) {
        return formats;
    }
    std::vector<MFT_REGISTER_TYPE_INFO> types(size / sizeof(MFT_REGISTER_TYPE_INFO));
    if (types.empty() ||
        FAILED(activate.GetBlob(MFT_INPUT_TYPES_Attributes, reinterpret_cast<UINT8*>(types.data()), size, nullptr))) {
        return formats;
    }
    for (const auto& type : types) {
        if (type.guidSubtype == MFVideoFormat_NV12) {
            formats.push_back(caps::PixelFormat::nv12);
        } else if (type.guidSubtype == MFVideoFormat_P010) {
            formats.push_back(caps::PixelFormat::p010);
        }
    }
    return formats;
}

// Reads registration attributes only; the transform is never activated.
std::optional<NativeEncoder> describe(IMFActivate& activate, caps::Codec codec, std::optional<NativeLuid> adapter) {
    GUID clsid{};
    wchar_t text[64]{};
    if (FAILED(activate.GetGUID(MFT_TRANSFORM_CLSID_Attribute, &clsid)) ||
        StringFromGUID2(clsid, text, static_cast<int>(std::size(text))) == 0) {
        return std::nullopt;
    }
    NativeEncoder encoder{
        .codec = codec,
        .clsid = utf8(text),
        .hardware = adapter.has_value(),
        .adapter = adapter,
        .inputs = input_formats(activate),
    };
    wchar_t* name = nullptr;
    UINT32 length = 0;
    if (SUCCEEDED(activate.GetAllocatedString(MFT_FRIENDLY_NAME_Attribute, &name, &length))) {
        encoder.name = utf8(std::wstring_view(name, length));
        CoTaskMemFree(name);
    }
    return encoder;
}

struct Enumeration {
    std::vector<NativeEncoder> encoders;
    Errors errors;
    std::size_t attempts = 0;
    std::size_t failures = 0;
    bool unidentified = false;

    // Takes ownership of the activation array the enumeration call allocated.
    void collect(HRESULT result, IMFActivate** activates, UINT32 count, caps::Codec codec,
                 std::optional<NativeLuid> adapter) {
        ++attempts;
        if (FAILED(result)) {
            ++failures;
            errors.remember(result);
            return;
        }
        for (UINT32 index = 0; index < count; ++index) {
            if (auto encoder = describe(*activates[index], codec, adapter)) {
                encoders.push_back(std::move(*encoder));
            } else {
                unidentified = true;
            }
            activates[index]->Release();
        }
        CoTaskMemFree(activates);
    }
};

void enumerate(caps::Codec codec, const GUID& subtype, const std::optional<Adapters>& adapters, Enumeration& found) {
    const MFT_REGISTER_TYPE_INFO output{MFMediaType_Video, subtype};
    IMFActivate** activates = nullptr;
    UINT32 count = 0;
    auto result = MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER, MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_SORTANDFILTER, nullptr,
                            &output, &activates, &count);
    found.collect(result, activates, count, codec, std::nullopt);

    if (!adapters) {
        return;
    }
    for (const auto& luid : adapters->hardware) {
        ComPtr<IMFAttributes> attributes;
        result = MFCreateAttributes(&attributes, 1);
        if (SUCCEEDED(result)) {
            result = attributes->SetBlob(MFT_ENUM_ADAPTER_LUID, reinterpret_cast<const UINT8*>(&luid), sizeof(luid));
        }
        activates = nullptr;
        count = 0;
        if (SUCCEEDED(result)) {
            result = MFTEnum2(MFT_CATEGORY_VIDEO_ENCODER, MFT_ENUM_FLAG_HARDWARE | MFT_ENUM_FLAG_SORTANDFILTER,
                              nullptr, &output, attributes.Get(), &activates, &count);
        }
        found.collect(result, activates, count, codec,
                      NativeLuid{static_cast<std::uint32_t>(luid.HighPart), luid.LowPart});
    }
}

void add_issue(std::vector<caps::ProbeIssue>& issues, const std::string& probe_id, caps::IssueCode code) {
    const caps::ProbeIssue issue{probe_id, code};
    if (std::ranges::find(issues, issue) == issues.end()) {
        issues.push_back(issue);
    }
}

} // namespace

caps::ProbeFragment run_encoder_probe(const caps::ProbeSpec& spec) {
    const auto started = std::chrono::steady_clock::now();
    const auto apartment = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    Enumeration found;
    const auto adapters = enumerate_adapters(found.errors);
    if (const auto result = MFStartup(MF_VERSION, MFSTARTUP_LITE); FAILED(result)) {
        found.errors.remember(result);
    } else {
        const std::array codecs{
            std::pair{caps::Codec::h264, MFVideoFormat_H264},
            std::pair{caps::Codec::hevc, MFVideoFormat_HEVC},
            std::pair{caps::Codec::av1, MFVideoFormat_AV1},
        };
        for (const auto& [codec, subtype] : codecs) {
            enumerate(codec, subtype, adapters, found);
        }
        MFShutdown();
    }
    if (SUCCEEDED(apartment)) {
        CoUninitialize();
    }

    caps::ProbeFragment fragment{.probe_id = spec.probe_id, .family = spec.family, .revision = spec.revision};
    if (found.attempts == found.failures) {
        fragment.outcome = caps::ProbeOutcome::os_failure;
        add_issue(fragment.issues, spec.probe_id, caps::IssueCode::os_failure);
    } else {
        fragment.encoders = translate_encoders(found.encoders, adapters ? adapters->with_outputs : std::vector<NativeLuid>{},
                                               spec.probe_id, fragment.issues);
        // A failed adapter or codec enumeration leaves encoders unlisted.
        if (found.errors.any) {
            fragment.outcome = caps::ProbeOutcome::partial;
            add_issue(fragment.issues, spec.probe_id, caps::IssueCode::os_failure);
        }
        // Store-packaged transforms (the HEVC video extension, for one) carry no registered CLSID
        // and no documented stable identity, so they stay out of the inventory.
        if (found.unidentified) {
            fragment.outcome = caps::ProbeOutcome::partial;
            add_issue(fragment.issues, spec.probe_id, caps::IssueCode::not_reported);
        }
    }
    fragment.native_error = found.errors.first;
    fragment.duration = std::max(std::chrono::microseconds{1}, std::chrono::duration_cast<std::chrono::microseconds>(
                                                                   std::chrono::steady_clock::now() - started));
    return fragment;
}

} // namespace catro::platform::windows
