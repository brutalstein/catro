#include <catro/platform/windows/screen_capture.hpp>

#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_6.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <wrl/client.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <winrt/Windows.Graphics.DirectX.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cctype>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace catro::platform::windows {
namespace {

using Microsoft::WRL::ComPtr;
namespace capture = winrt::Windows::Graphics::Capture;
namespace direct3d = winrt::Windows::Graphics::DirectX::Direct3D11;
namespace directx = winrt::Windows::Graphics::DirectX;

constexpr int kFramePoolBuffers = 2;

std::uint64_t pack_luid(LUID luid) noexcept {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(luid.HighPart)) << 32U) |
           static_cast<std::uint64_t>(luid.LowPart);
}

struct DeviceBundle {
    ComPtr<ID3D11Device> device;
    direct3d::IDirect3DDevice winrt_device{nullptr};
    std::uint64_t adapter_luid = 0;
};

struct DuplicationBundle {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGIOutput1> output;
    ComPtr<IDXGIOutputDuplication> duplication;
    std::uint64_t adapter_luid = 0;
    RECT desktop_bounds{};
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

std::variant<DeviceBundle, ScreenCaptureError> create_capture_device(
    const std::optional<std::uint64_t>& requested_luid) {
    ComPtr<IDXGIFactory6> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        return ScreenCaptureError{ScreenCaptureErrorCode::device_creation_failed};
    }

    ComPtr<IDXGIAdapter1> chosen;
    if (requested_luid) {
        LUID luid{};
        luid.LowPart = static_cast<DWORD>(*requested_luid & 0xffffffffULL);
        luid.HighPart = static_cast<LONG>((*requested_luid >> 32U) & 0xffffffffULL);
        const auto result = factory->EnumAdapterByLuid(luid, IID_PPV_ARGS(&chosen));
        if (FAILED(result) || !chosen) {
            return ScreenCaptureError{ScreenCaptureErrorCode::device_creation_failed, result};
        }
        DXGI_ADAPTER_DESC1 description{};
        if (FAILED(chosen->GetDesc1(&description)) ||
            (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) {
            return ScreenCaptureError{ScreenCaptureErrorCode::device_creation_failed};
        }
    }

    for (UINT index = 0; !chosen; ++index) {
        ComPtr<IDXGIAdapter1> candidate;
        const auto result = factory->EnumAdapterByGpuPreference(
            index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&candidate));
        if (result == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        if (FAILED(result)) {
            return ScreenCaptureError{ScreenCaptureErrorCode::device_creation_failed, result};
        }

        DXGI_ADAPTER_DESC1 description{};
        if (FAILED(candidate->GetDesc1(&description)) ||
            (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) {
            continue;
        }

        const auto probe = D3D11CreateDevice(
            candidate.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT,
            nullptr, 0, D3D11_SDK_VERSION, nullptr, nullptr, nullptr);
        if (SUCCEEDED(probe)) {
            chosen = std::move(candidate);
            break;
        }
    }

    // IDXGIFactory6 exists on supported Windows builds, but keep a hardware fallback for unusual
    // drivers where the preference enumeration cannot produce a usable adapter.
    if (!chosen && !requested_luid) {
        ComPtr<IDXGIFactory1> fallback_factory;
        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&fallback_factory)))) {
            return ScreenCaptureError{ScreenCaptureErrorCode::device_creation_failed};
        }
        for (UINT index = 0;; ++index) {
            ComPtr<IDXGIAdapter1> candidate;
            const auto result = fallback_factory->EnumAdapters1(index, &candidate);
            if (result == DXGI_ERROR_NOT_FOUND) {
                break;
            }
            if (FAILED(result)) {
                return ScreenCaptureError{ScreenCaptureErrorCode::device_creation_failed, result};
            }
            DXGI_ADAPTER_DESC1 description{};
            if (FAILED(candidate->GetDesc1(&description)) ||
                (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) {
                continue;
            }
            const auto probe = D3D11CreateDevice(
                candidate.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT,
                nullptr, 0, D3D11_SDK_VERSION, nullptr, nullptr, nullptr);
            if (SUCCEEDED(probe)) {
                chosen = std::move(candidate);
                break;
            }
        }
    }

    if (!chosen) {
        return ScreenCaptureError{ScreenCaptureErrorCode::device_creation_failed};
    }

    DXGI_ADAPTER_DESC1 description{};
    if (const auto result = chosen->GetDesc1(&description); FAILED(result)) {
        return ScreenCaptureError{ScreenCaptureErrorCode::device_creation_failed, result};
    }

    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
    ComPtr<ID3D11Device> device;
    const std::array levels{D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    auto result = D3D11CreateDevice(
        chosen.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags,
        levels.data(), static_cast<UINT>(levels.size()), D3D11_SDK_VERSION,
        &device, nullptr, nullptr);
    if (result == E_INVALIDARG) {
        result = D3D11CreateDevice(
            chosen.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags,
            &levels[1], 1, D3D11_SDK_VERSION, &device, nullptr, nullptr);
    }
    if (FAILED(result) || !device) {
        return ScreenCaptureError{ScreenCaptureErrorCode::device_creation_failed, result};
    }

    ComPtr<IDXGIDevice> dxgi_device;
    if (const auto query = device.As(&dxgi_device); FAILED(query)) {
        return ScreenCaptureError{ScreenCaptureErrorCode::device_creation_failed, query};
    }

    winrt::com_ptr<IInspectable> inspectable;
    if (const auto create = CreateDirect3D11DeviceFromDXGIDevice(
            dxgi_device.Get(), inspectable.put()); FAILED(create)) {
        return ScreenCaptureError{ScreenCaptureErrorCode::device_creation_failed, create};
    }

    return DeviceBundle{
        .device = std::move(device),
        .winrt_device = inspectable.as<direct3d::IDirect3DDevice>(),
        .adapter_luid = pack_luid(description.AdapterLuid),
    };
}

