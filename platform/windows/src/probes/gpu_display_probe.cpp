#include "../windows_translation.hpp"

#include <catro/platform/windows/capability_service.hpp>

#include <Windows.h>
#include <ShellScalingApi.h>
#include <d3d11.h>
#include <d3d12.h>
#include <dxcore.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <winrt/Windows.Graphics.Capture.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cwchar>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
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

NativeLuid native_luid(const LUID& luid) {
    return {static_cast<std::uint32_t>(luid.HighPart), luid.LowPart};
}

// Keeps the first native failure code for the probe record.
struct Errors {
    std::optional<std::int64_t> first;

    void remember(std::int64_t code) {
        if (!first) {
            first = code;
        }
    }
};

std::optional<NativeLuid> first_by_preference(IDXGIFactory6& factory, DXGI_GPU_PREFERENCE preference) {
    ComPtr<IDXGIAdapter1> adapter;
    DXGI_ADAPTER_DESC1 description{};
    if (FAILED(factory.EnumAdapterByGpuPreference(0, preference, IID_PPV_ARGS(&adapter))) ||
        FAILED(adapter->GetDesc1(&description))) {
        return std::nullopt;
    }
    return native_luid(description.AdapterLuid);
}

void add_dxcore_properties(std::vector<NativeAdapter>& adapters) {
    ComPtr<IDXCoreAdapterFactory> factory;
    if (FAILED(DXCoreCreateAdapterFactory(IID_PPV_ARGS(&factory)))) {
        return;
    }
    for (auto& adapter : adapters) {
        const LUID luid{adapter.luid.low, static_cast<LONG>(adapter.luid.high)};
        ComPtr<IDXCoreAdapter> core;
        if (FAILED(factory->GetAdapterByLuid(luid, IID_PPV_ARGS(&core)))) {
            continue;
        }
        const auto read = [&core](DXCoreAdapterProperty property) -> std::optional<bool> {
            bool value = false;
            if (core->IsPropertySupported(property) && SUCCEEDED(core->GetProperty(property, &value))) {
                return value;
            }
            return std::nullopt;
        };
        adapter.integrated = read(DXCoreAdapterProperty::IsIntegrated);
        adapter.detachable = read(DXCoreAdapterProperty::IsDetachable);
    }
}

// Enumerates adapters and, on the way, whether attached outputs expose duplication interfaces.
std::optional<std::vector<NativeAdapter>> enumerate_adapters(NativeCaptureApis& capture, Errors& errors) {
    ComPtr<IDXGIFactory1> factory;
    if (const auto result = CreateDXGIFactory1(IID_PPV_ARGS(&factory)); FAILED(result)) {
        errors.remember(result);
        return std::nullopt;
    }

    std::vector<NativeAdapter> adapters;
    ComPtr<IDXGIAdapter1> adapter;
    for (UINT index = 0; SUCCEEDED(factory->EnumAdapters1(index, &adapter)); ++index, adapter.Reset()) {
        DXGI_ADAPTER_DESC1 description{};
        if (const auto result = adapter->GetDesc1(&description); FAILED(result)) {
            errors.remember(result);
            continue;
        }
        // Null device pointers ask only whether the runtime and driver support the adapter.
        const bool direct3d11 = SUCCEEDED(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0,
                                                            nullptr, 0, D3D11_SDK_VERSION, nullptr, nullptr, nullptr));
        const bool direct3d12 =
            SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr));
        adapters.push_back(NativeAdapter{
            .luid = native_luid(description.AdapterLuid),
            .vendor_id = description.VendorId,
            .device_id = description.DeviceId,
            .description = utf8(std::wstring_view(description.Description,
                                                  wcsnlen(description.Description, std::size(description.Description)))),
            .software = (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0,
            .dedicated_memory = description.DedicatedVideoMemory,
            .shared_memory = description.SharedSystemMemory,
            .direct3d11 = direct3d11,
            .direct3d12 = direct3d12,
        });

        ComPtr<IDXGIOutput> output;
        for (UINT slot = 0; SUCCEEDED(adapter->EnumOutputs(slot, &output)); ++slot, output.Reset()) {
            ComPtr<IDXGIOutput1> duplication;
            ComPtr<IDXGIOutput5> duplication_hdr;
            const bool supported = SUCCEEDED(output.As(&duplication));
            capture.desktop_duplication = capture.desktop_duplication.value_or(false) || supported;
            capture.desktop_duplication_hdr = capture.desktop_duplication_hdr || SUCCEEDED(output.As(&duplication_hdr));
        }
    }

    ComPtr<IDXGIFactory6> ranked;
    if (SUCCEEDED(factory.As(&ranked))) {
        const auto minimum_power = first_by_preference(*ranked.Get(), DXGI_GPU_PREFERENCE_MINIMUM_POWER);
        const auto high_performance = first_by_preference(*ranked.Get(), DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE);
        for (auto& entry : adapters) {
            if (minimum_power) {
                entry.preferred_for_minimum_power = entry.luid == *minimum_power;
            }
            if (high_performance) {
                entry.preferred_for_high_performance = entry.luid == *high_performance;
            }
        }
    }
    add_dxcore_properties(adapters);
    return adapters;
}

std::optional<std::uint32_t> effective_dpi(std::wstring_view gdi_device) {
    struct Search {
        std::wstring_view device;
        HMONITOR monitor = nullptr;
    } search{gdi_device};
    EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR monitor, HDC, LPRECT, LPARAM data) -> BOOL {
            auto& state = *reinterpret_cast<Search*>(data);
            MONITORINFOEXW info{};
            info.cbSize = sizeof(info);
            if (GetMonitorInfoW(monitor, &info) && state.device == std::wstring_view(info.szDevice)) {
                state.monitor = monitor;
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&search));
    UINT x = 0;
    UINT y = 0;
    if (search.monitor == nullptr || FAILED(GetDpiForMonitor(search.monitor, MDT_EFFECTIVE_DPI, &x, &y)) || x == 0) {
        return std::nullopt;
    }
    return x;
}

