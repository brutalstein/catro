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
    listening,
    starting,
    sharing,
    failed,
};

enum class ScreenShareErrorCode : std::uint8_t {
    invalid_config,
    worker_start_failed,
    network_failed,
    memory_failed,
    capture_failed,
    encoder_failed,
    packetization_failed,
    preview_failed,
    decoder_failed,
    remote_present_failed,
};

struct ScreenShareError {
    ScreenShareErrorCode code = ScreenShareErrorCode::invalid_config;
    std::string message;
    std::int64_t native_code = 0;
};

struct ScreenTransportConfig {
    transport::UdpEndpoint bind;
    transport::UdpEndpoint peer;
    std::uint8_t payload_type = 96;
    std::uint16_t mtu_bytes = 1200;
    std::size_t max_access_unit_bytes = 4U * 1024U * 1024U;

    friend bool operator==(const ScreenTransportConfig&, const ScreenTransportConfig&) = default;
};

struct ScreenShareConfig {
    platform::windows::CaptureSource source;
    bool borderless = false;
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

    bool remote_active = false;
    std::uint32_t remote_width = 0;
    std::uint32_t remote_height = 0;
    std::uint64_t remote_packets = 0;
    std::uint64_t remote_wire_bytes = 0;
    std::uint64_t remote_frames = 0;
    std::uint64_t remote_decoded = 0;
    std::uint64_t remote_presented = 0;
    std::uint64_t remote_frame_drops = 0;
    std::uint64_t remote_packet_rejects = 0;
    std::uint64_t remote_decode_failures = 0;
    std::uint64_t remote_present_drops = 0;
};

// One full-duplex video transport per voice-room membership. The connected UDP socket is shared by
// one receive worker and (only while the local user shares) one capture/encode worker. The socket
// lifetime outlives both workers, so there is no second port, packet relay, or encoded-frame copy.
class WindowsScreenShareRuntime final {
public:
    WindowsScreenShareRuntime();
    ~WindowsScreenShareRuntime();

    WindowsScreenShareRuntime(const WindowsScreenShareRuntime&) = delete;
    WindowsScreenShareRuntime& operator=(const WindowsScreenShareRuntime&) = delete;

    // Starts receive/decode/presentation only. Product code calls this when joining voice so an
    // incoming screen stream can appear even when the local user is not sharing.
    [[nodiscard]] std::optional<ScreenShareError> start_listening(
        const ScreenTransportConfig& config);

    // Starts (or reuses) the transport and adds the local capture/encode sender.
    [[nodiscard]] std::optional<ScreenShareError> start(
        const ScreenShareConfig& config);

    // Stops only local capture/encode. Incoming video remains active on the same socket.
    void stop_sharing() noexcept;

    // Stops both directions and releases the transport.
    void stop() noexcept;

    [[nodiscard]] ScreenShareSnapshot snapshot() const;
    [[nodiscard]] Microsoft::WRL::ComPtr<IDXGISwapChain1>
    preview_swap_chain() const;
    [[nodiscard]] Microsoft::WRL::ComPtr<IDXGISwapChain1>
    remote_swap_chain() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace catro::screen