std::variant<DuplicationBundle, ScreenCaptureError> create_duplication_bundle(
    HMONITOR monitor) {
    if (monitor == nullptr) {
        return ScreenCaptureError{ScreenCaptureErrorCode::source_unavailable};
    }

    ComPtr<IDXGIFactory1> factory;
    auto result = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    if (FAILED(result) || !factory) {
        return ScreenCaptureError{ScreenCaptureErrorCode::device_creation_failed, result};
    }

    for (UINT adapter_index = 0;; ++adapter_index) {
        ComPtr<IDXGIAdapter1> adapter;
        result = factory->EnumAdapters1(adapter_index, &adapter);
        if (result == DXGI_ERROR_NOT_FOUND) break;
        if (FAILED(result) || !adapter) continue;

        DXGI_ADAPTER_DESC1 adapter_desc{};
        if (FAILED(adapter->GetDesc1(&adapter_desc)) ||
            (adapter_desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) {
            continue;
        }

        for (UINT output_index = 0;; ++output_index) {
            ComPtr<IDXGIOutput> output;
            result = adapter->EnumOutputs(output_index, &output);
            if (result == DXGI_ERROR_NOT_FOUND) break;
            if (FAILED(result) || !output) continue;

            DXGI_OUTPUT_DESC output_desc{};
            if (FAILED(output->GetDesc(&output_desc)) ||
                output_desc.Monitor != monitor) {
                continue;
            }

            UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT |
                         D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
            const std::array levels{
                D3D_FEATURE_LEVEL_11_1,
                D3D_FEATURE_LEVEL_11_0,
            };
            ComPtr<ID3D11Device> device;
            result = D3D11CreateDevice(
                adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags,
                levels.data(), static_cast<UINT>(levels.size()),
                D3D11_SDK_VERSION, &device, nullptr, nullptr);
            if (result == E_INVALIDARG) {
                result = D3D11CreateDevice(
                    adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags,
                    &levels[1], 1, D3D11_SDK_VERSION,
                    &device, nullptr, nullptr);
            }
            if (FAILED(result) || !device) {
                return ScreenCaptureError{
                    ScreenCaptureErrorCode::device_creation_failed, result};
            }

            ComPtr<IDXGIOutput1> output1;
            result = output.As(&output1);
            if (FAILED(result) || !output1) {
                return ScreenCaptureError{
                    ScreenCaptureErrorCode::capture_creation_failed, result};
            }

            ComPtr<IDXGIOutputDuplication> duplication;
            result = output1->DuplicateOutput(device.Get(), &duplication);
            if (FAILED(result) || !duplication) {
                return ScreenCaptureError{
                    ScreenCaptureErrorCode::capture_creation_failed, result};
            }

            ComPtr<ID3D11DeviceContext> context;
            device->GetImmediateContext(&context);
            if (!context) {
                return ScreenCaptureError{
                    ScreenCaptureErrorCode::device_creation_failed};
            }

            const auto width = output_desc.DesktopCoordinates.right -
                               output_desc.DesktopCoordinates.left;
            const auto height = output_desc.DesktopCoordinates.bottom -
                                output_desc.DesktopCoordinates.top;
            if (width <= 0 || height <= 0) {
                return ScreenCaptureError{
                    ScreenCaptureErrorCode::source_unavailable};
            }

            return DuplicationBundle{
                .device = std::move(device),
                .context = std::move(context),
                .output = std::move(output1),
                .duplication = std::move(duplication),
                .adapter_luid = pack_luid(adapter_desc.AdapterLuid),
                .desktop_bounds = output_desc.DesktopCoordinates,
                .width = static_cast<std::uint32_t>(width),
                .height = static_cast<std::uint32_t>(height),
            };
        }
    }
    return ScreenCaptureError{ScreenCaptureErrorCode::source_unavailable};
}

std::optional<ScreenCaptureError> recreate_duplication_on_device(
    HMONITOR monitor,
    ID3D11Device& device,
    ComPtr<IDXGIOutput1>& output1,
    ComPtr<IDXGIOutputDuplication>& duplication,
    RECT& desktop_bounds) noexcept {
    ComPtr<IDXGIDevice> dxgi_device;
    auto result = device.QueryInterface(IID_PPV_ARGS(&dxgi_device));
    if (FAILED(result) || !dxgi_device) {
        return ScreenCaptureError{ScreenCaptureErrorCode::device_creation_failed, result};
    }

    ComPtr<IDXGIAdapter> adapter;
    result = dxgi_device->GetAdapter(&adapter);
    if (FAILED(result) || !adapter) {
        return ScreenCaptureError{ScreenCaptureErrorCode::device_creation_failed, result};
    }

    for (UINT index = 0;; ++index) {
        ComPtr<IDXGIOutput> output;
        result = adapter->EnumOutputs(index, &output);
        if (result == DXGI_ERROR_NOT_FOUND) break;
        if (FAILED(result) || !output) {
            return ScreenCaptureError{ScreenCaptureErrorCode::capture_creation_failed, result};
        }

        DXGI_OUTPUT_DESC desc{};
        result = output->GetDesc(&desc);
        if (FAILED(result)) {
            return ScreenCaptureError{ScreenCaptureErrorCode::capture_creation_failed, result};
        }
        if (desc.Monitor != monitor) continue;

        ComPtr<IDXGIOutput1> refreshed_output;
        result = output.As(&refreshed_output);
        if (FAILED(result) || !refreshed_output) {
            return ScreenCaptureError{ScreenCaptureErrorCode::capture_creation_failed, result};
        }

        ComPtr<IDXGIOutputDuplication> refreshed_duplication;
        result = refreshed_output->DuplicateOutput(&device, &refreshed_duplication);
        if (FAILED(result) || !refreshed_duplication) {
            return ScreenCaptureError{ScreenCaptureErrorCode::capture_creation_failed, result};
        }

        output1 = std::move(refreshed_output);
        duplication = std::move(refreshed_duplication);
        desktop_bounds = desc.DesktopCoordinates;
        return std::nullopt;
    }

    return ScreenCaptureError{ScreenCaptureErrorCode::source_unavailable};
}

bool capture_client_rect(
    HWND window,
    RECT monitor_bounds,
    D3D11_TEXTURE2D_DESC const& desktop,
    D3D11_BOX& source_box) noexcept {
    if (window == nullptr || !IsWindow(window) || IsIconic(window)) return false;

    RECT client{};
    if (!GetClientRect(window, &client)) return false;
    POINT top_left{client.left, client.top};
    POINT bottom_right{client.right, client.bottom};
    if (!ClientToScreen(window, &top_left) ||
        !ClientToScreen(window, &bottom_right)) {
        return false;
    }

    const LONG left = std::max(top_left.x, monitor_bounds.left);
    const LONG top = std::max(top_left.y, monitor_bounds.top);
    const LONG right = std::min(bottom_right.x, monitor_bounds.right);
    const LONG bottom = std::min(bottom_right.y, monitor_bounds.bottom);
    if (right <= left || bottom <= top) return false;

    const auto local_left = static_cast<UINT>(left - monitor_bounds.left);
    const auto local_top = static_cast<UINT>(top - monitor_bounds.top);
    const auto local_right = static_cast<UINT>(right - monitor_bounds.left);
    const auto local_bottom = static_cast<UINT>(bottom - monitor_bounds.top);
    if (local_right > desktop.Width || local_bottom > desktop.Height ||
        local_left >= local_right || local_top >= local_bottom) {
        return false;
    }

    source_box = D3D11_BOX{
        .left = local_left,
        .top = local_top,
        .front = 0,
        .right = local_right,
        .bottom = local_bottom,
        .back = 1,
    };
    return true;
}

bool nearly_equal_edge(LONG left, LONG right) noexcept {
    constexpr LONG tolerance = 8;
    const auto delta = left >= right ? left - right : right - left;
    return delta <= tolerance;
}

bool window_is_fullscreen_like(HWND window, HMONITOR monitor) noexcept {
    if (window == nullptr || monitor == nullptr) return false;

    RECT candidate{};
    if (IsIconic(window)) {
        WINDOWPLACEMENT placement{};
        placement.length = sizeof(placement);
        if (!GetWindowPlacement(window, &placement)) return false;
        candidate = placement.rcNormalPosition;
    } else {
        RECT client{};
        if (!GetClientRect(window, &client)) return false;
        POINT top_left{client.left, client.top};
        POINT bottom_right{client.right, client.bottom};
        if (!ClientToScreen(window, &top_left) ||
            !ClientToScreen(window, &bottom_right)) {
            return false;
        }
        candidate = RECT{
            top_left.x,
            top_left.y,
            bottom_right.x,
            bottom_right.y,
        };
    }

    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info)) return false;

    return nearly_equal_edge(candidate.left, info.rcMonitor.left) &&
           nearly_equal_edge(candidate.top, info.rcMonitor.top) &&
           nearly_equal_edge(candidate.right, info.rcMonitor.right) &&
           nearly_equal_edge(candidate.bottom, info.rcMonitor.bottom);
}

