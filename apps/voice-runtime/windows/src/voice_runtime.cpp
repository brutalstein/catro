#include <catro/voice_runtime.h>
#include <catro/room_runtime.h>

#include "voice_peer.hpp"

#include <catro/audio/external_session.hpp>
#include <catro/platform/windows/audio_platform.hpp>
#include <catro/voice/pipeline.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <new>
#include <ostream>
#include <sstream>
#include <streambuf>
#include <string>
#include <thread>
#include <utility>

namespace {

class NullBuffer final : public std::streambuf {
protected:
    int_type overflow(int_type value) override {
        return traits_type::not_eof(value);
    }
};

class NullStream final : public std::ostream {
public:
    NullStream() : std::ostream(&buffer_) {}

private:
    NullBuffer buffer_;
};


constexpr auto kRoomVoicePoll = std::chrono::milliseconds{2};
constexpr auto kRoomVoiceFrame = std::chrono::milliseconds{20};
constexpr int kRoomEncodeDrain = 4;
constexpr int kRoomReceiveDrain = 64;
constexpr int kRoomPlayoutCatchup = 3;

int run_room_voice(
    const CatroVoiceRuntimeConfig& config,
    catro::platform::windows::WasapiAudioPlatform& platform,
    catro::tools::VoicePeerControl& control,
    std::string& error_text) {
    if (config.room_runtime == nullptr ||
        config.stream_id == 0 ||
        config.jitter_packets < 1 ||
        config.jitter_packets > 10 ||
        config.bitrate < 12'000 ||
        config.bitrate > 128'000) {
        error_text = "invalid room voice configuration";
        return catro::tools::voice_peer_invalid_arguments;
    }

    catro::voice::VoicePipelineConfig media_config;
    media_config.local_stream_id = config.stream_id;
    media_config.jitter_target_packets = config.jitter_packets;
    media_config.encoder.bitrate = config.bitrate;

    auto pipeline_result =
        catro::voice::VoicePipeline::create(media_config);
    if (const auto* failure =
            std::get_if<catro::voice::CodecError>(
                &pipeline_result)) {
        error_text =
            std::string{"voice codec: "} +
            catro::voice::name(failure->code);
        return catro::tools::voice_peer_codec_failed;
    }
    auto pipeline =
        std::move(std::get<
            std::unique_ptr<catro::voice::VoicePipeline>>(
            pipeline_result));

    std::atomic_bool audio_failed{false};
    catro::audio::ExternalAudioSession audio_session(
        platform,
        [&](catro::audio::AudioError) {
            audio_failed.store(
                true, std::memory_order_release);
        });

    catro::audio::ExternalSessionConfig audio_config;
    audio_config.mode =
        catro::audio::ExternalSessionMode::duplex;
    if (config.input_endpoint != nullptr &&
        config.input_endpoint[0] != '\0') {
        audio_config.input =
            catro::capabilities::AudioEndpointId{
                std::string{config.input_endpoint},
                catro::capabilities::IdentityScope::persistent};
    }
    if (config.output_endpoint != nullptr &&
        config.output_endpoint[0] != '\0') {
        audio_config.output =
            catro::capabilities::AudioEndpointId{
                std::string{config.output_endpoint},
                catro::capabilities::IdentityScope::persistent};
    }

    if (const auto failure =
            audio_session.start(
                audio_config,
                pipeline->capture(),
                pipeline->render())) {
        error_text =
            std::string{"audio: "} +
            catro::audio::name(failure->code);
        return catro::tools::voice_peer_audio_failed;
    }

    control.media_started.store(
        true, std::memory_order_release);

    auto* const room =
        static_cast<CatroRoomRuntimeHandle>(
            config.room_runtime);
    std::array<
        std::byte,
        catro::voice::kMaxVoiceDatagramBytes + 1>
        receive_buffer{};
    catro::voice::OutboundDatagram outbound;

    bool playout_started = false;
    auto next_playout =
        std::chrono::steady_clock::now();

    int exit_code = catro::tools::voice_peer_ok;

    while (!control.stop_requested.load(
               std::memory_order_acquire)) {
        const bool deafened =
            control.deafened.load(
                std::memory_order_acquire);
        pipeline->set_deafened(deafened);
        pipeline->set_muted(
            deafened ||
            control.muted.load(
                std::memory_order_acquire));

        const auto room_snapshot =
            catro_room_runtime_snapshot(room);
        if (room_snapshot.state == CATRO_ROOM_FAILED) {
            error_text =
                room_snapshot.error[0] != '\0'
                    ? room_snapshot.error
                    : "RTC room transport failed";
            exit_code =
                catro::tools::voice_peer_network_failed;
            break;
        }

        if (audio_failed.load(
                std::memory_order_acquire)) {
            error_text = "audio session failed";
            exit_code =
                catro::tools::voice_peer_audio_failed;
            break;
        }

        for (int drained = 0;
             drained < kRoomEncodeDrain;
             ++drained) {
            const auto encoded =
                pipeline->encode_next(outbound);
            if (const auto* failure =
                    std::get_if<
                        catro::voice::CodecError>(
                        &encoded)) {
                error_text =
                    std::string{"voice encode: "} +
                    catro::voice::name(
                        failure->code);
                exit_code =
                    catro::tools::
                        voice_peer_codec_failed;
                break;
            }
            if (std::get<catro::voice::EncodeStep>(
                    encoded) ==
                catro::voice::EncodeStep::no_frame) {
                break;
            }

            const auto peers =
                catro_room_runtime_send_voice(
                    room,
                    outbound.bytes.data(),
                    outbound.size);
            if (peers != 0) {
                control.sent_packets.fetch_add(
                    1, std::memory_order_relaxed);
            }
        }
        if (exit_code !=
            catro::tools::voice_peer_ok) {
            break;
        }

        const auto received =
            catro_room_runtime_receive_voice(
                room,
                receive_buffer.data(),
                receive_buffer.size(),
                static_cast<std::uint32_t>(
                    kRoomVoicePoll.count()));
        if (received < 0) {
            error_text =
                "RTC room voice receive failed";
            exit_code =
                catro::tools::voice_peer_network_failed;
            break;
        }

        auto consume =
            [&](std::size_t bytes) -> bool {
                if (bytes == 0) {
                    return true;
                }
                control.received_packets.fetch_add(
                    1, std::memory_order_relaxed);
                const auto step =
                    pipeline->receive(
                        std::span<const std::byte>(
                            receive_buffer.data(),
                            bytes));
                if (const auto* failure =
                        std::get_if<
                            catro::voice::CodecError>(
                            &step)) {
                    error_text =
                        std::string{
                            "voice receive: "} +
                        catro::voice::name(
                            failure->code);
                    return false;
                }
                return true;
            };

        if (received > 0 &&
            !consume(
                static_cast<std::size_t>(
                    received))) {
            exit_code =
                catro::tools::voice_peer_codec_failed;
            break;
        }

        for (int drained = 1;
             received > 0 &&
             drained < kRoomReceiveDrain;
             ++drained) {
            const auto more =
                catro_room_runtime_receive_voice(
                    room,
                    receive_buffer.data(),
                    receive_buffer.size(),
                    0);
            if (more <= 0) {
                break;
            }
            if (!consume(
                    static_cast<std::size_t>(
                        more))) {
                exit_code =
                    catro::tools::
                        voice_peer_codec_failed;
                break;
            }
        }
        if (exit_code !=
            catro::tools::voice_peer_ok) {
            break;
        }

        auto now =
            std::chrono::steady_clock::now();
        if (!playout_started) {
            const auto decoded =
                pipeline->decode_next();
            if (const auto* failure =
                    std::get_if<
                        catro::voice::CodecError>(
                        &decoded)) {
                error_text =
                    std::string{"voice decode: "} +
                    catro::voice::name(
                        failure->code);
                exit_code =
                    catro::tools::
                        voice_peer_codec_failed;
                break;
            }
            if (std::get<catro::voice::DecodeStep>(
                    decoded) !=
                catro::voice::DecodeStep::waiting) {
                playout_started = true;
                next_playout =
                    now + kRoomVoiceFrame;
            }
        } else {
            if (now - next_playout >=
                kRoomVoiceFrame *
                    kRoomPlayoutCatchup) {
                if (const auto failure =
                        pipeline
                            ->resynchronize_receiver()) {
                    error_text =
                        std::string{
                            "voice resync: "} +
                        catro::voice::name(
                            failure->code);
                    exit_code =
                        catro::tools::
                            voice_peer_codec_failed;
                    break;
                }
                playout_started = false;
                next_playout = now;
            } else {
                int caught_up = 0;
                while (now >= next_playout &&
                       caught_up <
                           kRoomPlayoutCatchup) {
                    const auto decoded =
                        pipeline->decode_next();
                    if (const auto* failure =
                            std::get_if<
                                catro::voice::CodecError>(
                                &decoded)) {
                        error_text =
                            std::string{
                                "voice decode: "} +
                            catro::voice::name(
                                failure->code);
                        exit_code =
                            catro::tools::
                                voice_peer_codec_failed;
                        break;
                    }
                    next_playout +=
                        kRoomVoiceFrame;
                    ++caught_up;
                    now =
                        std::chrono::
                            steady_clock::now();
                }
            }
        }
    }

    audio_session.stop();
    control.media_started.store(
        false, std::memory_order_release);
    control.last_exit_code.store(
        exit_code, std::memory_order_release);
    return exit_code;
}

class VoiceRuntime final {
public:
    VoiceRuntime() = default;
    ~VoiceRuntime() {
        stop();
    }

