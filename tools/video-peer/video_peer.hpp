#pragma once

#include "udp_socket.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ostream>
#include <span>
#include <string_view>

namespace catro::tools {

enum class VideoPeerMode : std::uint8_t {
    send,
    receive,
};

struct VideoPeerOptions {
    VideoPeerMode mode = VideoPeerMode::receive;
    UdpEndpoint bind;
    UdpEndpoint peer;
    std::chrono::seconds duration{30};

    // The sender preserves the source aspect ratio exactly and never upscales. These are ceilings,
    // not a forced output size, so 16:9, 16:10 and ultrawide displays stay geometrically correct.
    std::uint32_t max_width = 2560;
    std::uint32_t max_height = 1080;
    std::uint32_t fps = 30;
    std::uint32_t bitrate = 6'000'000;

    std::uint32_t ssrc = 1;
    std::uint8_t payload_type = 96;
    std::uint16_t mtu_bytes = 1200;

    // One bounded allocation on the receive side and one capacity reservation on the sender.
    std::size_t max_access_unit_bytes = 4U * 1024U * 1024U;
};

struct VideoPeerControl {
    std::atomic_bool stop_requested{false};
};

enum VideoPeerExit : int {
    video_peer_ok = 0,
    video_peer_invalid_arguments = 2,
    video_peer_capture_failed = 5,
    video_peer_network_failed = 6,
    video_peer_encoder_failed = 7,
    video_peer_packetization_failed = 8,
    video_peer_memory_failed = 9,
};

inline constexpr std::string_view kVideoPeerUsage =
    "usage: catro-video-peer --mode send|receive --bind <ipv4:port> --peer <ipv4:port> [options]\n"
    "  --seconds 1-300              run duration (default 30)\n"
    "  --max-width 320-7680         sender width ceiling (default 2560)\n"
    "  --max-height 180-4320        sender height ceiling (default 1080)\n"
    "  --fps 1-120                  sender frame rate (default 30)\n"
    "  --bitrate 128000-50000000    sender H.264 bit rate (default 6000000)\n"
    "  --ssrc 1-4294967295          RTP SSRC (default 1)\n"
    "  --payload-type 96-127        dynamic RTP payload type (default 96)\n"
    "  --mtu 576-1400               RTP UDP datagram ceiling (default 1200)\n"
    "  --frame-buffer 262144-16777216 receiver/sender AU capacity in bytes (default 4194304)\n"
    "  numeric loopback/private IPv4 only; UDP is an unencrypted engineering transport\n";

[[nodiscard]] std::optional<VideoPeerOptions> parse_video_peer_arguments(
    std::span<const std::string_view> arguments);

[[nodiscard]] int run_video_peer(
    const VideoPeerOptions& options,
    std::ostream& out,
    std::ostream& error,
    VideoPeerControl* control = nullptr);

} // namespace catro::tools