std::string utf8(std::wstring_view wide) {
    if (wide.empty()) {
        return {};
    }
    const auto required = WideCharToMultiByte(
        CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
        nullptr, 0, nullptr, nullptr);
    if (required <= 0) {
        return {};
    }
    std::string output(static_cast<std::size_t>(required), '\0');
    const auto converted = WideCharToMultiByte(
        CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
        output.data(), required, nullptr, nullptr);
    return converted == required ? output : std::string{};
}

std::string process_image_name(DWORD process_id) noexcept {
    if (process_id == 0) {
        return {};
    }
    const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id);
    if (process == nullptr) {
        return {};
    }
    std::wstring path(32768, L'\0');
    DWORD length = static_cast<DWORD>(path.size());
    const BOOL queried = QueryFullProcessImageNameW(process, 0, path.data(), &length);
    CloseHandle(process);
    if (!queried || length == 0) {
        return {};
    }
    path.resize(static_cast<std::size_t>(length));
    const auto separator = path.find_last_of(L"\\/");
    const std::wstring_view base =
        separator == std::wstring::npos ? std::wstring_view{path}
                                        : std::wstring_view{path}.substr(separator + 1U);
    auto result = utf8(base);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return result;
}

std::variant<capture::GraphicsCaptureItem, ScreenCaptureError> capture_item_for_monitor(
    HMONITOR monitor) {
    if (monitor == nullptr) {
        return ScreenCaptureError{ScreenCaptureErrorCode::source_unavailable};
    }

    capture::GraphicsCaptureItem item{nullptr};
    try {
        auto interop =
            winrt::get_activation_factory<capture::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
        const auto result = interop->CreateForMonitor(
            monitor, __uuidof(ABI::Windows::Graphics::Capture::IGraphicsCaptureItem),
            reinterpret_cast<void**>(winrt::put_abi(item)));
        if (FAILED(result) || !item) {
            return ScreenCaptureError{ScreenCaptureErrorCode::source_unavailable, result};
        }
    } catch (const winrt::hresult_error& failure) {
        return ScreenCaptureError{
            ScreenCaptureErrorCode::source_unavailable,
            static_cast<std::int64_t>(failure.code().value),
        };
    }
    return item;
}

