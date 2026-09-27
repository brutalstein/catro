#include "voice_peer.hpp"

#include <catro/audio/external_session.hpp>
#include <catro/voice/pipeline.hpp>

#include <algorithm>
#include <atomic>
#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>
#include <variant>

namespace catro::tools {
namespace {

using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

constexpr auto kWorkerPoll = 2ms;
constexpr auto kFramePeriod = 20ms;
constexpr int kMaxEncodeDrain = 4;
constexpr int kMaxReceiveDrain = 64;
constexpr int kMaxPlayoutCatchup = 3;

struct NetworkStatistics {
    std::uint64_t sent_packets = 0;
    std::uint64_t sent_bytes = 0;
    std::uint64_t send_backpressure_drops = 0;
    std::uint64_t received_packets = 0;
    std::uint64_t received_bytes = 0;
    std::uint64_t worker_late_resyncs = 0;
};

struct TimingAccumulator {
    std::uint64_t count = 0;
    std::uint64_t total_us = 0;
    std::uint64_t max_us = 0;

    void add(Clock::duration duration) noexcept {
        const auto value = static_cast<std::uint64_t>(
            std::max<std::int64_t>(0, std::chrono::duration_cast<std::chrono::microseconds>(duration).count()));
        ++count;
        total_us += value;
        max_us = std::max(max_us, value);
    }