    VoiceRuntime(const VoiceRuntime&) = delete;
    VoiceRuntime& operator=(const VoiceRuntime&) = delete;

    std::int32_t start(const CatroVoiceRuntimeConfig& config) noexcept {
        std::scoped_lock lifecycle_lock(lifecycle_mutex_);
        stop_locked();

        const bool room_mode =
            config.room_runtime != nullptr;
        if ((!room_mode &&
             (config.bind_address == nullptr ||
              config.peer_address == nullptr ||
              config.bind_address[0] == '\0' ||
              config.peer_address[0] == '\0' ||
              config.bind_port == 0 ||
              config.peer_port == 0)) ||
            config.stream_id == 0 ||
            config.jitter_packets < 1 ||
            config.jitter_packets > 10 ||
            config.bitrate < 12'000 ||
            config.bitrate > 128'000) {
            state_.store(CATRO_VOICE_FAILED, std::memory_order_release);
            exit_code_.store(catro::tools::voice_peer_invalid_arguments, std::memory_order_release);
            set_error("invalid voice runtime configuration");
            return catro::tools::voice_peer_invalid_arguments;
        }

        catro::tools::VoicePeerOptions options;
        options.mode = catro::tools::VoicePeerMode::duplex;
        if (!room_mode) {
            options.bind = {
                config.bind_address,
                config.bind_port};
            options.peer = {
                config.peer_address,
                config.peer_port};
        }
        options.duration = std::chrono::hours(24);
        options.stream_id = config.stream_id;
        options.jitter_packets = config.jitter_packets;
        options.bitrate = config.bitrate;
        if (config.input_endpoint != nullptr && config.input_endpoint[0] != '\0') {
            options.input = catro::capabilities::AudioEndpointId{
                std::string(config.input_endpoint), catro::capabilities::IdentityScope::persistent};
        }
        if (config.output_endpoint != nullptr && config.output_endpoint[0] != '\0') {
            options.output = catro::capabilities::AudioEndpointId{
                std::string(config.output_endpoint), catro::capabilities::IdentityScope::persistent};
        }

        reset_control();
        exit_code_.store(-1, std::memory_order_relaxed);
        state_.store(CATRO_VOICE_STARTING, std::memory_order_release);
        set_error({});

        try {
            const auto room_handle =
                config.room_runtime;
            const auto stream_id =
                config.stream_id;
            const auto jitter_packets =
                config.jitter_packets;
            const auto bitrate =
                config.bitrate;
            const std::string input_endpoint =
                config.input_endpoint != nullptr
                    ? config.input_endpoint
                    : "";
            const std::string output_endpoint =
                config.output_endpoint != nullptr
                    ? config.output_endpoint
                    : "";

            worker_ = std::thread(
                [this,
                 room_handle,
                 stream_id,
                 jitter_packets,
                 bitrate,
                 input_endpoint,
                 output_endpoint,
                 options = std::move(options)]() mutable {
                    NullStream output;
                    std::ostringstream error;
                    int code = 0;

                    if (room_handle != nullptr) {
                        CatroVoiceRuntimeConfig room_config{};
                        room_config.room_runtime =
                            room_handle;
                        room_config.stream_id =
                            stream_id;
                        room_config.jitter_packets =
                            jitter_packets;
                        room_config.bitrate =
                            bitrate;
                        room_config.input_endpoint =
                            input_endpoint.empty()
                                ? nullptr
                                : input_endpoint.c_str();
                        room_config.output_endpoint =
                            output_endpoint.empty()
                                ? nullptr
                                : output_endpoint.c_str();

                        std::string room_error;
                        code = run_room_voice(
                            room_config,
                            platform_,
                            control_,
                            room_error);
                        if (!room_error.empty()) {
                            error << room_error;
                        }
                    } else {
                        code =
                            catro::tools::run_voice_peer(
                                options,
                                platform_,
                                output,
                                error,
                                &control_);
                    }

                    exit_code_.store(
                        code,
                        std::memory_order_release);
                    if (!error.str().empty()) {
                        set_error(error.str());
                    }

                    const bool requested_stop =
                        control_.stop_requested.load(
                            std::memory_order_acquire);
                    state_.store(
                        requested_stop ||
                                code ==
                                    catro::tools::
                                        voice_peer_ok
                            ? CATRO_VOICE_IDLE
                            : CATRO_VOICE_FAILED,
                        std::memory_order_release);
                });
        } catch (...) {
            state_.store(CATRO_VOICE_FAILED, std::memory_order_release);
            exit_code_.store(catro::tools::voice_peer_network_failed, std::memory_order_release);
            set_error("failed to start voice worker");
            return catro::tools::voice_peer_network_failed;
        }

        return catro::tools::voice_peer_ok;
    }