std::variant<capture::GraphicsCaptureItem, ScreenCaptureError> capture_item_for_window(
    HWND window) {
    if (window == nullptr || !IsWindow(window)) {
        return ScreenCaptureError{ScreenCaptureErrorCode::source_unavailable};
    }

    capture::GraphicsCaptureItem item{nullptr};
    try {
        auto interop =
            winrt::get_activation_factory<capture::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
        const auto result = interop->CreateForWindow(
            window, __uuidof(ABI::Windows::Graphics::Capture::IGraphicsCaptureItem),
            reinterpret_cast<void**>(winrt::put_abi(item)));
        if (FAILED(result) || !item) {
            return ScreenCaptureError{ScreenCaptureErrorCode::source_unavailable, result};
        }
    } catch (const winrt::hresult_error& failure) {
        return ScreenCaptureError{
            ScreenCaptureErrorCode::source_unavailable,
            static_cast<std::int64_t>(failure.code().value),
        };
    }
    return item;
}

std::variant<capture::GraphicsCaptureItem, ScreenCaptureError> capture_item_for_source(
    const CaptureSource& source) {
    if (source.native_handle == 0) {
        return ScreenCaptureError{ScreenCaptureErrorCode::source_unavailable};
    }

    if (source.kind == CaptureSourceKind::window) {
        return capture_item_for_window(
            reinterpret_cast<HWND>(source.native_handle));
    }
    return capture_item_for_monitor(
        reinterpret_cast<HMONITOR>(source.native_handle));
}

std::variant<capture::GraphicsCaptureItem, ScreenCaptureError> primary_display_item() {
    const auto monitor = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    return capture_item_for_monitor(monitor);
}

} // namespace

struct WindowsGraphicsCapture::Impl {
    struct CallbackGuard {
        explicit CallbackGuard(Impl& owner) noexcept : owner_(owner) {
            owner_.active_callbacks_.fetch_add(1, std::memory_order_acq_rel);
        }
        ~CallbackGuard() {
            if (owner_.active_callbacks_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                std::scoped_lock lock(owner_.callback_mutex_);
                owner_.callback_cv_.notify_all();
            }
        }
        Impl& owner_;
    };

    std::optional<ScreenCaptureError> start_primary_display(const ScreenCaptureConfig& config) {
        const auto monitor =
            MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
        if (config.backend == ScreenCaptureBackend::desktop_duplication) {
            return start_duplication(monitor, nullptr);
        }
        initialize_apartment();
        auto item_result = primary_display_item();
        return start_item(std::move(item_result), config);
    }

    std::optional<ScreenCaptureError> start_source(
        const CaptureSource& source,
        const ScreenCaptureConfig& config) {
        if (config.backend == ScreenCaptureBackend::desktop_duplication) {
            const auto monitor = reinterpret_cast<HMONITOR>(
                source.kind == CaptureSourceKind::display
                    ? source.native_handle
                    : source.monitor_handle);
            const auto window =
                source.kind == CaptureSourceKind::window
                    ? reinterpret_cast<HWND>(source.native_handle)
                    : nullptr;
            return start_duplication(monitor, window);
        }
        initialize_apartment();
        auto item_result = capture_item_for_source(source);
        return start_item(std::move(item_result), config);
    }

