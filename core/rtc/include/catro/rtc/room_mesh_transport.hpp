#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace catro::rtc {

enum class RoomTransportState : std::uint8_t {
    idle,
    connecting,
    joined,
    failed,
};

enum class RoomTransportErrorCode : std::uint8_t {
    invalid_config,
    signaling_failed,
    rtc_failed,
    capacity_reached,
};

struct RoomTransportError {
    RoomTransportErrorCode code = RoomTransportErrorCode::rtc_failed;
    std::string message;
};

struct RoomMeshConfig {
    std::string signaling_url;
    std::string access_token;
    std::string server_id;
    std::string channel_id;
    std::string user_id;
    std::vector<std::string> ice_server_urls;

    std::size_t max_peers = 16;

    // Production defaults are intentionally strict. Engineering/local tests must explicitly opt
    // out instead of accidentally shipping plaintext signaling or a no-TURN configuration.
    bool allow_insecure_signaling = false;
    bool allow_no_turn = false;
};

struct RoomTransportCallbacks {
    // Callback spans are valid only for the duration of the callback. Copy only when a downstream
    // queue actually needs ownership; the normal media path consumes synchronously.
    std::function<void(std::string_view, std::span<const std::byte>)>
        on_voice_datagram;
    std::function<void(std::string_view, std::span<const std::byte>)>
        on_video_datagram;
    std::function<void(std::string_view, std::span<const std::byte>)>
        on_stream_audio_datagram;
    std::function<void(RoomTransportState)> on_state;
    std::function<void(std::string_view)> on_error;
};

// Small-room production RTC transport.
//
// Signaling: WSS room service.
// NAT traversal: ICE + STUN/TURN.
// Media confidentiality/integrity: WebRTC DTLS.
// Voice/video payloads: Catro's already-bounded datagrams over unordered no-retransmit data
// channels. Encoding remains once-per-source; compressed datagrams are fanned out to peers.
class RoomMeshTransport final {
public:
    RoomMeshTransport();
    ~RoomMeshTransport();

    RoomMeshTransport(const RoomMeshTransport&) = delete;
    RoomMeshTransport& operator=(const RoomMeshTransport&) = delete;

    [[nodiscard]] std::optional<RoomTransportError> start(
        RoomMeshConfig config,
        RoomTransportCallbacks callbacks);
    void stop() noexcept;

    [[nodiscard]] std::size_t send_voice(
        std::span<const std::byte> datagram) noexcept;
    [[nodiscard]] std::size_t send_video(
        std::span<const std::byte> datagram) noexcept;
    [[nodiscard]] std::size_t send_stream_audio(
        std::span<const std::byte> datagram) noexcept;

    [[nodiscard]] RoomTransportState state() const noexcept;
    [[nodiscard]] std::size_t peer_count() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace catro::rtc
