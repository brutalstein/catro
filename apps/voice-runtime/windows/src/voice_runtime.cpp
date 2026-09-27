#include <catro/voice_runtime.h>

#include "voice_peer.hpp"

#include <catro/platform/windows/audio_platform.hpp>

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

        if (config.bind_address == nullptr || config.peer_address == nullptr ||
            config.bind_address[0] == '\0' || config.peer_address[0] == '\0' ||
            config.bind_port == 0 || config.peer_port == 0 || config.stream_id == 0 ||
            config.jitter_packets < 1 || config.jitter_packets > 10 ||
            config.bitrate < 12'000 || config.bitrate > 128'000) {
            state_.store(CATRO_VOICE_FAILED, std::memory_order_release);
            exit_code_.store(catro::tools::voice_peer_invalid_arguments, std::memory_order_release);
            set_error("invalid voice runtime configuration");
            return catro::tools::voice_peer_invalid_arguments;
        }

        catro::tools::VoicePeerOptions options;
        options.mode = catro::tools::VoicePeerMode::duplex;
        options.bind = {config.bind_address, config.bind_port};
        options.peer = {config.peer_address, config.peer_port};
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
            worker_ = std::thread([this, options = std::move(options)]() mutable {
                NullStream output;
                std::ostringstream error;
                const auto code =
                    catro::tools::run_voice_peer(options, platform_, output, error, &control_);

                exit_code_.store(code, std::memory_order_release);
                if (!error.str().empty()) {
                    set_error(error.str());
                }

                const bool requested_stop =
                    control_.stop_requested.load(std::memory_order_acquire);
                state_.store(requested_stop || code == catro::tools::voice_peer_ok
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