    void initialize_apartment() noexcept {
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
        } catch (const winrt::hresult_error&) {
            // WinUI callers already own an STA. WGC activation is valid from that apartment.
        }
    }

    std::optional<ScreenCaptureError> start_duplication(
        HMONITOR monitor, HWND source_window) {
        std::scoped_lock lifecycle_lock(lifecycle_mutex_);
        stop_locked();

        auto result = create_duplication_bundle(monitor);
        if (const auto* error = std::get_if<ScreenCaptureError>(&result)) {
            return fail(*error);
        }
        auto bundle = std::move(std::get<DuplicationBundle>(result));

        reset_counters();
        backend_.store(ScreenCaptureBackend::desktop_duplication,
                       std::memory_order_release);
        adapter_luid_.store(bundle.adapter_luid, std::memory_order_relaxed);
        width_.store(bundle.width, std::memory_order_relaxed);
        height_.store(bundle.height, std::memory_order_relaxed);
        format_.store(DXGI_FORMAT_B8G8R8A8_UNORM, std::memory_order_relaxed);
        stopping_.store(false, std::memory_order_release);

        d3d_device_ = std::move(bundle.device);
        duplication_context_ = std::move(bundle.context);
        duplication_output_ = std::move(bundle.output);
        duplication_ = std::move(bundle.duplication);
        duplication_monitor_ = monitor;
        duplication_window_ = source_window;
        duplication_desktop_bounds_ = bundle.desktop_bounds;
        duplication_slot_ = 0;
        for (auto& texture : duplication_textures_) texture.Reset();

        state_.store(ScreenCaptureState::running, std::memory_order_release);
        return std::nullopt;
    }

    std::optional<ScreenCaptureError> start_item(
        std::variant<capture::GraphicsCaptureItem, ScreenCaptureError> item_result,
        const ScreenCaptureConfig& config) {
        std::scoped_lock lifecycle_lock(lifecycle_mutex_);
        stop_locked();

        try {
            if (!capture::GraphicsCaptureSession::IsSupported()) {
                return fail(ScreenCaptureError{ScreenCaptureErrorCode::unsupported});
            }

            auto device_result = create_capture_device(config.adapter_luid);
            if (const auto* error = std::get_if<ScreenCaptureError>(&device_result)) {
                return fail(*error);
            }
            auto bundle = std::move(std::get<DeviceBundle>(device_result));

            if (const auto* error = std::get_if<ScreenCaptureError>(&item_result)) {
                return fail(*error);
            }
            auto item = std::get<capture::GraphicsCaptureItem>(std::move(item_result));
            const auto size = item.Size();
            if (size.Width <= 0 || size.Height <= 0) {
                return fail(ScreenCaptureError{ScreenCaptureErrorCode::source_unavailable});
            }

            auto pool = capture::Direct3D11CaptureFramePool::CreateFreeThreaded(
                bundle.winrt_device,
                directx::DirectXPixelFormat::B8G8R8A8UIntNormalized,
                kFramePoolBuffers,
                size);
            auto session = pool.CreateCaptureSession(item);
            if (config.borderless) {
                try {
                    session.IsBorderRequired(false);
                } catch (const winrt::hresult_error&) {
                    // Consent/capability denial is cosmetic; capture must continue normally.
                }
            }

            reset_counters();
            backend_.store(ScreenCaptureBackend::windows_graphics_capture,
                           std::memory_order_release);
            adapter_luid_.store(bundle.adapter_luid, std::memory_order_relaxed);
            width_.store(static_cast<std::uint32_t>(size.Width), std::memory_order_relaxed);
            height_.store(static_cast<std::uint32_t>(size.Height), std::memory_order_relaxed);
            stopping_.store(false, std::memory_order_release);

            d3d_device_ = std::move(bundle.device);
            winrt_device_ = std::move(bundle.winrt_device);
            item_ = std::move(item);
            pool_ = std::move(pool);
            session_ = std::move(session);
            current_size_ = size;

            frame_token_ = pool_.FrameArrived(
                [this](capture::Direct3D11CaptureFramePool const& sender, auto const&) {
                    CallbackGuard guard(*this);
                    on_frame(sender);
                });
            closed_token_ = item_.Closed([this](auto const&, auto const&) {
                CallbackGuard guard(*this);
                if (!stopping_.load(std::memory_order_acquire)) {
                    state_.store(ScreenCaptureState::source_closed, std::memory_order_release);
                    frame_cv_.notify_all();
                }
            });

            state_.store(ScreenCaptureState::running, std::memory_order_release);
            session_.StartCapture();
            return std::nullopt;
        } catch (const winrt::hresult_error& failure) {
            return fail(ScreenCaptureError{
                ScreenCaptureErrorCode::capture_creation_failed,
                static_cast<std::int64_t>(failure.code().value),
            });
        } catch (...) {
            return fail(ScreenCaptureError{ScreenCaptureErrorCode::capture_creation_failed});
        }
    }

    void stop() noexcept {
        std::scoped_lock lifecycle_lock(lifecycle_mutex_);
        stop_locked();
    }

    void stop_locked() noexcept {
        stopping_.store(true, std::memory_order_release);

        try {
            if (pool_ && frame_token_.value != 0) {
                pool_.FrameArrived(frame_token_);
            }
            if (item_ && closed_token_.value != 0) {
                item_.Closed(closed_token_);
            }
            if (session_) {
                session_.Close();
            }
            if (pool_) {
                pool_.Close();
            }
        } catch (...) {
        }

        frame_token_ = {};
        closed_token_ = {};

        // A callback that entered immediately before stopping_ changed may still be holding the
        // D3D/WinRT objects. Keep those objects alive until every in-flight callback has returned.
        {
            std::unique_lock callback_lock(callback_mutex_);
            callback_cv_.wait(callback_lock, [this] {
                return active_callbacks_.load(std::memory_order_acquire) == 0;
            });
        }

        session_ = nullptr;
        pool_ = nullptr;
        item_ = nullptr;
        winrt_device_ = nullptr;
        duplication_.Reset();
        duplication_output_.Reset();
        duplication_context_.Reset();
        duplication_monitor_ = nullptr;
        duplication_window_ = nullptr;
        duplication_desktop_bounds_ = {};
        for (auto& texture : duplication_textures_) texture.Reset();
        duplication_slot_ = 0;
        d3d_device_.Reset();

        {
            std::scoped_lock frame_lock(frame_mutex_);
            latest_ = {};
            has_latest_ = false;
        }
        frame_cv_.notify_all();

        error_code_.store(0, std::memory_order_relaxed);
        error_native_.store(0, std::memory_order_relaxed);
        if (state_.load(std::memory_order_acquire) != ScreenCaptureState::failed) {
            state_.store(ScreenCaptureState::idle, std::memory_order_release);
        }
    }

    std::optional<ScreenCaptureError> fail(ScreenCaptureError error) noexcept {
        error_code_.store(static_cast<int>(error.code) + 1, std::memory_order_relaxed);
        error_native_.store(error.native_code, std::memory_order_relaxed);
        state_.store(ScreenCaptureState::failed, std::memory_order_release);
        frame_cv_.notify_all();
        return error;
    }

    void fail_from_callback(ScreenCaptureError error) noexcept {
        error_code_.store(static_cast<int>(error.code) + 1, std::memory_order_relaxed);
        error_native_.store(error.native_code, std::memory_order_relaxed);
        state_.store(ScreenCaptureState::failed, std::memory_order_release);
        frame_cv_.notify_all();
    }

    void reset_counters() noexcept {
        sequence_.store(0, std::memory_order_relaxed);
        frames_received_.store(0, std::memory_order_relaxed);
        frames_published_.store(0, std::memory_order_relaxed);
        mailbox_overwrites_.store(0, std::memory_order_relaxed);
        contention_drops_.store(0, std::memory_order_relaxed);
        resize_events_.store(0, std::memory_order_relaxed);
        format_.store(DXGI_FORMAT_UNKNOWN, std::memory_order_relaxed);
        error_code_.store(0, std::memory_order_relaxed);
        error_native_.store(0, std::memory_order_relaxed);
        {
            std::scoped_lock frame_lock(frame_mutex_);
            latest_ = {};
            has_latest_ = false;
        }
    }

    void on_frame(capture::Direct3D11CaptureFramePool const& sender) noexcept {
        if (stopping_.load(std::memory_order_acquire) ||
            state_.load(std::memory_order_acquire) != ScreenCaptureState::running) {
            return;
        }

        try {
            auto frame = sender.TryGetNextFrame();
            if (!frame) {
                return;
            }
            frames_received_.fetch_add(1, std::memory_order_relaxed);

            const auto content_size = frame.ContentSize();
            if (content_size.Width <= 0 || content_size.Height <= 0) {
                return;
            }

            if (content_size.Width != current_size_.Width ||
                content_size.Height != current_size_.Height) {
                frame.Close();
                current_size_ = content_size;
                sender.Recreate(
                    winrt_device_,
                    directx::DirectXPixelFormat::B8G8R8A8UIntNormalized,
                    kFramePoolBuffers,
                    current_size_);
                width_.store(static_cast<std::uint32_t>(content_size.Width), std::memory_order_relaxed);
                height_.store(static_cast<std::uint32_t>(content_size.Height), std::memory_order_relaxed);
                resize_events_.fetch_add(1, std::memory_order_relaxed);
                return;
            }

            auto access =
                frame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
            ComPtr<ID3D11Texture2D> texture;
            const auto result = access->GetInterface(IID_PPV_ARGS(&texture));
            if (FAILED(result) || !texture) {
                fail_from_callback(ScreenCaptureError{ScreenCaptureErrorCode::frame_failure, result});
                return;
            }

            D3D11_TEXTURE2D_DESC description{};
            texture->GetDesc(&description);
            width_.store(description.Width, std::memory_order_relaxed);
            height_.store(description.Height, std::memory_order_relaxed);
            format_.store(description.Format, std::memory_order_relaxed);

            const auto sequence = sequence_.fetch_add(1, std::memory_order_relaxed) + 1;

            std::unique_lock frame_lock(frame_mutex_, std::try_to_lock);
            if (!frame_lock.owns_lock()) {
                contention_drops_.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            if (has_latest_) {
                mailbox_overwrites_.fetch_add(1, std::memory_order_relaxed);
            }
            latest_ = GpuCaptureFrame{
                .lease = std::move(frame),
                .texture = std::move(texture),
                .sequence = sequence,
                .width = description.Width,
                .height = description.Height,
                .format = description.Format,
            };
            has_latest_ = true;
            frames_published_.fetch_add(1, std::memory_order_relaxed);
            frame_lock.unlock();
            frame_cv_.notify_one();
        } catch (const winrt::hresult_error& failure) {
            fail_from_callback(ScreenCaptureError{
                ScreenCaptureErrorCode::frame_failure,
                static_cast<std::int64_t>(failure.code().value),
            });
        } catch (...) {
            fail_from_callback(ScreenCaptureError{ScreenCaptureErrorCode::frame_failure});
        }
    }

    bool wait_for_duplication(
        GpuCaptureFrame& output,
        std::chrono::milliseconds timeout) noexcept {
        std::scoped_lock lock(duplication_mutex_);
        if (!duplication_ || !duplication_context_ || !d3d_device_ ||
            state_.load(std::memory_order_acquire) != ScreenCaptureState::running) {
            return false;
        }

        DXGI_OUTDUPL_FRAME_INFO info{};
        ComPtr<IDXGIResource> resource;
        const auto timeout_ms = static_cast<UINT>(
            std::max<std::int64_t>(0, timeout.count()));
        auto acquired =
            duplication_->AcquireNextFrame(timeout_ms, &info, &resource);
        if (acquired == DXGI_ERROR_WAIT_TIMEOUT) return false;
        if (acquired == DXGI_ERROR_ACCESS_LOST) {
            duplication_.Reset();
            duplication_output_.Reset();
            for (auto& texture : duplication_textures_) texture.Reset();
            duplication_slot_ = 0;
            if (!d3d_device_ || duplication_monitor_ == nullptr) {
                fail_from_callback(ScreenCaptureError{
                    ScreenCaptureErrorCode::frame_failure, acquired});
                return false;
            }
            if (const auto recreated = recreate_duplication_on_device(
                    duplication_monitor_,
                    *d3d_device_.Get(),
                    duplication_output_,
                    duplication_,
                    duplication_desktop_bounds_)) {
                fail_from_callback(*recreated);
            } else {
                resize_events_.fetch_add(1, std::memory_order_relaxed);
            }
            return false;
        }
        if (FAILED(acquired) || !resource) {
            fail_from_callback(ScreenCaptureError{
                ScreenCaptureErrorCode::frame_failure, acquired});
            return false;
        }

        struct FrameGuard {
            IDXGIOutputDuplication* duplication = nullptr;
            ~FrameGuard() {
                if (duplication) (void)duplication->ReleaseFrame();
            }
        } guard{duplication_.Get()};

        ComPtr<ID3D11Texture2D> desktop;
        const auto query = resource.As(&desktop);
        if (FAILED(query) || !desktop) {
            fail_from_callback(ScreenCaptureError{
                ScreenCaptureErrorCode::frame_failure, query});
            return false;
        }

        D3D11_TEXTURE2D_DESC desc{};
        desktop->GetDesc(&desc);
        if (desc.Width == 0 || desc.Height == 0 ||
            desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM) {
            fail_from_callback(ScreenCaptureError{
                ScreenCaptureErrorCode::frame_failure});
            return false;
        }

        D3D11_BOX source_box{
            .left = 0,
            .top = 0,
            .front = 0,
            .right = desc.Width,
            .bottom = desc.Height,
            .back = 1,
        };
        if (duplication_window_ != nullptr) {
            D3D11_BOX cropped{};
            if (!capture_client_rect(
                    duplication_window_,
                    duplication_desktop_bounds_,
                    desc,
                    cropped)) {
                return false;
            }
            source_box = cropped;
        }

        const UINT output_width = source_box.right - source_box.left;
        const UINT output_height = source_box.bottom - source_box.top;

        auto& copy = duplication_textures_[duplication_slot_];
        bool recreate = !copy;
        if (copy) {
            D3D11_TEXTURE2D_DESC current{};
            copy->GetDesc(&current);
            recreate = current.Width != output_width ||
                       current.Height != output_height ||
                       current.Format != desc.Format;
        }
        if (recreate) {
            D3D11_TEXTURE2D_DESC target = desc;
            target.Width = output_width;
            target.Height = output_height;
            target.MipLevels = 1;
            target.ArraySize = 1;
            target.SampleDesc.Count = 1;
            target.SampleDesc.Quality = 0;
            target.Usage = D3D11_USAGE_DEFAULT;
            target.BindFlags = D3D11_BIND_SHADER_RESOURCE |
                               D3D11_BIND_RENDER_TARGET;
            target.CPUAccessFlags = 0;
            target.MiscFlags = 0;
            copy.Reset();
            const auto created =
                d3d_device_->CreateTexture2D(&target, nullptr, &copy);
            if (FAILED(created) || !copy) {
                fail_from_callback(ScreenCaptureError{
                    ScreenCaptureErrorCode::frame_failure, created});
                return false;
            }
        }

        duplication_context_->CopySubresourceRegion(
            copy.Get(), 0, 0, 0, 0, desktop.Get(), 0, &source_box);
        frames_received_.fetch_add(1, std::memory_order_relaxed);
        const auto sequence =
            sequence_.fetch_add(1, std::memory_order_relaxed) + 1;
        frames_published_.fetch_add(1, std::memory_order_relaxed);
        width_.store(output_width, std::memory_order_relaxed);
        height_.store(output_height, std::memory_order_relaxed);
        format_.store(desc.Format, std::memory_order_relaxed);

        output = GpuCaptureFrame{
            .lease = nullptr,
            .texture = copy,
            .sequence = sequence,
            .width = output_width,
            .height = output_height,
            .format = desc.Format,
        };
        duplication_slot_ =
            (duplication_slot_ + 1U) % duplication_textures_.size();
        return true;
    }

    bool wait_for_latest(GpuCaptureFrame& output, std::chrono::milliseconds timeout) noexcept {
        if (backend_.load(std::memory_order_acquire) ==
            ScreenCaptureBackend::desktop_duplication) {
            return wait_for_duplication(output, timeout);
        }

        std::unique_lock lock(frame_mutex_);
        frame_cv_.wait_for(lock, timeout, [this] {
            return has_latest_ ||
                   state_.load(std::memory_order_acquire) != ScreenCaptureState::running;
        });
        if (!has_latest_) return false;
        output = std::move(latest_);
        latest_ = {};
        has_latest_ = false;
        return true;
    }

    ScreenCaptureStatistics statistics() const noexcept {
        ScreenCaptureStatistics result;
        result.state = state_.load(std::memory_order_acquire);
        result.backend = backend_.load(std::memory_order_acquire);
        const auto encoded_error = error_code_.load(std::memory_order_relaxed);
        if (encoded_error > 0) {
            result.error = ScreenCaptureError{
                static_cast<ScreenCaptureErrorCode>(encoded_error - 1),
                error_native_.load(std::memory_order_relaxed),
            };
        }
        result.frames_received = frames_received_.load(std::memory_order_relaxed);
        result.frames_published = frames_published_.load(std::memory_order_relaxed);
        result.mailbox_overwrites = mailbox_overwrites_.load(std::memory_order_relaxed);
        result.contention_drops = contention_drops_.load(std::memory_order_relaxed);
        result.resize_events = resize_events_.load(std::memory_order_relaxed);
        result.width = width_.load(std::memory_order_relaxed);
        result.height = height_.load(std::memory_order_relaxed);
        result.format = format_.load(std::memory_order_relaxed);
        result.adapter_luid = adapter_luid_.load(std::memory_order_relaxed);
        return result;
    }

    mutable std::mutex lifecycle_mutex_;
    mutable std::mutex frame_mutex_;
    std::condition_variable frame_cv_;
    GpuCaptureFrame latest_;
    bool has_latest_ = false;

    std::mutex callback_mutex_;
    std::condition_variable callback_cv_;
    std::atomic<std::uint32_t> active_callbacks_{0};
    std::atomic_bool stopping_{true};

    ComPtr<ID3D11Device> d3d_device_;
    direct3d::IDirect3DDevice winrt_device_{nullptr};
    std::mutex duplication_mutex_;
    ComPtr<ID3D11DeviceContext> duplication_context_;
    ComPtr<IDXGIOutput1> duplication_output_;
    ComPtr<IDXGIOutputDuplication> duplication_;
    HMONITOR duplication_monitor_ = nullptr;
    HWND duplication_window_ = nullptr;
    RECT duplication_desktop_bounds_{};
    std::array<ComPtr<ID3D11Texture2D>, 2> duplication_textures_{};
    std::size_t duplication_slot_ = 0;
    capture::GraphicsCaptureItem item_{nullptr};
    capture::Direct3D11CaptureFramePool pool_{nullptr};
    capture::GraphicsCaptureSession session_{nullptr};
    winrt::event_token frame_token_{};
    winrt::event_token closed_token_{};
    winrt::Windows::Graphics::SizeInt32 current_size_{};

    std::atomic<ScreenCaptureState> state_{ScreenCaptureState::idle};
    std::atomic<ScreenCaptureBackend> backend_{
        ScreenCaptureBackend::windows_graphics_capture};
    std::atomic<int> error_code_{0};
    std::atomic<std::int64_t> error_native_{0};
    std::atomic<std::uint64_t> sequence_{0};
    std::atomic<std::uint64_t> frames_received_{0};
    std::atomic<std::uint64_t> frames_published_{0};
    std::atomic<std::uint64_t> mailbox_overwrites_{0};
    std::atomic<std::uint64_t> contention_drops_{0};
    std::atomic<std::uint64_t> resize_events_{0};
    std::atomic<std::uint32_t> width_{0};
    std::atomic<std::uint32_t> height_{0};
    std::atomic<DXGI_FORMAT> format_{DXGI_FORMAT_UNKNOWN};
    std::atomic<std::uint64_t> adapter_luid_{0};
};