    void stop() noexcept {
        std::scoped_lock lifecycle_lock(lifecycle_mutex_);
        stop_locked();
    }

    void set_muted(bool value) noexcept {
        control_.muted.store(value, std::memory_order_release);
    }

    void set_deafened(bool value) noexcept {
        control_.deafened.store(value, std::memory_order_release);
    }

    CatroVoiceRuntimeSnapshot snapshot() const noexcept {
        CatroVoiceRuntimeSnapshot result{};
        auto state = state_.load(std::memory_order_acquire);
        if (state == CATRO_VOICE_STARTING &&
            control_.media_started.load(std::memory_order_acquire)) {
            state = CATRO_VOICE_JOINED;
        }
        result.state = state;
        result.exit_code = exit_code_.load(std::memory_order_acquire);
        result.muted = control_.muted.load(std::memory_order_acquire) ? 1U : 0U;
        result.deafened = control_.deafened.load(std::memory_order_acquire) ? 1U : 0U;
        result.sent_packets = control_.sent_packets.load(std::memory_order_relaxed);
        result.received_packets = control_.received_packets.load(std::memory_order_relaxed);
        result.peer_unreachable_events =
            control_.peer_unreachable_events.load(std::memory_order_relaxed);
        result.peer_seen = result.received_packets > 0 ? 1U : 0U;

        std::scoped_lock error_lock(error_mutex_);
        if (!error_.empty()) {
            std::snprintf(result.error, sizeof(result.error), "%.*s",
                          static_cast<int>(sizeof(result.error) - 1), error_.c_str());
        }
        return result;
    }

private:
    void reset_control() noexcept {
        control_.stop_requested.store(false, std::memory_order_relaxed);
        control_.muted.store(false, std::memory_order_relaxed);
        control_.deafened.store(false, std::memory_order_relaxed);
        control_.media_started.store(false, std::memory_order_relaxed);
        control_.sent_packets.store(0, std::memory_order_relaxed);
        control_.received_packets.store(0, std::memory_order_relaxed);
        control_.peer_unreachable_events.store(0, std::memory_order_relaxed);
        control_.last_exit_code.store(-1, std::memory_order_relaxed);
    }

