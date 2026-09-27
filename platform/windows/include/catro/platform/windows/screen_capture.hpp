#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <winrt/Windows.Graphics.Capture.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>

namespace catro::platform::windows {

enum class ScreenCaptureState : std::uint8_t {
    idle,
    running,
    source_closed,
    failed,
};

enum class ScreenCaptureErrorCode : std::uint8_t {
    unsupported,
    source_unavailable,
    device_creation_failed,
    capture_creation_failed,
    frame_failure,
};

struct ScreenCaptureError {
    ScreenCaptureErrorCode code = ScreenCaptureErrorCode::capture_creation_failed;
    std::int64_t native_code = 0;

    friend bool operator==(const ScreenCaptureError&, const ScreenCaptureError&) = default;
};

[[nodiscard]] constexpr const char* name(ScreenCaptureErrorCode code) noexcept {
    switch (code) {
    case ScreenCaptureErrorCode::unsupported:
        return "Windows Graphics Capture unsupported";
    case ScreenCaptureErrorCode::source_unavailable:
        return "capture source unavailable";
    case ScreenCaptureErrorCode::device_creation_failed:
        return "D3D11 capture device creation failed";
    case ScreenCaptureErrorCode::capture_creation_failed:
        return "capture session creation failed";
    case ScreenCaptureErrorCode::frame_failure:
        return "capture frame failure";
    }
    return "screen capture failure";
}

struct ScreenCaptureConfig {
    // Packed DXGI LUID (high 32 bits followed by low 32 bits). When present, capture must use
    // exactly this adapter; production policy can therefore keep WGC and the hardware encoder on
    // the same GPU. Diagnostics may omit it and use the OS high-performance preference.
    std::optional<std::uint64_t> adapter_luid;
};

struct GpuCaptureFrame {
    // WGC owns a small reusable surface pool. Keeping the projected frame alive is the lease that
    // prevents the texture from being recycled while the consumer/encoder still reads it.
    winrt::Windows::Graphics::Capture::Direct3D11CaptureFrame lease{nullptr};
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    std::uint64_t sequence = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
};

struct ScreenCaptureStatistics {
    ScreenCaptureState state = ScreenCaptureState::idle;
    std::optional<ScreenCaptureError> error;
    std::uint64_t frames_received = 0;
    std::uint64_t frames_published = 0;
    // A newer GPU frame replaced an older frame that the consumer had not taken yet.
    std::uint64_t mailbox_overwrites = 0;
    // The capture callback never blocks on the consumer. If the one-frame mailbox is busy,
    // the newest callback is dropped instead.
    std::uint64_t contention_drops = 0;
    std::uint64_t resize_events = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    std::uint64_t adapter_luid = 0;
};

// Windows Graphics Capture backed by a hardware D3D11 device.
//
// The capture callback never maps/copies pixels to CPU memory. It publishes only the newest
// ID3D11Texture2D into a one-frame mailbox. A future hardware encoder can consume that texture
// directly and stale frames cannot accumulate latency.
//
// Lifecycle methods are control-thread operations. wait_for_latest() is intended for exactly one
// worker consumer (diagnostics today, encoder worker next).
class WindowsGraphicsCapture final {
public:
    WindowsGraphicsCapture();
    ~WindowsGraphicsCapture();

    WindowsGraphicsCapture(const WindowsGraphicsCapture&) = delete;
    WindowsGraphicsCapture& operator=(const WindowsGraphicsCapture&) = delete;

    [[nodiscard]] std::optional<ScreenCaptureError> start_primary_display(
        const ScreenCaptureConfig& config = {});
    void stop() noexcept;

    [[nodiscard]] bool wait_for_latest(
        GpuCaptureFrame& frame, std::chrono::milliseconds timeout) noexcept;

    [[nodiscard]] ScreenCaptureStatistics statistics() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace catro::platform::windows