std::vector<CaptureSource> enumerate_capture_sources() noexcept {
    std::vector<CaptureSource> sources;
    try {
        (void)EnumDisplayMonitors(
            nullptr, nullptr,
            [](HMONITOR monitor, HDC, LPRECT, LPARAM opaque) -> BOOL {
                auto& output =
                    *reinterpret_cast<std::vector<CaptureSource>*>(opaque);
                MONITORINFOEXW info{};
                info.cbSize = sizeof(info);
                if (!GetMonitorInfoW(monitor, &info)) {
                    return TRUE;
                }

                const auto width = info.rcMonitor.right - info.rcMonitor.left;
                const auto height = info.rcMonitor.bottom - info.rcMonitor.top;
                if (width <= 0 || height <= 0) {
                    return TRUE;
                }

                std::string title =
                    (info.dwFlags & MONITORINFOF_PRIMARY) != 0
                        ? "Primary display"
                        : utf8(info.szDevice);
                output.push_back(CaptureSource{
                    .kind = CaptureSourceKind::display,
                    .native_handle = reinterpret_cast<std::uintptr_t>(monitor),
                    .monitor_handle = reinterpret_cast<std::uintptr_t>(monitor),
                    .title = std::move(title),
                    .width = static_cast<std::uint32_t>(width),
                    .height = static_cast<std::uint32_t>(height),
                    .primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0,
                });
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&sources));

        (void)EnumWindows(
            [](HWND window, LPARAM opaque) -> BOOL {
                if (!IsWindowVisible(window) ||
                    window == GetShellWindow() ||
                    (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) != 0) {
                    return TRUE;
                }

                DWORD process_id = 0;
                (void)GetWindowThreadProcessId(window, &process_id);
                if (process_id == GetCurrentProcessId()) {
                    return TRUE;
                }

                const auto length = GetWindowTextLengthW(window);
                if (length <= 0 || length > 1024) {
                    return TRUE;
                }

                RECT bounds{};
                if (!GetWindowRect(window, &bounds)) {
                    return TRUE;
                }
                const auto width = bounds.right - bounds.left;
                const auto height = bounds.bottom - bounds.top;
                if (width <= 1 || height <= 1) {
                    return TRUE;
                }

                std::wstring title(
                    static_cast<std::size_t>(length) + 1U, L'\0');
                const auto written = GetWindowTextW(
                    window, title.data(), static_cast<int>(title.size()));
                if (written <= 0) {
                    return TRUE;
                }
                title.resize(static_cast<std::size_t>(written));

                const auto monitor =
                    MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
                const bool fullscreen_like =
                    window_is_fullscreen_like(window, monitor);

                auto& output =
                    *reinterpret_cast<std::vector<CaptureSource>*>(opaque);
                output.push_back(CaptureSource{
                    .kind = CaptureSourceKind::window,
                    .native_handle = reinterpret_cast<std::uintptr_t>(window),
                    .monitor_handle = reinterpret_cast<std::uintptr_t>(monitor),
                    .title = utf8(title),
                    .process_name = process_image_name(process_id),
                    .width = static_cast<std::uint32_t>(width),
                    .height = static_cast<std::uint32_t>(height),
                    .primary = false,
                    .fullscreen_like = fullscreen_like,
                });
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&sources));

        std::stable_sort(
            sources.begin(), sources.end(),
            [](const CaptureSource& left, const CaptureSource& right) {
                if (left.kind != right.kind) {
                    return left.kind == CaptureSourceKind::display;
                }
                if (left.kind == CaptureSourceKind::display &&
                    left.primary != right.primary) {
                    return left.primary;
                }
                return left.title < right.title;
            });
    } catch (...) {
        sources.clear();
    }
    return sources;
}

