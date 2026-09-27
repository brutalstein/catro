#include "voice_peer.hpp"
#include "worker_priority.hpp"

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
constexpr int kStartupRenderPrimeFrames = 2;

struct NetworkStatistics {
    std::uint64_t sent_packets = 0;
    std::uint64_t sent_bytes = 0;
    std::uint64_t send_backpressure_drops = 0;
    std::uint64_t received_packets = 0;
    std::uint64_t received_bytes = 0;
    std::uint64_t oversized_packets = 0;
    std::uint64_t peer_unreachable_events = 0;
    std::uint64_t stale_network_packets_discarded = 0;
    std::uint64_t startup_prime_frames = 0;
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
        << " oversize " << network.oversized_packets
        << " peer-miss " << network.peer_unreachable_events
        << " net-stale " << network.stale_network_packets_discarded
        << " malformed " << media.malformed_datagrams
        << " dup " << media.jitter.duplicates
        << " reorder " << media.jitter.reordered
        << " late " << media.jitter.late
        << " window " << media.jitter.outside_window
        << " wrong-stream " << media.jitter.wrong_stream
        << " fec " << media.jitter.fec
        << " plc " << media.jitter.plc
        << " jitter " << media.jitter.buffered << "/" << media.jitter.peak_buffered
        << " cap-q " << media.capture.buffered_samples
        << " cap-drop " << media.capture.dropped_callbacks
        << " cap-stale " << media.capture.stale_frames_discarded
        << " cap-resync " << media.capture.resync_events
        << " cap-skip " << media.capture.timeline_frames_skipped
        << " enc-err " << media.encode_errors
        << " dec-err " << media.decode_errors
        << " render-q " << media.render.buffered_samples
        << " render-full " << media.render_queue_full
        << " render-resync " << media.render.resync_events
        << " prime " << network.startup_prime_frames
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

int run_voice_peer(const VoicePeerOptions& options,
                   audio::AudioPlatform& platform,
                   std::ostream& out,
                   std::ostream& error,
                   VoicePeerControl* control) {
    if (control != nullptr) {
        control->media_started.store(false, std::memory_order_relaxed);
        control->sent_packets.store(0, std::memory_order_relaxed);
        control->received_packets.store(0, std::memory_order_relaxed);
        control->peer_unreachable_events.store(0, std::memory_order_relaxed);
        control->last_exit_code.store(-1, std::memory_order_relaxed);
    }

    if (options.bind.address.empty() || options.peer.address.empty() ||
        options.bind.port == 0 || options.peer.port == 0 || options.stream_id == 0 ||
        options.duration <= std::chrono::seconds::zero()) {
        error << kVoicePeerUsage;
        return voice_peer_invalid_arguments;
    }

    auto socket_result = UdpPeerSocket::bind(options.bind);
    if (auto* failure = std::get_if<UdpError>(&socket_result)) {
        report_udp_error(error, *failure);
        return voice_peer_network_failed;
    }
    auto socket = std::move(std::get<std::unique_ptr<UdpPeerSocket>>(socket_result));
    const auto connected = socket->connect_peer(options.peer);
    if (const auto* failure = std::get_if<UdpError>(&connected)) {
        report_udp_error(error, *failure);
        return voice_peer_network_failed;
    }

    voice::VoicePipelineConfig media_config;
    media_config.local_stream_id = options.stream_id;
    media_config.jitter_target_packets = options.jitter_packets;
    media_config.encoder.bitrate = options.bitrate;
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
        .mode = audio_mode(options.mode),
        .input = options.input,
        .output = options.output,
    };
    if (const auto failure =
            audio_session.start(audio_config, pipeline->capture(), pipeline->render())) {
        report_audio_error(error, *failure);
        if (control != nullptr) {
            control->last_exit_code.store(voice_peer_audio_failed, std::memory_order_release);
        }
        return voice_peer_audio_failed;
    }
    if (control != nullptr) {
        control->media_started.store(true, std::memory_order_release);
    }

    auto initial_audio = audio_session.statistics();
    out << "mode: " << mode_name(options.mode) << '\n';
    out << "bind: " << endpoint_text(options.bind)
        << " -> peer: " << endpoint_text(options.peer) << '\n';
    out << "voice: 48000 Hz mono, 20 ms, Opus " << options.bitrate
        << " bit/s, jitter target " << options.jitter_packets * 20 << " ms\n";
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

