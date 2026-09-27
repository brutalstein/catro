#pragma once

#include <catro/platform/windows/screen_capture.hpp>
#include <catro/transport/udp_peer_socket.hpp>

#include <dxgi1_2.h>
#include <wrl/client.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace catro::screen {

enum class ScreenShareState : std::uint8_t {
    idle,
    starting,
    sharing,
    failed,
};

enum class ScreenShareErrorCode : std::uint8_t {
    invalid_config,
    worker_start_failed,
    network_failed,
    capture_failed,
    encoder_failed,
    packetization_failed,
    preview_failed,
};

struct ScreenShareError {
    ScreenShareErrorCode code = ScreenShareErrorCode::invalid_config;
    std::string message;
    std::int64_t native_code = 0;
};

struct ScreenShareConfig {
    platform::windows::CaptureSource source;
    transport::UdpEndpoint bind;
    transport::UdpEndpoint peer;

    // These are ceilings. Source aspect ratio is preserved, output is even for NV12, and the
    // runtime never upscales a smaller source.
    std::uint32_t max_width = 1920;
    std::uint32_t max_height = 1080;
    std::uint32_t fps = 30;
    std::uint32_t bitrate = 6'000'000;

    std::uint32_t ssrc = 1;
    std::uint8_t payload_type = 96;
    std::uint16_t mtu_bytes = 1200;
    std::size_t max_access_unit_bytes = 4U * 1024U * 1024U;
};

struct ScreenShareSnapshot {
    ScreenShareState state = ScreenShareState::idle;
    std::string source_title;
    std::string error;

    std::uint32_t source_width = 0;
    std::uint32_t source_height = 0;
    std::uint32_t encoded_width = 0;
    std::uint32_t encoded_height = 0;

    std::uint64_t frames_encoded = 0;
    std::uint64_t frames_sent = 0;
    std::uint64_t frames_dropped = 0;
    std::uint64_t packets_sent = 0;
    std::uint64_t wire_bytes = 0;
    std::uint64_t backpressure_events = 0;
    std::uint64_t peer_unreachable_events = 0;
    std::uint64_t preview_frames = 0;
    std::uint64_t preview_drops = 0;
    std::uint64_t encoder_input_failures = 0;
    std::uint64_t encoder_output_failures = 0;
    std::uint64_t encoder_timeouts = 0;
    std::uint64_t capture_contention_drops = 0;
};

class WindowsScreenShareRuntime final {
public:
    WindowsScreenShareRuntime();
    ~WindowsScreenShareRuntime();

    WindowsScreenShareRuntime(const WindowsScreenShareRuntime&) = delete;
    WindowsScreenShareRuntime& operator=(const WindowsScreenShareRuntime&) = delete;

    // Validation failures are returned synchronously. Media/network failures after the worker has
    // started are published through snapshot().error with state == failed.
    [[nodiscard]] std::optional<ScreenShareError> start(
        const ScreenShareConfig& config);
    void stop() noexcept;

    [[nodiscard]] ScreenShareSnapshot snapshot() const;
    [[nodiscard]] Microsoft::WRL::ComPtr<IDXGISwapChain1>
    preview_swap_chain() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace catro::screen