ScreenCaptureBackend recommended_capture_backend(
    const CaptureSource& source) noexcept {
    if (source.kind == CaptureSourceKind::window &&
        source.monitor_handle != 0 &&
        (source.fullscreen_like || source.process_name == "cs2.exe")) {
        return ScreenCaptureBackend::desktop_duplication;
    }
    return ScreenCaptureBackend::windows_graphics_capture;
}

WindowsGraphicsCapture::WindowsGraphicsCapture() : impl_(std::make_unique<Impl>()) {}

WindowsGraphicsCapture::~WindowsGraphicsCapture() {
    stop();
}

std::optional<ScreenCaptureError> WindowsGraphicsCapture::start_primary_display(
    const ScreenCaptureConfig& config) {
    return impl_->start_primary_display(config);
}

std::optional<ScreenCaptureError> WindowsGraphicsCapture::start_source(
    const CaptureSource& source,
    const ScreenCaptureConfig& config) {
    return impl_->start_source(source, config);
}

void WindowsGraphicsCapture::stop() noexcept {
    impl_->stop();
}

bool WindowsGraphicsCapture::wait_for_latest(
    GpuCaptureFrame& frame, std::chrono::milliseconds timeout) noexcept {
    return impl_->wait_for_latest(frame, timeout);
}

ScreenCaptureStatistics WindowsGraphicsCapture::statistics() const noexcept {
    return impl_->statistics();
}

} // namespace catro::platform::windows
