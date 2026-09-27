#pragma once

#include "udp_socket.hpp"

#include <catro/audio/engine.hpp>
#include <catro/capabilities/ids.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <optional>
#include <ostream>
#include <span>
#include <string_view>

namespace catro::tools {

enum class VoicePeerMode {
    send,
    receive,
    duplex,
};

struct VoicePeerOptions {
    VoicePeerMode mode = VoicePeerMode::duplex;
    UdpEndpoint bind;
    UdpEndpoint peer;
    std::chrono::seconds duration{30};
    std::optional<capabilities::AudioEndpointId> input;
    std::optional<capabilities::AudioEndpointId> output;
    std::uint32_t stream_id = 1;
    std::uint16_t jitter_packets = 3;
    std::int32_t bitrate = 48'000;
};

struct VoicePeerControl {
    std::atomic_bool stop_requested{false};
    std::atomic_bool muted{false};
    std::atomic_bool deafened{false};
};

enum VoicePeerExit : int {
    voice_peer_ok = 0,
    voice_peer_invalid_arguments = 2,
    voice_peer_audio_failed = 5,
    voice_peer_network_failed = 6,
    voice_peer_codec_failed = 7,
};

inline constexpr std::string_view kVoicePeerUsage =
    "usage: catro-voice-peer --bind <ipv4:port> --peer <ipv4:port> [options]\n"
    "  --mode send|receive|duplex   media direction (default duplex)\n"
    "  --seconds 1-300              run duration (default 30)\n"
    "  --input <endpoint-id>        capture endpoint; default is OS communications input\n"
    "  --output <endpoint-id>       render endpoint; default is OS communications output\n"
    "  --stream-id 1-4294967295     packet stream id (default 1)\n"
    "  --jitter 1-10                target 20 ms packets (default 3 = 60 ms)\n"
    "  --bitrate 12000-128000       Opus bitrate in bit/s (default 48000)\n"
    "  numeric loopback/private IPv4 only; UDP is an unencrypted engineering transport\n";

[[nodiscard]] std::optional<VoicePeerOptions> parse_voice_peer_arguments(
    std::span<const std::string_view> arguments);

[[nodiscard]] int run_voice_peer(const VoicePeerOptions& options,
                                 audio::AudioPlatform& platform,
                                 std::ostream& out,
                                 std::ostream& error,
                                 VoicePeerControl* control = nullptr);

[[nodiscard]] int run_voice_peer(std::span<const std::string_view> arguments,
                                 audio::AudioPlatform& platform,
                                 std::ostream& out,
                                 std::ostream& error,
                                 VoicePeerControl* control = nullptr);

} // namespace catro::tools