    [[nodiscard]] std::uint64_t average_us() const noexcept {
        return count == 0 ? 0 : total_us / count;
    }
};

template <class Integer>
bool parse_integer(std::string_view text, Integer& value) {
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc{} && end == text.data() + text.size();
}

std::optional<UdpEndpoint> parse_endpoint(std::string_view value) {
    const auto separator = value.rfind(':');
    if (separator == std::string_view::npos || separator == 0 || separator + 1 >= value.size()) {
        return std::nullopt;
    }
    unsigned port = 0;
    if (!parse_integer(value.substr(separator + 1), port) || port == 0 || port > 65535) {
        return std::nullopt;
    }
    return UdpEndpoint{std::string(value.substr(0, separator)), static_cast<std::uint16_t>(port)};
}

std::string endpoint_text(const UdpEndpoint& endpoint) {
    return endpoint.address + ":" + std::to_string(endpoint.port);
}

std::string audio_info(const audio::StreamInfo& info) {
    return info.device.value + " (" + std::to_string(info.device_sample_rate) + " Hz, " +
           std::to_string(info.device_channels) + " ch, period " +
           std::to_string(info.period_frames) + " frames)";
}

void report_udp_error(std::ostream& error, const UdpError& failure) {
    error << "catro-voice-peer: " << name(failure.code);
    if (failure.native_code != 0) {
        error << " (native " << failure.native_code << ")";
    }
    error << '\n';
}

void report_audio_error(std::ostream& error, const audio::AudioError& failure) {
    error << "catro-voice-peer: audio " << audio::name(failure.code);
    if (failure.native_code) {
        char code[24]{};
        std::snprintf(code, sizeof(code), "0x%08x", static_cast<unsigned>(*failure.native_code));
        error << " (" << code << ")";
    }
    error << '\n';
}

void report_codec_error(std::ostream& error, const voice::CodecError& failure) {
    error << "catro-voice-peer: codec " << voice::name(failure.code);
    if (failure.native_code != 0) {
        error << " (native " << failure.native_code << ")";
    }
    error << '\n';
}

bool sends(VoicePeerMode mode) noexcept {
    return mode != VoicePeerMode::receive;
}

bool receives(VoicePeerMode mode) noexcept {
    return mode != VoicePeerMode::send;
}

std::string_view mode_name(VoicePeerMode mode) noexcept {
    switch (mode) {
    case VoicePeerMode::send:
        return "send";
    case VoicePeerMode::receive:
        return "receive";
    case VoicePeerMode::duplex:
        return "duplex";
    }
    return "duplex";
}

audio::ExternalSessionMode audio_mode(VoicePeerMode mode) noexcept {
    switch (mode) {
    case VoicePeerMode::send:
        return audio::ExternalSessionMode::capture_only;
    case VoicePeerMode::receive:
        return audio::ExternalSessionMode::render_only;
    case VoicePeerMode::duplex:
        return audio::ExternalSessionMode::duplex;
    }
    return audio::ExternalSessionMode::duplex;
}

void print_progress(std::ostream& out, std::int64_t elapsed_seconds,
                    const NetworkStatistics& network,
                    const voice::VoicePipelineStatistics& media,
                    const audio::ExternalAudioStatistics& audio_stats,
                    const TimingAccumulator& encode_timing,
                    const TimingAccumulator& decode_timing) {
    out << elapsed_seconds << "s:"
        << " tx " << network.sent_packets << " pkts/" << network.sent_bytes << " B"
        << " rx " << network.received_packets << " pkts/" << network.received_bytes << " B"
        << " net-drop " << network.send_backpressure_drops
        << " reorder " << media.jitter.reordered
        << " late " << media.jitter.late
        << " fec " << media.jitter.fec
        << " plc " << media.jitter.plc
        << " jitter " << media.jitter.buffered << "/" << media.jitter.peak_buffered
        << " cap-drop " << media.capture.dropped_callbacks
        << " render-full " << media.render_queue_full
        << " underrun " << media.render.underrun_callbacks
        << " glitches " << audio_stats.glitches
        << " enc " << encode_timing.average_us() << "/" << encode_timing.max_us << " us"
        << " dec " << decode_timing.average_us() << "/" << decode_timing.max_us << " us"
        << " worker-resync " << network.worker_late_resyncs
        << '\n';
}

} // namespace

std::optional<VoicePeerOptions> parse_voice_peer_arguments(
    std::span<const std::string_view> arguments) {
    VoicePeerOptions options;
    bool bind_seen = false;
    bool peer_seen = false;
    bool mode_seen = false;
    bool seconds_seen = false;
    bool stream_seen = false;
    bool jitter_seen = false;
    bool bitrate_seen = false;

    for (std::size_t index = 0; index < arguments.size(); ++index) {
        if (index + 1 >= arguments.size()) {
            return std::nullopt;
        }
        const auto option = arguments[index];
        const auto value = arguments[++index];

        if (option == "--bind" && !bind_seen) {
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
        } else if (option == "--mode" && !mode_seen) {
            mode_seen = true;
            if (value == "send") {
                options.mode = VoicePeerMode::send;
            } else if (value == "receive") {
                options.mode = VoicePeerMode::receive;
            } else if (value == "duplex") {
                options.mode = VoicePeerMode::duplex;
            } else {
                return std::nullopt;
            }
        } else if (option == "--seconds" && !seconds_seen) {
            seconds_seen = true;
            int seconds = 0;
            if (!parse_integer(value, seconds) || seconds < 1 || seconds > 300) {
                return std::nullopt;
            }
            options.duration = std::chrono::seconds(seconds);
        } else if (option == "--stream-id" && !stream_seen) {
            stream_seen = true;
            std::uint32_t stream_id = 0;
            if (!parse_integer(value, stream_id) || stream_id == 0) {
                return std::nullopt;
            }
            options.stream_id = stream_id;
        } else if (option == "--jitter" && !jitter_seen) {
            jitter_seen = true;
            unsigned jitter = 0;
            if (!parse_integer(value, jitter) || jitter < 1 || jitter > 10) {
                return std::nullopt;
            }
            options.jitter_packets = static_cast<std::uint16_t>(jitter);
        } else if (option == "--bitrate" && !bitrate_seen) {
            bitrate_seen = true;
            int bitrate = 0;
            if (!parse_integer(value, bitrate) || bitrate < 12'000 || bitrate > 128'000) {
                return std::nullopt;
            }
            options.bitrate = bitrate;
        } else if (option == "--input" && !options.input && !value.empty()) {
            options.input = capabilities::AudioEndpointId{
                std::string(value), capabilities::IdentityScope::persistent};
        } else if (option == "--output" && !options.output && !value.empty()) {
            options.output = capabilities::AudioEndpointId{
                std::string(value), capabilities::IdentityScope::persistent};
        } else {
            return std::nullopt;
        }
    }

    if (!bind_seen || !peer_seen) {
        return std::nullopt;
    }
    return options;
}

int run_voice_peer(std::span<const std::string_view> arguments,
                   audio::AudioPlatform& platform,
                   std::ostream& out,
                   std::ostream& error) {
    const auto options = parse_voice_peer_arguments(arguments);
    if (!options) {
        error << kVoicePeerUsage;
        return voice_peer_invalid_arguments;
    }

    auto socket_result = UdpPeerSocket::bind(options->bind);
    if (auto* failure = std::get_if<UdpError>(&socket_result)) {
        report_udp_error(error, *failure);
        return voice_peer_network_failed;
    }
    auto socket = std::move(std::get<std::unique_ptr<UdpPeerSocket>>(socket_result));
    const auto connected = socket->connect_peer(options->peer);
    if (const auto* failure = std::get_if<UdpError>(&connected)) {
        report_udp_error(error, *failure);
        return voice_peer_network_failed;
    }

    voice::VoicePipelineConfig media_config;
    media_config.local_stream_id = options->stream_id;
    media_config.jitter_target_packets = options->jitter_packets;
    media_config.encoder.bitrate = options->bitrate;
    auto pipeline_result = voice::VoicePipeline::create(media_config);
    if (auto* failure = std::get_if<voice::CodecError>(&pipeline_result)) {
        report_codec_error(error, *failure);
        return voice_peer_codec_failed;
    }
    auto pipeline = std::move(std::get<std::unique_ptr<voice::VoicePipeline>>(pipeline_result));

    std::atomic_bool audio_failed{false};
    audio::ExternalAudioSession audio_session(
        platform, [&](audio::AudioError) { audio_failed.store(true, std::memory_order_release); });
    const audio::ExternalSessionConfig audio_config{
        .mode = audio_mode(options->mode),
        .input = options->input,
        .output = options->output,
    };
    if (const auto failure =
            audio_session.start(audio_config, pipeline->capture(), pipeline->render())) {
        report_audio_error(error, *failure);
        return voice_peer_audio_failed;
    }

    auto initial_audio = audio_session.statistics();
    out << "mode: " << mode_name(options->mode) << '\n';
    out << "bind: " << endpoint_text(options->bind)
        << " -> peer: " << endpoint_text(options->peer) << '\n';
    out << "voice: 48000 Hz mono, 20 ms, Opus " << options->bitrate
        << " bit/s, jitter target " << options->jitter_packets * 20 << " ms\n";
    if (initial_audio.input) {
        out << "input: " << audio_info(*initial_audio.input) << '\n';
        if (initial_audio.input->device_sample_rate < 32'000) {
            error << "catro-voice-peer: warning: selected input is narrowband ("
                  << initial_audio.input->device_sample_rate
                  << " Hz device mix); prefer an active 48 kHz endpoint for voice validation\n";
        }
    }
    if (initial_audio.output) {
        out << "output: " << audio_info(*initial_audio.output) << '\n';
    }

    NetworkStatistics network;
    TimingAccumulator encode_timing;
    TimingAccumulator decode_timing;
    std::array<std::byte, voice::kMaxVoiceDatagramBytes> receive_buffer{};
    voice::OutboundDatagram outbound;

    const auto started_at = Clock::now();
    const auto deadline = started_at + options->duration;
    auto next_report = started_at + 1s;
    auto next_playout = started_at;
    bool playout_started = false;
    int exit_code = voice_peer_ok;
    std::optional<UdpError> network_failure;
    std::optional<voice::CodecError> codec_failure;
    std::optional<audio::AudioError> audio_failure;

    while (Clock::now() < deadline) {
        if (audio_failed.load(std::memory_order_acquire)) {
            const auto audio_stats = audio_session.statistics();
            audio_failure = audio_stats.error.value_or(audio::AudioError{audio::AudioErrorCode::os_failure});
            exit_code = voice_peer_audio_failed;
            break;
        }

        if (sends(options->mode)) {
            for (int drained = 0; drained < kMaxEncodeDrain; ++drained) {
                const auto before = Clock::now();
                const auto encoded = pipeline->encode_next(outbound);
                const auto after = Clock::now();
                if (const auto* failure = std::get_if<voice::CodecError>(&encoded)) {
                    codec_failure = *failure;
                    exit_code = voice_peer_codec_failed;
                    break;
                }
                if (std::get<voice::EncodeStep>(encoded) == voice::EncodeStep::no_frame) {
                    break;
                }
                encode_timing.add(after - before);

                const auto sent = socket->send(outbound.view());
                if (const auto* failure = std::get_if<UdpError>(&sent)) {
                    if (failure->code == UdpErrorCode::would_block) {
                        ++network.send_backpressure_drops;
                        continue;
                    }
                    network_failure = *failure;
                    exit_code = voice_peer_network_failed;
                    break;
                }
                const auto bytes = std::get<std::size_t>(sent);
                if (bytes != outbound.size) {
                    network_failure = UdpError{UdpErrorCode::send_failed};
                    exit_code = voice_peer_network_failed;
                    break;
                }
                ++network.sent_packets;
                network.sent_bytes += bytes;
            }
            if (exit_code != voice_peer_ok) {
                break;
            }
        }

        auto now = Clock::now();
        auto wake_at = std::min(deadline, now + kWorkerPoll);
        wake_at = std::min(wake_at, next_report);
        if (receives(options->mode) && playout_started) {
            wake_at = std::min(wake_at, next_playout);
        }
        auto timeout = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::max(Clock::duration::zero(), wake_at - now));

        if (receives(options->mode)) {
            const auto ready = socket->wait_readable(timeout);
            if (const auto* failure = std::get_if<UdpError>(&ready)) {
                network_failure = *failure;
                exit_code = voice_peer_network_failed;
                break;
            }
            if (std::get<bool>(ready)) {
                for (int drained = 0; drained < kMaxReceiveDrain; ++drained) {
                    const auto received = socket->receive(receive_buffer);
                    if (const auto* failure = std::get_if<UdpError>(&received)) {
                        network_failure = *failure;
                        exit_code = voice_peer_network_failed;
                        break;
                    }
                    const auto bytes = std::get<std::size_t>(received);
                    if (bytes == 0) {
                        break;
                    }
                    ++network.received_packets;
                    network.received_bytes += bytes;
                    (void)pipeline->receive(std::span<const std::byte>(receive_buffer).first(bytes));
                }
                if (exit_code != voice_peer_ok) {
                    break;
                }
            }
        } else if (timeout > std::chrono::milliseconds::zero()) {
            std::this_thread::sleep_for(timeout);
        } else {
            std::this_thread::yield();
        }

        now = Clock::now();
        if (receives(options->mode)) {
            if (!playout_started) {
                const auto before = Clock::now();
                const auto decoded = pipeline->decode_next();
                const auto after = Clock::now();
                if (const auto* failure = std::get_if<voice::CodecError>(&decoded)) {
                    codec_failure = *failure;
                    exit_code = voice_peer_codec_failed;
                    break;
                }
                if (std::get<voice::DecodeStep>(decoded) != voice::DecodeStep::waiting) {
                    decode_timing.add(after - before);
                    playout_started = true;
                    next_playout = after + kFramePeriod;
                }
            } else {
                int caught_up = 0;
                while (now >= next_playout && caught_up < kMaxPlayoutCatchup) {
                    const auto before = Clock::now();
                    const auto decoded = pipeline->decode_next();
                    const auto after = Clock::now();
                    if (const auto* failure = std::get_if<voice::CodecError>(&decoded)) {
                        codec_failure = *failure;
                        exit_code = voice_peer_codec_failed;
                        break;
                    }
                    if (std::get<voice::DecodeStep>(decoded) != voice::DecodeStep::waiting) {
                        decode_timing.add(after - before);
                    }
                    next_playout += kFramePeriod;
                    ++caught_up;
                    now = after;
                }
                if (exit_code != voice_peer_ok) {
                    break;
                }
                if (now >= next_playout) {
                    ++network.worker_late_resyncs;
                    next_playout = now + kFramePeriod;
                }
            }
        }

        now = Clock::now();
        if (now >= next_report) {
            const auto elapsed =
                std::chrono::duration_cast<std::chrono::seconds>(now - started_at).count();
            print_progress(out, elapsed, network, pipeline->statistics(),
                           audio_session.statistics(), encode_timing, decode_timing);
            do {
                next_report += 1s;
            } while (next_report <= now);
        }
    }

