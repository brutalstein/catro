#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace catro::platform::macos {

enum class ScreenCaptureState : std::uint8_t {
    idle,
    running,
    source_closed,
    failed,
};

enum class ScreenCaptureErrorCode : std::uint8_t {
    unsupported,
    wrong_thread,
    permission_denied,
    source_unavailable,
    invalid_config,
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
        return "ScreenCaptureKit unsupported";
    case ScreenCaptureErrorCode::wrong_thread:
        return "screen capture work must not run on the main thread";
    case ScreenCaptureErrorCode::permission_denied:
        return "Screen Recording permission denied";
    case ScreenCaptureErrorCode::source_unavailable:
        return "capture source unavailable";
    case ScreenCaptureErrorCode::invalid_config:
        return "invalid screen capture configuration";
    case ScreenCaptureErrorCode::capture_creation_failed:
        return "ScreenCaptureKit stream creation failed";
    case ScreenCaptureErrorCode::frame_failure:
        return "screen capture frame failure";
    }
    return "screen capture failure";
}

enum class CaptureSourceKind : std::uint8_t {
    display,
    window,
};

struct CaptureSource {
    CaptureSourceKind kind = CaptureSourceKind::display;
    std::uint64_t native_id = 0;
    std::string title;
    std::string application_name;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    bool primary = false;

    friend bool operator==(const CaptureSource&, const CaptureSource&) = default;
};

struct ScreenCaptureConfig {
    std::uint32_t max_width = 1920;
    std::uint32_t max_height = 1080;
    std::uint32_t frame_rate = 30;
    bool shows_cursor = true;
};

// Platform frames remain native and GPU-backed. lease owns the CVPixelBuffer lifetime while
// pixel_buffer is an opaque CVPixelBufferRef for Objective-C++ consumers.
struct NativeVideoFrame {
    std::shared_ptr<void> lease;
    void* pixel_buffer = nullptr;
    std::uint64_t sequence = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t pixel_format = 0;
    std::int64_t pts_100ns = 0;

    [[nodiscard]] explicit operator bool() const noexcept {
        return pixel_buffer != nullptr && width != 0 && height != 0;
    }
};

struct CaptureEnumerationResult {
    std::vector<CaptureSource> sources;
    std::optional<ScreenCaptureError> error;
};

struct ScreenCaptureStatistics {
    ScreenCaptureState state = ScreenCaptureState::idle;
    std::optional<ScreenCaptureError> error;
    std::uint64_t frames_received = 0;
    std::uint64_t frames_published = 0;
    std::uint64_t mailbox_overwrites = 0;
    std::uint64_t contention_drops = 0;
    std::uint64_t resize_events = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

class ScreenCaptureNativeAdapter {
public:
    using FrameHandler = std::function<void(NativeVideoFrame)>;
    using StopHandler = std::function<void(ScreenCaptureError)>;

    virtual ~ScreenCaptureNativeAdapter() = default;
    [[nodiscard]] virtual CaptureEnumerationResult enumerate_sources() noexcept = 0;
    [[nodiscard]] virtual std::optional<ScreenCaptureError> start(
        const CaptureSource& source,
        const ScreenCaptureConfig& config,
        FrameHandler on_frame,
        StopHandler on_stop) noexcept = 0;
    virtual void stop() noexcept = 0;
};

[[nodiscard]] std::unique_ptr<ScreenCaptureNativeAdapter>
make_screen_capture_native_adapter();

class MacScreenCapture final {
public:
    MacScreenCapture();
    explicit MacScreenCapture(std::unique_ptr<ScreenCaptureNativeAdapter> adapter);
    ~MacScreenCapture();

    MacScreenCapture(const MacScreenCapture&) = delete;
    MacScreenCapture& operator=(const MacScreenCapture&) = delete;

    [[nodiscard]] CaptureEnumerationResult enumerate_sources() noexcept;
    [[nodiscard]] std::optional<ScreenCaptureError> start_source(
        const CaptureSource& source,
        const ScreenCaptureConfig& config = {}) noexcept;
    void stop() noexcept;

    [[nodiscard]] bool wait_for_latest(
        NativeVideoFrame& frame,
        std::chrono::milliseconds timeout) noexcept;
    [[nodiscard]] ScreenCaptureStatistics statistics() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace catro::platform::macos
