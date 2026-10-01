#pragma once

#include <catro/platform/macos/pixel_buffer.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace catro::platform::macos {

enum class ScreenCaptureState : std::uint8_t {
    idle,
    running,
    source_closed,
    failed,
};

enum class ScreenCaptureErrorCode : std::uint8_t {
    invalid_config,
    // Screen Recording access is not granted, or it was revoked while capturing.
    permission_denied,
    source_unavailable,
    enumeration_failed,
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
    case ScreenCaptureErrorCode::invalid_config:
        return "invalid screen capture configuration";
    case ScreenCaptureErrorCode::permission_denied:
        return "Screen Recording permission is not granted";
    case ScreenCaptureErrorCode::source_unavailable:
        return "capture source unavailable";
    case ScreenCaptureErrorCode::enumeration_failed:
        return "capture source enumeration failed";
    case ScreenCaptureErrorCode::capture_creation_failed:
        return "ScreenCaptureKit stream creation failed";
    case ScreenCaptureErrorCode::frame_failure:
        return "capture frame failure";
    }
    return "screen capture failure";
}

enum class CaptureSourceKind : std::uint8_t {
    display,
    window,
};

struct CaptureSource {
    CaptureSourceKind kind = CaptureSourceKind::display;
    // CGDirectDisplayID or CGWindowID. Revalidated against fresh shareable content at start.
    std::uint32_t display_id = 0;
    std::uint32_t window_id = 0;
    std::string title;
    std::string application_name;
    std::int32_t process_id = 0;
    // Displays report native pixels; windows report their frame in points.
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    bool primary = false;

    friend bool operator==(const CaptureSource&, const CaptureSource&) = default;
};

using CaptureSourcesResult = std::variant<std::vector<CaptureSource>, ScreenCaptureError>;

// Passive TCC check; never prompts.
[[nodiscard]] bool screen_capture_access_granted() noexcept;
// Shows the system Screen Recording prompt. Call only from an explicit user action.
bool request_screen_capture_access() noexcept;

// Lightweight discovery for the share-source chooser; opens no stream. Without Screen Recording
// access it fails with permission_denied before touching ScreenCaptureKit, which would prompt.
// Blocks the caller up to `timeout`; never call it on the main thread.
[[nodiscard]] CaptureSourcesResult enumerate_capture_sources(
    std::chrono::milliseconds timeout = std::chrono::seconds(3)) noexcept;

struct ScreenCaptureConfig {
    std::uint32_t max_width = 1920;
    std::uint32_t max_height = 1080;
    std::uint32_t frame_rate = 30;
    bool show_cursor = true;
};

[[nodiscard]] std::optional<ScreenCaptureError> validate(const ScreenCaptureConfig& config) noexcept;

struct CaptureFrame {
    PixelBuffer buffer;
    std::uint64_t sequence = 0;
    std::int64_t pts_100ns = 0;
};

// One-frame latest-edge mailbox between a capture callback and exactly one consumer. publish()
// never blocks: a newer frame replaces an untaken one, and a busy mailbox drops the newest frame.
class LatestFrameMailbox final {
public:
    void publish(CaptureFrame frame) noexcept;
    // Returns false on timeout or once closed.
    [[nodiscard]] bool wait_for_latest(CaptureFrame& frame, std::chrono::milliseconds timeout) noexcept;
    // Wakes the consumer and drops the held frame; later publishes are ignored until reopen().
    void close() noexcept;
    void reopen() noexcept;

    [[nodiscard]] std::uint64_t published() const noexcept {
        return published_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint64_t overwrites() const noexcept {
        return overwrites_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint64_t contention_drops() const noexcept {
        return contention_drops_.load(std::memory_order_relaxed);
    }

private:
    std::mutex mutex_;
    std::condition_variable ready_;
    std::optional<CaptureFrame> slot_;
    bool closed_ = false;
    std::atomic<std::uint64_t> published_{0};
    std::atomic<std::uint64_t> overwrites_{0};
    std::atomic<std::uint64_t> contention_drops_{0};
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

// ScreenCaptureKit stream delivering IOSurface-backed NV12 frames. The stream callback only
// retains the newest frame into the mailbox; it never copies pixels or blocks on the consumer.
// Lifecycle methods are control-thread operations; wait_for_latest() serves one worker.
class MacScreenCapture final {
public:
    MacScreenCapture();
    ~MacScreenCapture();

    MacScreenCapture(const MacScreenCapture&) = delete;
    MacScreenCapture& operator=(const MacScreenCapture&) = delete;

    [[nodiscard]] std::optional<ScreenCaptureError> start(const CaptureSource& source,
                                                          const ScreenCaptureConfig& config = {});
    void stop() noexcept;
    [[nodiscard]] bool wait_for_latest(CaptureFrame& frame, std::chrono::milliseconds timeout) noexcept;
    [[nodiscard]] ScreenCaptureStatistics statistics() const noexcept;

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

} // namespace catro::platform::macos
