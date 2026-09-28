#include "video_peer.hpp"

#include <catro/platform/windows/screen_capture.hpp>
#include <catro/platform/windows/video_decoder.hpp>
#include <catro/platform/windows/video_encoder.hpp>
#include <catro/video/geometry.hpp>
#include <catro/video/rtp_h264.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <thread>
#include <variant>

namespace catro::tools {
namespace {

using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
using platform::windows::EncodedAccessUnit;
using platform::windows::GpuCaptureFrame;
using platform::windows::DecodedGpuFrame;
using platform::windows::H264DecoderConfig;
using platform::windows::H264DecoderError;
using platform::windows::HardwareEncoderConfig;
using platform::windows::HardwareEncoderError;
using platform::windows::ScreenCaptureError;
using platform::windows::WindowsGraphicsCapture;
using platform::windows::WindowsH264D3D11Decoder;
using platform::windows::WindowsH264HardwareEncoder;

constexpr std::size_t kReceiveDatagramBytes = 1500;
constexpr std::size_t kMaxReceiveDrain = 512;

template <class Integer>
bool parse_integer(std::string_view text, Integer& value) {
    const auto [end, error] =
        std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc{} && end == text.data() + text.size();
}

std::optional<UdpEndpoint> parse_endpoint(std::string_view value) {
    const auto separator = value.rfind(':');
    if (separator == std::string_view::npos || separator == 0 ||
        separator + 1 >= value.size()) {
        return std::nullopt;
    }

    unsigned port = 0;
    if (!parse_integer(value.substr(separator + 1), port) ||
        port == 0 || port > 65'535) {
        return std::nullopt;
    }
    return UdpEndpoint{
        std::string(value.substr(0, separator)),
        static_cast<std::uint16_t>(port)};
}

HardwareEncoderConfig make_encoder_config(
    const VideoPeerOptions& options,
    const GpuCaptureFrame& first,
    std::uint64_t adapter_luid) {
    HardwareEncoderConfig config;
    const auto size = video::fit_even_video_extent(
        first.width, first.height, options.max_width, options.max_height);
    if (size) {
        config.width = size->width;
        config.height = size->height;
    } else {
        config.width = 0;
        config.height = 0;
    }
    config.frame_rate_numerator = options.fps;
    config.frame_rate_denominator = 1;
    config.bitrate = options.bitrate;
    config.gop_frames = options.fps * 2U;
    config.max_access_unit_bytes = options.max_access_unit_bytes;
    config.adapter_luid = adapter_luid;
    return config;
}

std::chrono::nanoseconds frame_period(std::uint32_t fps) noexcept {
    constexpr std::uint64_t kNanosecondsPerSecond = 1'000'000'000ULL;
    return std::chrono::nanoseconds(
        static_cast<std::int64_t>(
            (kNanosecondsPerSecond + fps / 2U) / fps));
}

void report_udp_error(std::ostream& error, const UdpError& failure) {
    error << "catro-video-peer: " << name(failure.code);
    if (failure.native_code != 0) {
        error << " (native " << failure.native_code << ")";
    }
    error << '\n';
}

void report_capture_error(std::ostream& error, const ScreenCaptureError& failure) {
    error << "catro-video-peer: capture "
          << platform::windows::name(failure.code);
    if (failure.native_code != 0) {
        error << " (native " << failure.native_code << ")";
    }
    error << '\n';
}

void report_encoder_error(std::ostream& error, const HardwareEncoderError& failure) {
    error << "catro-video-peer: encoder "
          << platform::windows::name(failure.code);
    if (failure.native_code != 0) {
        error << " (native " << failure.native_code << ")";
    }
    error << '\n';
}

void report_decoder_error(std::ostream& error, const H264DecoderError& failure) {
    error << "catro-video-peer: decoder "
          << platform::windows::name(failure.code);
    if (failure.native_code != 0) {
        error << " (native " << failure.native_code << ")";
    }
    error << '\n';
}

[[nodiscard]] std::int64_t rtp_timestamp_to_100ns(
    std::uint32_t timestamp_90khz) noexcept {
    // 10,000,000 / 90,000 = 1000 / 9. A 32-bit RTP timestamp times 1000 fits in uint64_t.
    return static_cast<std::int64_t>(
        (static_cast<std::uint64_t>(timestamp_90khz) * 1000ULL + 4ULL) / 9ULL);
}

std::variant<std::unique_ptr<UdpPeerSocket>, UdpError> open_socket(
    const VideoPeerOptions& options) noexcept {
    auto opened = UdpPeerSocket::bind(options.bind);
    if (const auto* failure = std::get_if<UdpError>(&opened)) {
        return *failure;
    }

    auto socket =
        std::move(std::get<std::unique_ptr<UdpPeerSocket>>(opened));
    const auto connected = socket->connect_peer(options.peer);
    if (const auto* failure = std::get_if<UdpError>(&connected)) {
        return *failure;
    }
    return std::move(socket);
}

bool stop_requested(const VideoPeerControl* control) noexcept {
    return control != nullptr &&
           control->stop_requested.load(std::memory_order_relaxed);
}

struct SendStatistics {
    std::uint64_t frames_sent = 0;
    std::uint64_t frames_dropped = 0;
    std::uint64_t packets_sent = 0;
    std::uint64_t wire_bytes = 0;
    std::uint64_t backpressure_events = 0;
    std::uint64_t peer_unreachable_events = 0;
};

struct PacketSendContext {
    UdpPeerSocket* socket = nullptr;
    SendStatistics* stats = nullptr;
    std::optional<UdpError> fatal_error;
    bool soft_drop = false;