// Windows 11 24H2 separates HDR from wide color; older builds report advanced color only.
void read_advanced_color(const DISPLAYCONFIG_PATH_TARGET_INFO& target, NativeDisplay& display) {
    DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO_2 current{};
    current.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO_2;
    current.header.size = sizeof(current);
    current.header.adapterId = target.adapterId;
    current.header.id = target.id;
    if (DisplayConfigGetDeviceInfo(&current.header) == ERROR_SUCCESS) {
        display.hdr_supported = current.highDynamicRangeSupported != 0;
        display.hdr_enabled = current.activeColorMode == DISPLAYCONFIG_ADVANCED_COLOR_MODE_HDR;
        display.bits_per_channel = static_cast<std::uint8_t>(std::min<UINT32>(current.bitsPerColorChannel, 255));
        return;
    }
    DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO legacy{};
    legacy.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO;
    legacy.header.size = sizeof(legacy);
    legacy.header.adapterId = target.adapterId;
    legacy.header.id = target.id;
    if (DisplayConfigGetDeviceInfo(&legacy.header) == ERROR_SUCCESS) {
        display.hdr_supported = legacy.advancedColorSupported != 0;
        display.hdr_enabled = legacy.advancedColorEnabled != 0;
        display.bits_per_channel = static_cast<std::uint8_t>(std::min<UINT32>(legacy.bitsPerColorChannel, 255));
    }
}

std::optional<std::vector<NativeDisplay>> enumerate_displays(Errors& errors) {
    std::vector<DISPLAYCONFIG_PATH_INFO> paths;
    std::vector<DISPLAYCONFIG_MODE_INFO> modes;
    LONG result = ERROR_SUCCESS;
    do {
        UINT32 path_count = 0;
        UINT32 mode_count = 0;
        result = GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &path_count, &mode_count);
        if (result != ERROR_SUCCESS) {
            break;
        }
        paths.resize(path_count);
        modes.resize(mode_count);
        result = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &path_count, paths.data(), &mode_count, modes.data(), nullptr);
        paths.resize(path_count);
        modes.resize(mode_count);
    } while (result == ERROR_INSUFFICIENT_BUFFER);
    if (result != ERROR_SUCCESS) {
        errors.remember(result);
        return std::nullopt;
    }

    std::vector<NativeDisplay> displays;
    for (const auto& path : paths) {
        NativeDisplay display{
            .adapter = native_luid(path.targetInfo.adapterId),
            .target_id = path.targetInfo.id,
            .refresh_numerator = path.targetInfo.refreshRate.Numerator,
            .refresh_denominator = path.targetInfo.refreshRate.Denominator,
        };
        const auto source_mode = path.sourceInfo.modeInfoIdx;
        if (source_mode != DISPLAYCONFIG_PATH_MODE_IDX_INVALID && source_mode < modes.size() &&
            modes[source_mode].infoType == DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE) {
            const auto& source = modes[source_mode].sourceMode;
            display.width = source.width;
            display.height = source.height;
            display.primary = source.position.x == 0 && source.position.y == 0;
        }
        DISPLAYCONFIG_SOURCE_DEVICE_NAME source_name{};
        source_name.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        source_name.header.size = sizeof(source_name);
        source_name.header.adapterId = path.sourceInfo.adapterId;
        source_name.header.id = path.sourceInfo.id;
        if (DisplayConfigGetDeviceInfo(&source_name.header) == ERROR_SUCCESS) {
            display.dpi = effective_dpi(source_name.viewGdiDeviceName);
        }
        read_advanced_color(path.targetInfo, display);
        displays.push_back(display);
    }
    return displays;
}

// Asks only whether capture is supported; never creates a capture item or shows a picker.
std::optional<bool> graphics_capture_supported() {
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
    } catch (const winrt::hresult_error&) {
        // The thread already joined an apartment; activation below still works in it.
    }
    try {
        return winrt::Windows::Graphics::Capture::GraphicsCaptureSession::IsSupported();
    } catch (const winrt::hresult_error&) {
        return std::nullopt;
    }
}

void add_issue(std::vector<caps::ProbeIssue>& issues, const std::string& probe_id, caps::IssueCode code) {
    const caps::ProbeIssue issue{probe_id, code};
    if (std::ranges::find(issues, issue) == issues.end()) {
        issues.push_back(issue);
    }
}

} // namespace

caps::ProbeFragment run_gpu_display_probe(const caps::ProbeSpec& spec) {
    const auto started = std::chrono::steady_clock::now();
    Errors errors;
    NativeGpuDisplay native;
    native.adapters = enumerate_adapters(native.capture, errors);
    native.displays = enumerate_displays(errors);
    native.capture.graphics_capture = graphics_capture_supported();

    caps::ProbeFragment fragment{.probe_id = spec.probe_id, .family = spec.family, .revision = spec.revision};
    if (!native.adapters && !native.displays) {
        fragment.outcome = caps::ProbeOutcome::os_failure;
        add_issue(fragment.issues, spec.probe_id, caps::IssueCode::os_failure);
    } else {
        fragment.gpu_display = translate_gpu_display(native, spec.probe_id, fragment.issues);
        if (!native.adapters || !native.displays) {
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