    const auto final_media = pipeline->statistics();
    const auto final_audio = audio_session.statistics();
    audio_session.stop();

    out << "final:"
        << " tx " << network.sent_packets
        << ", rx " << network.received_packets
        << ", net-drop " << network.send_backpressure_drops
        << ", malformed " << final_media.malformed_datagrams
        << ", duplicate " << final_media.jitter.duplicates
        << ", reordered " << final_media.jitter.reordered
        << ", late " << final_media.jitter.late
        << ", fec " << final_media.jitter.fec
        << ", plc " << final_media.jitter.plc
        << ", capture-drop " << final_media.capture.dropped_callbacks
        << ", render-full " << final_media.render_queue_full
        << ", underrun " << final_media.render.underrun_callbacks
        << ", startup-silence " << final_media.render.startup_silence_samples
        << ", glitches " << final_audio.glitches
        << ", enc avg/max " << encode_timing.average_us() << "/" << encode_timing.max_us << " us"
        << ", dec avg/max " << decode_timing.average_us() << "/" << decode_timing.max_us << " us"
        << ", worker-resync " << network.worker_late_resyncs
        << '\n';

    if (network_failure) {
        report_udp_error(error, *network_failure);
    }
    if (codec_failure) {
        report_codec_error(error, *codec_failure);
    }
    if (audio_failure) {
        report_audio_error(error, *audio_failure);
    }
    return exit_code;
}

} // namespace catro::tools
