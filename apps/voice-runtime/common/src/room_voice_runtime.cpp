#include <catro/room_voice_runtime.hpp>

#include <catro/audio/external_session.hpp>
#include <catro/voice/pipeline.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <string_view>
#include <utility>
#include <variant>

namespace catro::voice_runtime {
namespace {

constexpr auto kRoomVoicePoll = std::chrono::milliseconds{2};
constexpr auto kRoomVoiceFrame = std::chrono::milliseconds{20};
constexpr int kRoomEncodeDrain = 4;
constexpr int kRoomReceiveDrain = 64;
constexpr int kRoomPlayoutCatchup = 3;
// Default devices are compared once a second; a lost device is reopened every 500 ms for 10 s.
constexpr auto kDeviceCheck = std::chrono::seconds{1};
constexpr auto kAudioRetry = std::chrono::milliseconds{500};
constexpr int kAudioRetries = 20;

// True when the session runs on a system default that is no longer the default, e.g. after a
// headset was plugged in. Explicitly chosen devices are never switched.
bool default_moved(audio::AudioPlatform& platform, const audio::ExternalAudioSession& session,
                   const audio::ExternalSessionConfig& config) {
    const auto statistics = session.statistics();
    const auto moved = [&](const std::optional<capabilities::AudioEndpointId>& chosen,
                           const std::optional<audio::StreamInfo>& running, audio::DeviceDirection direction) {
        if (chosen || !running) {
            return false;
        }
        const auto current = platform.default_device(direction);
        return current && current->value != running->device.value;
    };
    return moved(config.input, statistics.input, audio::DeviceDirection::capture) ||
           moved(config.output, statistics.output, audio::DeviceDirection::render);
}

bool valid_media(const CatroVoiceRuntimeConfig& config) noexcept {
    return config.stream_id != 0 && config.jitter_packets >= 1 && config.jitter_packets <= 10 &&
           config.bitrate >= 12'000 && config.bitrate <= 128'000;
}

std::optional<capabilities::AudioEndpointId> endpoint(const char* value) {
    if (value == nullptr || value[0] == '\0') {
        return std::nullopt;
    }
    return capabilities::AudioEndpointId{std::string{value}, capabilities::IdentityScope::persistent};
}

// The production room loop: capture -> encode -> room, room -> jitter -> decode -> mix -> render.
// Encode and receive drains are bounded per pass, and playout that falls three frames behind
// resynchronizes the receiver instead of accumulating latency.
int run_room_voice(const tools::VoicePeerOptions& options,
                   CatroRoomRuntimeHandle room,
                   const RoomVoiceApi& api,
                   audio::AudioPlatform& platform,
                   tools::VoicePeerControl& control,
                   voice::StreamControls& streams,
                   const LiveVoiceSettings& settings,
                   std::string& error_text) {
    voice::VoicePipelineConfig media_config;
    media_config.local_stream_id = options.stream_id;
    media_config.jitter_target_packets = options.jitter_packets;
    media_config.encoder.bitrate = options.bitrate;
    // Echo cancellation, noise suppression, and automatic gain, on by default like Discord.
    media_config.processing = voice::VoiceProcessingConfig{
        .echo_cancellation = settings.echo_cancellation.load(std::memory_order_relaxed),
        .noise_suppression = settings.noise_suppression.load(std::memory_order_relaxed),
        .automatic_gain = settings.automatic_gain.load(std::memory_order_relaxed),
    };
    media_config.controls = &streams;

    auto pipeline_result = voice::VoicePipeline::create(media_config);
    if (const auto* failure = std::get_if<voice::CodecError>(&pipeline_result)) {
        error_text = std::string{"voice codec: "} + std::string{voice::name(failure->code)};
        return tools::voice_peer_codec_failed;
    }
    auto pipeline = std::move(std::get<std::unique_ptr<voice::VoicePipeline>>(pipeline_result));

    std::atomic_bool audio_failed{false};
    audio::ExternalAudioSession audio_session(
        platform, [&](audio::AudioError) { audio_failed.store(true, std::memory_order_release); });

    audio::ExternalSessionConfig audio_config;
    audio_config.mode = audio::ExternalSessionMode::duplex;
    audio_config.input = options.input;
    audio_config.output = options.output;
    if (const auto failure = audio_session.start(audio_config, pipeline->capture(), pipeline->render())) {
        error_text = std::string{"audio: "} + std::string{audio::name(failure->code)};
        return tools::voice_peer_audio_failed;
    }
    auto next_device_check = std::chrono::steady_clock::now() + kDeviceCheck;
    auto next_audio_retry = std::chrono::steady_clock::time_point{};
    int audio_retries = 0;
    // Reopens both streams on the current devices; the call itself never drops.
    const auto reopen_audio = [&]() -> bool {
        audio_failed.store(false, std::memory_order_release);
        if (audio_session.start(audio_config, pipeline->capture(), pipeline->render())) {
            audio_failed.store(true, std::memory_order_release);
            return false;
        }
        return true;
    };

    control.media_started.store(true, std::memory_order_release);

    std::array<std::byte, voice::kMaxVoiceDatagramBytes + 1> receive_buffer{};
    voice::OutboundDatagram outbound;
    bool playout_started = false;
    auto next_playout = std::chrono::steady_clock::now();
    int exit_code = tools::voice_peer_ok;

    const auto codec_failure = [&](std::string_view stage, const voice::CodecError& failure) {
        error_text = std::string{stage} + std::string{voice::name(failure.code)};
        exit_code = tools::voice_peer_codec_failed;
    };

    const auto consume = [&](std::size_t bytes) -> bool {
        control.received_packets.fetch_add(1, std::memory_order_relaxed);
        const auto step = pipeline->receive(std::span<const std::byte>(receive_buffer.data(), bytes));
        if (const auto* failure = std::get_if<voice::CodecError>(&step)) {
            codec_failure("voice receive: ", *failure);
            return false;
        }
        return true;
    };

    // Forces the first pass to apply the threshold and processing settings.
    auto applied_settings = settings.version.load(std::memory_order_acquire) - 1U;
    while (!control.stop_requested.load(std::memory_order_acquire)) {
        const bool deafened = control.deafened.load(std::memory_order_acquire);
        pipeline->set_deafened(deafened);
        pipeline->set_muted(deafened || control.muted.load(std::memory_order_acquire) ||
                            !settings.transmit.load(std::memory_order_acquire));
        if (const auto version = settings.version.load(std::memory_order_acquire); version != applied_settings) {
            applied_settings = version;
            pipeline->set_processing({
                .echo_cancellation = settings.echo_cancellation.load(std::memory_order_relaxed),
                .noise_suppression = settings.noise_suppression.load(std::memory_order_relaxed),
                .automatic_gain = settings.automatic_gain.load(std::memory_order_relaxed),
            });
            const auto threshold = settings.input_threshold_db.load(std::memory_order_relaxed);
            pipeline->set_input_threshold(std::isnan(threshold) ? std::nullopt : std::optional<float>{threshold});
        }

        const auto room_snapshot = api.snapshot(room);
        if (room_snapshot.state == CATRO_ROOM_FAILED) {
            error_text = room_snapshot.error[0] != '\0' ? room_snapshot.error : "RTC room transport failed";
            exit_code = tools::voice_peer_network_failed;
            break;
        }
        if (const auto now = std::chrono::steady_clock::now(); audio_failed.load(std::memory_order_acquire)) {
            if (now >= next_audio_retry) {
                if (++audio_retries > kAudioRetries) {
                    error_text = "audio session failed";
                    exit_code = tools::voice_peer_audio_failed;
                    break;
                }
                next_audio_retry = now + kAudioRetry;
                if (reopen_audio()) {
                    audio_retries = 0;
                    control.audio_restarts.fetch_add(1, std::memory_order_relaxed);
                }
            }
        } else if (now >= next_device_check) {
            next_device_check = now + kDeviceCheck;
            if (default_moved(platform, audio_session, audio_config) && reopen_audio()) {
                control.audio_restarts.fetch_add(1, std::memory_order_relaxed);
            }
        }

        for (int drained = 0; drained < kRoomEncodeDrain; ++drained) {
            const auto encoded = pipeline->encode_next(outbound);
            if (const auto* failure = std::get_if<voice::CodecError>(&encoded)) {
                codec_failure("voice encode: ", *failure);
                break;
            }
            if (std::get<voice::EncodeStep>(encoded) == voice::EncodeStep::no_frame) {
                break;
            }
            if (api.send_voice(room, outbound.bytes.data(), outbound.size) != 0) {
                control.sent_packets.fetch_add(1, std::memory_order_relaxed);
            }
        }
        if (exit_code != tools::voice_peer_ok) {
            break;
        }

        const auto received = api.receive_voice(room, receive_buffer.data(), receive_buffer.size(),
                                                static_cast<std::uint32_t>(kRoomVoicePoll.count()));
        if (received < 0) {
            error_text = "RTC room voice receive failed";
            exit_code = tools::voice_peer_network_failed;
            break;
        }
        if (received > 0 && !consume(static_cast<std::size_t>(received))) {
            break;
        }
        for (int drained = 1; received > 0 && drained < kRoomReceiveDrain; ++drained) {
            const auto more = api.receive_voice(room, receive_buffer.data(), receive_buffer.size(), 0);
            if (more <= 0) {
                break;
            }
            if (!consume(static_cast<std::size_t>(more))) {
                break;
            }
        }
        if (exit_code != tools::voice_peer_ok) {
            break;
        }

        auto now = std::chrono::steady_clock::now();
        if (!playout_started) {
            const auto decoded = pipeline->decode_next();
            if (const auto* failure = std::get_if<voice::CodecError>(&decoded)) {
                codec_failure("voice decode: ", *failure);
                break;
            }
            if (std::get<voice::DecodeStep>(decoded) != voice::DecodeStep::waiting) {
                playout_started = true;
                next_playout = now + kRoomVoiceFrame;
            }
        } else if (now - next_playout >= kRoomVoiceFrame * kRoomPlayoutCatchup) {
            if (const auto failure = pipeline->resynchronize_receiver()) {
                codec_failure("voice resync: ", *failure);
                break;
            }
            playout_started = false;
            next_playout = now;
        } else {
            for (int caught_up = 0; now >= next_playout && caught_up < kRoomPlayoutCatchup; ++caught_up) {
                const auto decoded = pipeline->decode_next();
                if (const auto* failure = std::get_if<voice::CodecError>(&decoded)) {
                    codec_failure("voice decode: ", *failure);
                    break;
                }
                next_playout += kRoomVoiceFrame;
                now = std::chrono::steady_clock::now();
            }
            if (exit_code != tools::voice_peer_ok) {
                break;
            }
        }
    }

    audio_session.stop();
    control.media_started.store(false, std::memory_order_release);
    control.last_exit_code.store(exit_code, std::memory_order_release);
    return exit_code;
}

} // namespace

VoiceRuntimeHost::VoiceRuntimeHost(audio::AudioPlatform& platform, RoomVoiceApi room, DirectPeerRunner direct)
    : platform_(platform), room_(room), direct_(std::move(direct)) {}

VoiceRuntimeHost::~VoiceRuntimeHost() {
    stop();
}

std::int32_t VoiceRuntimeHost::start(const CatroVoiceRuntimeConfig& config) noexcept {
    std::scoped_lock lifecycle_lock(lifecycle_mutex_);
    stop_locked();

    const auto room = config.room_runtime;
    const bool direct_ready = config.bind_address != nullptr && config.peer_address != nullptr &&
                              config.bind_address[0] != '\0' && config.peer_address[0] != '\0' &&
                              config.bind_port != 0 && config.peer_port != 0;
    if (!valid_media(config) || (room == nullptr && (!direct_ || !direct_ready))) {
        state_.store(CATRO_VOICE_FAILED, std::memory_order_release);
        exit_code_.store(tools::voice_peer_invalid_arguments, std::memory_order_release);
        set_error(room == nullptr && !direct_ ? "voice runtime requires an RTC room"
                                              : "invalid voice runtime configuration");
        return tools::voice_peer_invalid_arguments;
    }

    control_.stop_requested.store(false, std::memory_order_relaxed);
    control_.muted.store(false, std::memory_order_relaxed);
    control_.deafened.store(false, std::memory_order_relaxed);
    control_.media_started.store(false, std::memory_order_relaxed);
    control_.sent_packets.store(0, std::memory_order_relaxed);
    control_.received_packets.store(0, std::memory_order_relaxed);
    control_.peer_unreachable_events.store(0, std::memory_order_relaxed);
    control_.audio_restarts.store(0, std::memory_order_relaxed);
    control_.last_exit_code.store(-1, std::memory_order_relaxed);
    exit_code_.store(-1, std::memory_order_relaxed);
    state_.store(CATRO_VOICE_STARTING, std::memory_order_release);
    set_error({});

    try {
        tools::VoicePeerOptions options;
        options.mode = tools::VoicePeerMode::duplex;
        if (room == nullptr) {
            options.bind = {config.bind_address, config.bind_port};
            options.peer = {config.peer_address, config.peer_port};
        }
        options.duration = std::chrono::hours(24);
        options.stream_id = config.stream_id;
        options.jitter_packets = config.jitter_packets;
        options.bitrate = config.bitrate;
        options.input = endpoint(config.input_endpoint);
        options.output = endpoint(config.output_endpoint);

        worker_ = std::thread([this, room, options = std::move(options)] {
            std::string error;
            int code = tools::voice_peer_ok;
            if (room != nullptr) {
                code = run_room_voice(options, room, room_, platform_, control_, streams_, settings_, error);
            } else {
                std::ostringstream stream;
                code = direct_(options, control_, stream);
                error = stream.str();
            }

            exit_code_.store(code, std::memory_order_release);
            if (!error.empty()) {
                set_error(std::move(error));
            }
            const bool requested_stop = control_.stop_requested.load(std::memory_order_acquire);
            state_.store(requested_stop || code == tools::voice_peer_ok ? CATRO_VOICE_IDLE : CATRO_VOICE_FAILED,
                         std::memory_order_release);
        });
    } catch (...) {
        state_.store(CATRO_VOICE_FAILED, std::memory_order_release);
        exit_code_.store(tools::voice_peer_network_failed, std::memory_order_release);
        set_error("failed to start voice worker");
        return tools::voice_peer_network_failed;
    }
    return tools::voice_peer_ok;
}

void VoiceRuntimeHost::stop() noexcept {
    std::scoped_lock lifecycle_lock(lifecycle_mutex_);
    stop_locked();
}

void VoiceRuntimeHost::set_muted(bool value) noexcept {
    control_.muted.store(value, std::memory_order_release);
}

void VoiceRuntimeHost::set_deafened(bool value) noexcept {
    control_.deafened.store(value, std::memory_order_release);
}

CatroVoiceRuntimeSnapshot VoiceRuntimeHost::snapshot() const noexcept {
    CatroVoiceRuntimeSnapshot result{};
    auto state = state_.load(std::memory_order_acquire);
    if (state == CATRO_VOICE_STARTING && control_.media_started.load(std::memory_order_acquire)) {
        state = CATRO_VOICE_JOINED;
    }
    result.state = state;
    result.exit_code = exit_code_.load(std::memory_order_acquire);
    result.muted = control_.muted.load(std::memory_order_acquire) ? 1U : 0U;
    result.deafened = control_.deafened.load(std::memory_order_acquire) ? 1U : 0U;
    result.sent_packets = control_.sent_packets.load(std::memory_order_relaxed);
    result.received_packets = control_.received_packets.load(std::memory_order_relaxed);
    result.peer_unreachable_events = control_.peer_unreachable_events.load(std::memory_order_relaxed);
    result.peer_seen = result.received_packets > 0 ? 1U : 0U;
    result.speaking = streams_.local_speaking() ? 1U : 0U;
    result.input_level = streams_.local_level();
    result.audio_restarts = static_cast<std::uint32_t>(control_.audio_restarts.load(std::memory_order_relaxed));

    std::scoped_lock error_lock(error_mutex_);
    if (!error_.empty()) {
        std::snprintf(result.error, sizeof(result.error), "%.*s", static_cast<int>(sizeof(result.error) - 1),
                      error_.c_str());
    }
    return result;
}

void VoiceRuntimeHost::set_user_volume(const char* user_id, float volume) noexcept {
    if (user_id != nullptr && user_id[0] != '\0') {
        streams_.set_volume(voice::user_stream_id(user_id), volume);
    }
}

void VoiceRuntimeHost::set_processing(bool echo_cancellation, bool noise_suppression,
                                      bool automatic_gain) noexcept {
    settings_.echo_cancellation.store(echo_cancellation, std::memory_order_relaxed);
    settings_.noise_suppression.store(noise_suppression, std::memory_order_relaxed);
    settings_.automatic_gain.store(automatic_gain, std::memory_order_relaxed);
    settings_.version.fetch_add(1, std::memory_order_release);
}

void VoiceRuntimeHost::set_input_threshold(float dbfs) noexcept {
    settings_.input_threshold_db.store(dbfs, std::memory_order_relaxed);
    settings_.version.fetch_add(1, std::memory_order_release);
}

void VoiceRuntimeHost::set_transmit(bool transmit) noexcept {
    settings_.transmit.store(transmit, std::memory_order_release);
}

bool VoiceRuntimeHost::user_speaking(const char* user_id) const noexcept {
    if (user_id == nullptr || user_id[0] == '\0') {
        return false;
    }
    const auto stream_id = voice::user_stream_id(user_id);
    std::array<std::uint32_t, voice::kMaxControlledStreams> speaking{};
    const auto count = streams_.speaking(speaking);
    return std::find(speaking.begin(), speaking.begin() + static_cast<std::ptrdiff_t>(count), stream_id) !=
           speaking.begin() + static_cast<std::ptrdiff_t>(count);
}

void VoiceRuntimeHost::stop_locked() noexcept {
    control_.stop_requested.store(true, std::memory_order_release);
    if (worker_.joinable()) {
        worker_.join();
    }
    state_.store(CATRO_VOICE_IDLE, std::memory_order_release);
    control_.media_started.store(false, std::memory_order_relaxed);
}

void VoiceRuntimeHost::set_error(std::string value) noexcept {
    try {
        std::scoped_lock lock(error_mutex_);
        if (value.size() > 512) {
            value.resize(512);
        }
        error_ = std::move(value);
    } catch (...) {
        // Diagnostics must never make media lifecycle fail.
    }
}

} // namespace catro::voice_runtime