    void stop_locked() noexcept {
        control_.stop_requested.store(true, std::memory_order_release);
        if (worker_.joinable()) {
            worker_.join();
        }
        state_.store(CATRO_VOICE_IDLE, std::memory_order_release);
        control_.media_started.store(false, std::memory_order_relaxed);
    }

    void set_error(std::string value) noexcept {
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

    catro::platform::windows::WasapiAudioPlatform platform_;
    catro::tools::VoicePeerControl control_;
    mutable std::mutex lifecycle_mutex_;
    mutable std::mutex error_mutex_;
    std::thread worker_;
    std::atomic<std::int32_t> state_{CATRO_VOICE_IDLE};
    std::atomic<std::int32_t> exit_code_{-1};
    std::string error_;
};

VoiceRuntime* runtime(CatroVoiceRuntimeHandle handle) noexcept {
    return static_cast<VoiceRuntime*>(handle);
}

} // namespace

extern "C" {

CatroVoiceRuntimeHandle catro_voice_runtime_create() noexcept {
    return new (std::nothrow) VoiceRuntime();
}

void catro_voice_runtime_destroy(CatroVoiceRuntimeHandle handle) noexcept {
    delete runtime(handle);
}

std::int32_t catro_voice_runtime_start(
    CatroVoiceRuntimeHandle handle, const CatroVoiceRuntimeConfig* config) noexcept {
    if (handle == nullptr || config == nullptr) {
        return catro::tools::voice_peer_invalid_arguments;
    }
    return runtime(handle)->start(*config);
}

void catro_voice_runtime_stop(CatroVoiceRuntimeHandle handle) noexcept {
    if (handle != nullptr) {
        runtime(handle)->stop();
    }
}

void catro_voice_runtime_set_muted(CatroVoiceRuntimeHandle handle, std::uint8_t muted) noexcept {
    if (handle != nullptr) {
        runtime(handle)->set_muted(muted != 0);
    }
}

void catro_voice_runtime_set_deafened(CatroVoiceRuntimeHandle handle, std::uint8_t deafened) noexcept {
    if (handle != nullptr) {
        runtime(handle)->set_deafened(deafened != 0);
    }
}

CatroVoiceRuntimeSnapshot catro_voice_runtime_snapshot(CatroVoiceRuntimeHandle handle) noexcept {
    if (handle == nullptr) {
        CatroVoiceRuntimeSnapshot result{};
        result.state = CATRO_VOICE_FAILED;
        result.exit_code = catro::tools::voice_peer_invalid_arguments;
        std::snprintf(result.error, sizeof(result.error), "%s", "voice runtime is unavailable");
        return result;
    }
    return runtime(handle)->snapshot();
}

} // extern "C"
