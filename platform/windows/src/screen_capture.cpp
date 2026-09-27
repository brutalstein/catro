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
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

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

std::variant<DeviceBundle, ScreenCaptureError> create_capture_device() {
    ComPtr<IDXGIFactory6> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        return ScreenCaptureError{ScreenCaptureErrorCode::device_creation_failed};
    }

    ComPtr<IDXGIAdapter1> chosen;
    for (UINT index = 0;; ++index) {
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
    if (!chosen) {
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
    D3D_FEATURE_LEVEL created_level{};
    const std::array levels{D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    auto result = D3D11CreateDevice(
        chosen.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags,
        levels.data(), static_cast<UINT>(levels.size()), D3D11_SDK_VERSION,
        &device, &created_level, nullptr);
    if (result == E_INVALIDARG) {
        result = D3D11CreateDevice(
            chosen.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags,
            &levels[1], 1, D3D11_SDK_VERSION, &device, &created_level, nullptr);
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

std::variant<capture::GraphicsCaptureItem, ScreenCaptureError> primary_display_item() {
    const auto monitor = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    if (monitor == nullptr) {
        return ScreenCaptureError{ScreenCaptureErrorCode::source_unavailable};
    }

    capture::GraphicsCaptureItem item{nullptr};
    try {
        auto interop =
            winrt::get_activation_factory<capture::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
        const auto result = interop->CreateForMonitor(
            monitor, winrt::guid_of<capture::GraphicsCaptureItem>(), winrt::put_abi(item));
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

    std::optional<ScreenCaptureError> start_primary_display() {
        std::scoped_lock lifecycle_lock(lifecycle_mutex_);
        stop_locked();

        try {
            try {
                winrt::init_apartment(winrt::apartment_type::multi_threaded);
            } catch (const winrt::hresult_error&) {
                // WinUI callers already own an STA. WGC activation is valid from that apartment.
            }

            if (!capture::GraphicsCaptureSession::IsSupported()) {
                return fail(ScreenCaptureError{ScreenCaptureErrorCode::unsupported});
            }

            auto device_result = create_capture_device();
            if (const auto* error = std::get_if<ScreenCaptureError>(&device_result)) {
                return fail(*error);
            }
            auto bundle = std::move(std::get<DeviceBundle>(device_result));

            auto item_result = primary_display_item();
            if (const auto* error = std::get_if<ScreenCaptureError>(&item_result)) {
                return fail(*error);
            }
            auto item = std::get<capture::GraphicsCaptureItem>(item_result);
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

            reset_counters();
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
        session_ = nullptr;
        pool_ = nullptr;
        item_ = nullptr;
        winrt_device_ = nullptr;
        d3d_device_.Reset();

        {
            std::unique_lock callback_lock(callback_mutex_);
            callback_cv_.wait(callback_lock, [this] {
                return active_callbacks_.load(std::memory_order_acquire) == 0;
            });
        }

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

            auto access = frame.Surface().as<IDirect3DDxgiInterfaceAccess>();
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

    bool wait_for_latest(GpuCaptureFrame& output, std::chrono::milliseconds timeout) noexcept {
        std::unique_lock lock(frame_mutex_);
        frame_cv_.wait_for(lock, timeout, [this] {
            return has_latest_ ||
                   state_.load(std::memory_order_acquire) != ScreenCaptureState::running;
        });
        if (!has_latest_) {
            return false;
        }
        output = std::move(latest_);
        latest_ = {};
        has_latest_ = false;
        return true;
    }

    ScreenCaptureStatistics statistics() const noexcept {
        ScreenCaptureStatistics result;
        result.state = state_.load(std::memory_order_acquire);
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
    capture::GraphicsCaptureItem item_{nullptr};
    capture::Direct3D11CaptureFramePool pool_{nullptr};
    capture::GraphicsCaptureSession session_{nullptr};
    winrt::event_token frame_token_{};
    winrt::event_token closed_token_{};
    winrt::Windows::Graphics::SizeInt32 current_size_{};

    std::atomic<ScreenCaptureState> state_{ScreenCaptureState::idle};
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

WindowsGraphicsCapture::WindowsGraphicsCapture() : impl_(std::make_unique<Impl>()) {}

WindowsGraphicsCapture::~WindowsGraphicsCapture() {
    stop();
}

std::optional<ScreenCaptureError> WindowsGraphicsCapture::start_primary_display() {
    return impl_->start_primary_display();
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