    VoiceWorkerPriority worker_priority;
    out << "worker priority: " << (worker_priority.elevated() ? "elevated" : "normal") << '\n';

    NetworkStatistics network;
    TimingAccumulator encode_timing;
    TimingAccumulator decode_timing;
    std::array<std::byte, voice::kMaxVoiceDatagramBytes + 1> receive_buffer{};
    voice::OutboundDatagram outbound;

    const auto started_at = Clock::now();
    const auto deadline = started_at + options.duration;
    auto next_report = started_at + 1s;
    auto next_playout = started_at;
    bool playout_started = false;
    bool discard_network_backlog = false;
    int exit_code = voice_peer_ok;
    std::optional<UdpError> network_failure;
    std::optional<voice::CodecError> codec_failure;
    std::optional<audio::AudioError> audio_failure;

    while (Clock::now() < deadline &&
           (control == nullptr || !control->stop_requested.load(std::memory_order_acquire))) {
        if (control != nullptr) {
            const bool deafened = control->deafened.load(std::memory_order_acquire);
            pipeline->set_deafened(deafened);
            pipeline->set_muted(deafened || control->muted.load(std::memory_order_acquire));
        }
        if (audio_failed.load(std::memory_order_acquire)) {
            const auto audio_stats = audio_session.statistics();
            audio_failure = audio_stats.error.value_or(audio::AudioError{audio::AudioErrorCode::os_failure});
            exit_code = voice_peer_audio_failed;
            break;
        }

        // If the worker was descheduled long enough to miss the bounded catch-up window, do not
        // feed seconds of queued UDP speech back into the jitter buffer. Reset codec/jitter state
        // and drain the kernel socket to its live edge first.
        const auto loop_now = Clock::now();
        if (receives(options.mode) && playout_started &&
            loop_now - next_playout >= kFramePeriod * kMaxPlayoutCatchup) {
            if (const auto failure = pipeline->resynchronize_receiver()) {
                codec_failure = *failure;
                exit_code = voice_peer_codec_failed;
                break;
            }
            ++network.worker_late_resyncs;
            playout_started = false;
            discard_network_backlog = true;
            next_playout = loop_now;
        }

        if (sends(options.mode)) {
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
                    if (failure->code == UdpErrorCode::peer_unreachable) {
                        ++network.peer_unreachable_events;
                        if (control != nullptr) {
                            control->peer_unreachable_events.fetch_add(1, std::memory_order_relaxed);
                        }
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
                if (control != nullptr) {
                    control->sent_packets.fetch_add(1, std::memory_order_relaxed);
                }
            }
            if (exit_code != voice_peer_ok) {
                break;
            }
        }

        auto now = Clock::now();
        auto wake_at = std::min(deadline, now + kWorkerPoll);
        wake_at = std::min(wake_at, next_report);
        if (receives(options.mode) && playout_started) {
            wake_at = std::min(wake_at, next_playout);
        }
        const auto remaining = std::max(Clock::duration::zero(), wake_at - now);
        auto timeout = std::chrono::duration_cast<std::chrono::microseconds>(remaining);
        if (remaining > Clock::duration::zero() && timeout == std::chrono::microseconds::zero()) {
            timeout = 1us;
        }

        if (receives(options.mode)) {
            const auto ready = socket->wait_readable(timeout);
            if (const auto* failure = std::get_if<UdpError>(&ready)) {
                network_failure = *failure;
                exit_code = voice_peer_network_failed;
                break;
            }
            if (!std::get<bool>(ready) && discard_network_backlog) {
                // select observed an empty receive queue, so the next packet is live media.
                discard_network_backlog = false;
            }
            if (std::get<bool>(ready)) {
                for (int drained = 0; drained < kMaxReceiveDrain; ++drained) {
                    const auto received = socket->receive(receive_buffer);
                    if (const auto* failure = std::get_if<UdpError>(&received)) {
                        if (failure->code == UdpErrorCode::datagram_too_large) {
                            ++network.oversized_packets;
                            continue;
                        }
                        if (failure->code == UdpErrorCode::peer_unreachable) {
                            ++network.peer_unreachable_events;
                            if (control != nullptr) {
                                control->peer_unreachable_events.fetch_add(1, std::memory_order_relaxed);
                            }
                            break;
                        }
                        network_failure = *failure;
                        exit_code = voice_peer_network_failed;
                        break;
                    }
                    const auto bytes = std::get<std::size_t>(received);
                    if (bytes == 0) {
                        discard_network_backlog = false;
                        break;
                    }
                    ++network.received_packets;
                    network.received_bytes += bytes;
                    if (control != nullptr) {
                        control->received_packets.fetch_add(1, std::memory_order_relaxed);
                    }
                    if (discard_network_backlog) {
                        ++network.stale_network_packets_discarded;
                        continue;
                    }
                    (void)pipeline->receive(std::span<const std::byte>(receive_buffer).first(bytes));
                }
                if (exit_code != voice_peer_ok) {
                    break;
                }
            }
        } else if (timeout > std::chrono::microseconds::zero()) {
            std::this_thread::sleep_for(timeout);
        } else {
            std::this_thread::yield();
        }

        now = Clock::now();
        if (receives(options.mode)) {
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
                    ++network.startup_prime_frames;
                    playout_started = true;

                    // Prime one extra real packet when the jitter store already has it. This keeps
                    // roughly 20 ms of decoded PCM ahead of the 10 ms render callback without
                    // manufacturing early PLC or increasing first-audio latency.
                    for (int primed = 1; primed < kStartupRenderPrimeFrames; ++primed) {
                        const auto next_kind = pipeline->next_playout_kind();
                        if (next_kind == voice::PlayoutKind::waiting ||
                            next_kind == voice::PlayoutKind::plc) {
                            break;
                        }
                        const auto extra_before = Clock::now();
                        const auto extra = pipeline->decode_next();
                        const auto extra_after = Clock::now();
                        if (const auto* failure = std::get_if<voice::CodecError>(&extra)) {
                            codec_failure = *failure;
                            exit_code = voice_peer_codec_failed;
                            break;
                        }
                        if (std::get<voice::DecodeStep>(extra) == voice::DecodeStep::waiting) {
                            break;
                        }
                        decode_timing.add(extra_after - extra_before);
                        ++network.startup_prime_frames;
                    }
                    if (exit_code != voice_peer_ok) {
                        break;
                    }
                    next_playout = Clock::now() + kFramePeriod;
                }
            } else {
                const auto resynchronize_live_edge = [&]() {
                    if (const auto failure = pipeline->resynchronize_receiver()) {
                        codec_failure = *failure;
                        exit_code = voice_peer_codec_failed;
                        return false;
                    }
                    ++network.worker_late_resyncs;
                    playout_started = false;
                    discard_network_backlog = true;
                    next_playout = Clock::now();
                    return true;
                };

                if (now - next_playout >= kFramePeriod * kMaxPlayoutCatchup) {
                    if (!resynchronize_live_edge()) {
                        break;
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
                    if (now >= next_playout && !resynchronize_live_edge()) {
                        break;
                    }
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
        << ", oversize " << network.oversized_packets
        << ", peer-miss " << network.peer_unreachable_events
        << ", net-stale " << network.stale_network_packets_discarded
        << ", malformed " << final_media.malformed_datagrams
        << ", duplicate " << final_media.jitter.duplicates
        << ", reordered " << final_media.jitter.reordered
        << ", late " << final_media.jitter.late
        << ", fec " << final_media.jitter.fec
        << ", plc " << final_media.jitter.plc
        << ", capture-drop " << final_media.capture.dropped_callbacks
        << ", capture-stale " << final_media.capture.stale_frames_discarded
        << ", capture-resync " << final_media.capture.resync_events
        << ", capture-skip " << final_media.capture.timeline_frames_skipped
        << ", encode-errors " << final_media.encode_errors
        << ", decode-errors " << final_media.decode_errors
        << ", render-full " << final_media.render_queue_full
        << ", render-resync " << final_media.render.resync_events
        << ", render-stale " << final_media.render.stale_samples_discarded
        << ", startup-prime " << network.startup_prime_frames
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
    if (control != nullptr) {
        control->media_started.store(false, std::memory_order_release);
        control->last_exit_code.store(exit_code, std::memory_order_release);
    }
    return exit_code;
}

int run_voice_peer(std::span<const std::string_view> arguments,
                   audio::AudioPlatform& platform,
                   std::ostream& out,
                   std::ostream& error,
                   VoicePeerControl* control) {
    const auto options = parse_voice_peer_arguments(arguments);
    if (!options) {
        error << kVoicePeerUsage;
        return voice_peer_invalid_arguments;
    }
    return run_voice_peer(*options, platform, out, error, control);
}

} // namespace catro::tools