    static bool send(
        void* opaque, const video::RtpPacketSlice& packet) noexcept {
        auto& context = *static_cast<PacketSendContext*>(opaque);
        const std::array<std::span<const std::byte>, 2> segments{
            std::span<const std::byte>(
                packet.prefix.data(),
                static_cast<std::size_t>(packet.prefix_size)),
            packet.payload};

        const auto sent = context.socket->send_segments(segments);
        if (const auto* size = std::get_if<std::size_t>(&sent)) {
            ++context.stats->packets_sent;
            context.stats->wire_bytes += *size;
            return true;
        }

        const auto failure = std::get<UdpError>(sent);
        if (failure.code == UdpErrorCode::would_block) {
            ++context.stats->backpressure_events;
            context.soft_drop = true;
            return false;
        }
        if (failure.code == UdpErrorCode::peer_unreachable) {
            ++context.stats->peer_unreachable_events;
            context.soft_drop = true;
            return false;
        }

        context.fatal_error = failure;
        return false;
    }
};

int run_sender(
    const VideoPeerOptions& options,
    UdpPeerSocket& socket,
    std::ostream& out,
    std::ostream& error,
    VideoPeerControl* control) {
    WindowsGraphicsCapture capture;
    if (const auto failure = capture.start_primary_display()) {
        report_capture_error(error, *failure);
        return video_peer_capture_failed;
    }

    GpuCaptureFrame first;
    if (!capture.wait_for_latest(first, 1500ms)) {
        const auto stats = capture.statistics();
        capture.stop();
        if (stats.error) {
            report_capture_error(error, *stats.error);
        } else {
            error << "catro-video-peer: no initial GPU frame arrived\n";
        }
        return video_peer_capture_failed;
    }

    const auto capture_initial = capture.statistics();
    auto encoder_config =
        make_encoder_config(options, first, capture_initial.adapter_luid);
    if (encoder_config.width == 0 || encoder_config.height == 0) {
        error << "catro-video-peer: source aspect ratio cannot fit the configured even H.264 ceiling\n";
        capture.stop();
        return video_peer_encoder_failed;
    }

    WindowsH264HardwareEncoder encoder;
    if (const auto failure = encoder.start(encoder_config, *first.texture.Get())) {
        report_encoder_error(error, *failure);
        capture.stop();
        return video_peer_encoder_failed;
    }

    EncodedAccessUnit access_unit;
    try {
        access_unit.bytes.reserve(options.max_access_unit_bytes);
    } catch (...) {
        encoder.stop();
        capture.stop();
        error << "catro-video-peer: access-unit reserve failed\n";
        return video_peer_memory_failed;
    }

    const video::H264RtpConfig rtp{
        options.ssrc, options.payload_type, options.mtu_bytes};
    SendStatistics network;
    std::uint16_t next_sequence = 1;

    const auto send_frame = [&](const GpuCaptureFrame& frame) -> int {
        if (const auto failure = encoder.encode(frame, access_unit)) {
            report_encoder_error(error, *failure);
            return video_peer_encoder_failed;
        }
        if (access_unit.bytes.size() > options.max_access_unit_bytes) {
            error << "catro-video-peer: encoded access unit exceeded bounded capacity\n";
            return video_peer_packetization_failed;
        }

        PacketSendContext context{&socket, &network};
        const auto timestamp =
            video::rtp_timestamp_90khz(
                access_unit.pts_100ns <= 0
                    ? 0U
                    : static_cast<std::uint64_t>(access_unit.pts_100ns));
        const auto packetized = video::packetize_h264_annex_b(
            access_unit.bytes, timestamp, next_sequence, rtp,
            &context, &PacketSendContext::send);
        next_sequence = packetized.next_sequence;

        if (!packetized) {
            if (context.fatal_error) {
                report_udp_error(error, *context.fatal_error);
                return video_peer_network_failed;
            }
            if (context.soft_drop) {
                ++network.frames_dropped;
                return video_peer_ok;
            }
            error << "catro-video-peer: hardware encoder produced an unsupported H.264 access unit\n";
            return video_peer_packetization_failed;
        }

        ++network.frames_sent;
        return video_peer_ok;
    };

    auto code = send_frame(first);
    first = {};
    if (code != video_peer_ok) {
        encoder.stop();
        capture.stop();
        return code;
    }

    const auto initial_encoder = encoder.statistics();
    out << "mode: send\n"
        << "source: primary display " << capture_initial.width << "x"
        << capture_initial.height << " BGRA8\n"
        << "target: " << encoder_config.width << "x" << encoder_config.height
        << "@" << encoder_config.frame_rate_numerator
        << " H264 " << encoder_config.bitrate << " bit/s\n"
        << "encoder: "
        << (initial_encoder.encoder_name.empty()
                ? "<unnamed hardware MFT>"
                : initial_encoder.encoder_name)
        << "\n"
        << "path: WGC GPU BGRA -> GPU NV12 -> hardware H264 -> RTP scatter/gather UDP\n"
        << "rtp: ssrc ";
    if (options.ssrc == 0) {
        out << "auto";
    } else {
        out << options.ssrc;
    }
    out << ", pt " << static_cast<unsigned>(options.payload_type)
        << ", mtu " << options.mtu_bytes << "\n";

    const auto started = Clock::now();
    const auto deadline = started + options.duration;
    const auto period = frame_period(options.fps);
    auto next_frame = started + period;
    auto next_report = started + 1s;

    while (Clock::now() < deadline && !stop_requested(control)) {
        const auto before_sleep = Clock::now();
        if (before_sleep < next_frame) {
            std::this_thread::sleep_until(next_frame);
        }

        auto now = Clock::now();
        do {
            next_frame += period;
        } while (next_frame <= now);

        GpuCaptureFrame frame;
        if (capture.wait_for_latest(frame, 5ms)) {
            code = send_frame(frame);
            if (code != video_peer_ok) {
                encoder.stop();
                capture.stop();
                return code;
            }
        }

        now = Clock::now();
        if (now >= next_report) {
            const auto capture_stats = capture.statistics();
            const auto encode_stats = encoder.statistics();
            const auto elapsed =
                std::chrono::duration_cast<std::chrono::seconds>(
                    now - started).count();
            const auto encoded =
                std::max<std::uint64_t>(encode_stats.frames_encoded, 1);

            out << elapsed << "s:"
                << " cap " << capture_stats.frames_received
                << " enc " << encode_stats.frames_encoded
                << " tx-frame " << network.frames_sent
                << " tx-drop " << network.frames_dropped
                << " pkt " << network.packets_sent
                << " wire " << network.wire_bytes
                << " backpressure " << network.backpressure_events
                << " peer-miss " << network.peer_unreachable_events
                << " convert "
                << (encode_stats.conversion_total_us / encoded)
                << "/" << encode_stats.conversion_max_us << " us"
                << " encode "
                << (encode_stats.encode_total_us / encoded)
                << "/" << encode_stats.encode_max_us << " us"
                << " cap-overwrite " << capture_stats.mailbox_overwrites
                << " cap-contention " << capture_stats.contention_drops
                << " timeout " << encode_stats.output_timeouts
                << '\n';

            if (capture_stats.error) {
                report_capture_error(error, *capture_stats.error);
                encoder.stop();
                capture.stop();
                return video_peer_capture_failed;
            }

            do {
                next_report += 1s;
            } while (next_report <= now);
        }
    }

    const auto capture_stats = capture.statistics();
    const auto encode_stats = encoder.statistics();
    encoder.stop();
    capture.stop();

    out << "final:"
        << " encoded " << encode_stats.frames_encoded
        << ", tx-frames " << network.frames_sent
        << ", tx-dropped " << network.frames_dropped
        << ", packets " << network.packets_sent
        << ", wire-bytes " << network.wire_bytes
        << ", backpressure " << network.backpressure_events
        << ", peer-miss " << network.peer_unreachable_events
        << ", input-fail " << encode_stats.input_failures
        << ", output-fail " << encode_stats.output_failures
        << ", timeout " << encode_stats.output_timeouts
        << ", mf-input-samples " << encode_stats.input_sample_allocations
        << ", mf-output-samples " << encode_stats.output_sample_allocations
        << ", capture-contention " << capture_stats.contention_drops
        << '\n';

    if (encode_stats.frames_encoded == 0 ||
        encode_stats.input_failures != 0 ||
        encode_stats.output_failures != 0 ||
        encode_stats.output_timeouts != 0 ||
        capture_stats.contention_drops != 0) {
        return video_peer_encoder_failed;
    }
    return video_peer_ok;
}

int run_receiver(
    const VideoPeerOptions& options,
    UdpPeerSocket& socket,
    std::ostream& out,
    std::ostream& error,
    VideoPeerControl* control) {
    auto frame_memory = std::unique_ptr<std::byte[]>(
        new (std::nothrow) std::byte[options.max_access_unit_bytes]);
    if (!frame_memory) {
        error << "catro-video-peer: receive frame buffer allocation failed\n";
        return video_peer_memory_failed;
    }

    const video::H264RtpConfig rtp{
        options.ssrc, options.payload_type, options.mtu_bytes};
    video::H264RtpReassembler reassembler(
        std::span<std::byte>(
            frame_memory.get(), options.max_access_unit_bytes),
        rtp);

    WindowsH264D3D11Decoder decoder;
    H264DecoderConfig decoder_config;
    decoder_config.max_access_unit_bytes =
        options.max_access_unit_bytes;
    if (const auto failure = decoder.start(decoder_config)) {
        report_decoder_error(error, *failure);
        return video_peer_decoder_failed;
    }

    std::array<std::byte, kReceiveDatagramBytes> datagram{};
    std::uint64_t packets = 0;
    std::uint64_t bytes = 0;
    std::uint64_t frames = 0;
    std::uint64_t keyframes = 0;
    std::uint64_t dropped_frames = 0;
    std::uint64_t rejected_packets = 0;
    std::uint64_t peer_unreachable_events = 0;

    const auto initial_decoder = decoder.statistics();
    out << "mode: receive\n"
        << "decoder: "
        << (initial_decoder.decoder_name.empty()
                ? "<unnamed H264 decoder>"
                : initial_decoder.decoder_name)
        << "\n"
        << "path: UDP RTP -> bounded H264 reassembly -> D3D11 H264 decode; presentation not enabled yet\n"
        << "rtp: ssrc " << options.ssrc
        << ", pt " << static_cast<unsigned>(options.payload_type)
        << ", mtu " << options.mtu_bytes
        << ", frame-buffer " << options.max_access_unit_bytes << " B\n";

    const auto started = Clock::now();
    const auto deadline = started + options.duration;
    auto next_report = started + 1s;

    while (Clock::now() < deadline && !stop_requested(control)) {
        const auto ready = socket.wait_readable(10ms);
        if (const auto* wait_failure = std::get_if<UdpError>(&ready)) {
            if (wait_failure->code == UdpErrorCode::peer_unreachable) {
                ++peer_unreachable_events;
            } else {
                report_udp_error(error, *wait_failure);
                return video_peer_network_failed;
            }
        } else if (std::get<bool>(ready)) {
            for (std::size_t drained = 0; drained < kMaxReceiveDrain; ++drained) {
                const auto received = socket.receive(datagram);
                if (const auto* receive_failure = std::get_if<UdpError>(&received)) {
                    if (receive_failure->code == UdpErrorCode::peer_unreachable) {
                        ++peer_unreachable_events;
                        break;
                    }
                    if (receive_failure->code == UdpErrorCode::datagram_too_large) {
                        ++rejected_packets;
                        continue;
                    }
                    report_udp_error(error, *receive_failure);
                    return video_peer_network_failed;
                }

                const auto size = std::get<std::size_t>(received);
                if (size == 0) {
                    break;
                }

                ++packets;
                bytes += size;
                const auto result =
                    reassembler.push(
                        std::span<const std::byte>(datagram).first(size));
                if (result.status == video::H264ReassemblyStatus::frame_ready) {
                    ++frames;
                    if (result.frame.keyframe) {
                        ++keyframes;
                    }

                    DecodedGpuFrame decoded;
                    const auto decode_failure = decoder.decode(
                        result.frame.annex_b,
                        rtp_timestamp_to_100ns(
                            result.frame.timestamp_90khz),
                        decoded);
                    if (decode_failure) {
                        report_decoder_error(error, *decode_failure);
                        decoder.stop();
                        return video_peer_decoder_failed;
                    }
                } else if (
                    result.status ==
                    video::H264ReassemblyStatus::frame_dropped) {
                    ++dropped_frames;
                } else if (
                    result.status ==
                    video::H264ReassemblyStatus::packet_rejected) {
                    ++rejected_packets;
                }
            }
        }

        const auto now = Clock::now();
        if (now >= next_report) {
            const auto elapsed =
                std::chrono::duration_cast<std::chrono::seconds>(
                    now - started).count();
            const auto decoder_stats = decoder.statistics();
            const auto submitted =
                std::max<std::uint64_t>(
                    decoder_stats.frames_submitted, 1);
            out << elapsed << "s:"
                << " rx-pkt " << packets
                << " rx-bytes " << bytes
                << " frames " << frames
                << " key " << keyframes
                << " decoded " << decoder_stats.frames_decoded
                << " decode "
                << (decoder_stats.decode_total_us / submitted)
                << "/" << decoder_stats.decode_max_us << " us"
                << " stream-change " << decoder_stats.stream_changes
                << " input-buf " << decoder_stats.input_sample_allocations
                << " gpu-fail " << decoder_stats.gpu_output_failures
                << " frame-drop " << dropped_frames
                << " reject " << rejected_packets
                << " peer-miss " << peer_unreachable_events
                << '\n';
            do {
                next_report += 1s;
            } while (next_report <= now);
        }
    }

    const auto decoder_stats = decoder.statistics();
    decoder.stop();

    out << "final:"
        << " packets " << packets
        << ", bytes " << bytes
        << ", frames " << frames
        << ", keyframes " << keyframes
        << ", decoded " << decoder_stats.frames_decoded
        << ", decode-input-fail " << decoder_stats.input_failures
        << ", decode-output-fail " << decoder_stats.output_failures
        << ", gpu-output-fail " << decoder_stats.gpu_output_failures
        << ", stream-changes " << decoder_stats.stream_changes
        << ", decoder-input-buffers " << decoder_stats.input_sample_allocations
        << ", frame-drops " << dropped_frames
        << ", rejected " << rejected_packets
        << ", peer-miss " << peer_unreachable_events
        << '\n';

    if ((frames > 0 && decoder_stats.frames_decoded == 0) ||
        decoder_stats.input_failures != 0 ||
        decoder_stats.output_failures != 0 ||
        decoder_stats.gpu_output_failures != 0) {
        return video_peer_decoder_failed;
    }
    return video_peer_ok;
}

} // namespace

std::optional<VideoPeerOptions> parse_video_peer_arguments(
    std::span<const std::string_view> arguments) {
    VideoPeerOptions options;
    bool mode_seen = false;
    bool bind_seen = false;
    bool peer_seen = false;
    bool seconds_seen = false;
    bool width_seen = false;
    bool height_seen = false;
    bool fps_seen = false;
    bool bitrate_seen = false;
    bool ssrc_seen = false;
    bool payload_seen = false;
    bool mtu_seen = false;
    bool frame_buffer_seen = false;

    for (std::size_t index = 0; index < arguments.size(); ++index) {
        if (index + 1 >= arguments.size()) {
            return std::nullopt;
        }
        const auto option = arguments[index];
        const auto value = arguments[++index];

        if (option == "--mode" && !mode_seen) {
            mode_seen = true;
            if (value == "send") {
                options.mode = VideoPeerMode::send;
            } else if (value == "receive") {
                options.mode = VideoPeerMode::receive;
            } else {
                return std::nullopt;
            }
        } else if (option == "--bind" && !bind_seen) {
            bind_seen = true;
            const auto endpoint = parse_endpoint(value);
            if (!endpoint) {
                return std::nullopt;
            }
            options.bind = *endpoint;
        } else if (option == "--peer" && !peer_seen) {
            peer_seen = true;
            const auto endpoint = parse_endpoint(value);
            if (!endpoint) {
                return std::nullopt;
            }
            options.peer = *endpoint;
        } else if (option == "--seconds" && !seconds_seen) {
            seconds_seen = true;
            int seconds = 0;
            if (!parse_integer(value, seconds) || seconds < 1 || seconds > 300) {
                return std::nullopt;
            }
            options.duration = std::chrono::seconds(seconds);
        } else if (option == "--max-width" && !width_seen) {
            width_seen = true;
            if (!parse_integer(value, options.max_width) ||
                options.max_width < 320 || options.max_width > 7680) {
                return std::nullopt;
            }
        } else if (option == "--max-height" && !height_seen) {
            height_seen = true;
            if (!parse_integer(value, options.max_height) ||
                options.max_height < 180 || options.max_height > 4320) {
                return std::nullopt;
            }
        } else if (option == "--fps" && !fps_seen) {
            fps_seen = true;
            if (!parse_integer(value, options.fps) ||
                options.fps < 1 || options.fps > 120) {
                return std::nullopt;
            }
        } else if (option == "--bitrate" && !bitrate_seen) {
            bitrate_seen = true;
            if (!parse_integer(value, options.bitrate) ||
                options.bitrate < 128'000 ||
                options.bitrate > 50'000'000) {
                return std::nullopt;
            }
        } else if (option == "--ssrc" && !ssrc_seen) {
            ssrc_seen = true;
            if (!parse_integer(value, options.ssrc)) {
                return std::nullopt;
            }
        } else if (option == "--payload-type" && !payload_seen) {
            payload_seen = true;
            unsigned payload_type = 0;
            if (!parse_integer(value, payload_type) ||
                payload_type < 96 || payload_type > 127) {
                return std::nullopt;
            }
            options.payload_type = static_cast<std::uint8_t>(payload_type);
        } else if (option == "--mtu" && !mtu_seen) {
            mtu_seen = true;
            unsigned mtu = 0;
            if (!parse_integer(value, mtu) || mtu < 576 || mtu > 1400) {
                return std::nullopt;
            }
            options.mtu_bytes = static_cast<std::uint16_t>(mtu);
        } else if (option == "--frame-buffer" && !frame_buffer_seen) {
            frame_buffer_seen = true;
            std::uint64_t frame_buffer = 0;
            if (!parse_integer(value, frame_buffer) ||
                frame_buffer < 262'144 || frame_buffer > 16'777'216) {
                return std::nullopt;
            }
            options.max_access_unit_bytes =
                static_cast<std::size_t>(frame_buffer);
        } else {
            return std::nullopt;
        }
    }

    if (!mode_seen || !bind_seen || !peer_seen) {
        return std::nullopt;
    }
    if (options.mode == VideoPeerMode::send) {
        if (!ssrc_seen) {
            options.ssrc = 1;
        } else if (options.ssrc == 0) {
            return std::nullopt;
        }
    }
    return options;
}

int run_video_peer(
    const VideoPeerOptions& options,
    std::ostream& out,
    std::ostream& error,
    VideoPeerControl* control) {
    auto opened = open_socket(options);
    if (const auto* failure = std::get_if<UdpError>(&opened)) {
        report_udp_error(error, *failure);
        return video_peer_network_failed;
    }

    auto socket =
        std::move(std::get<std::unique_ptr<UdpPeerSocket>>(opened));
    if (options.mode == VideoPeerMode::send) {
        return run_sender(options, *socket, out, error, control);
    }
    return run_receiver(options, *socket, out, error, control);
}

} // namespace catro::tools
