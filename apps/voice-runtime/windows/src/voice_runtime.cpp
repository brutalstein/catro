#include <catro/voice_runtime.h>
#include <catro/room_runtime.h>
#include <catro/room_voice_runtime.hpp>

#include "voice_peer.hpp"

#include <catro/platform/windows/audio_platform.hpp>

#include <cstdio>
#include <new>
#include <ostream>
#include <streambuf>

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

// WASAPI audio for the shared room voice loop, plus the engineering direct-UDP peer that only
// Windows keeps.
class VoiceRuntime final {
public:
    VoiceRuntime()
        : host(platform,
               {&catro_room_runtime_snapshot, &catro_room_runtime_send_voice, &catro_room_runtime_receive_voice},
               [this](const catro::tools::VoicePeerOptions& options,
                      catro::tools::VoicePeerControl& control,
                      std::ostream& error) {
                   NullStream output;
                   return catro::tools::run_voice_peer(options, platform, output, error, &control);
               }) {}

    catro::platform::windows::WasapiAudioPlatform platform;
    catro::voice_runtime::VoiceRuntimeHost host;
};

catro::voice_runtime::VoiceRuntimeHost& host(CatroVoiceRuntimeHandle handle) noexcept {
    return static_cast<VoiceRuntime*>(handle)->host;
}

} // namespace

extern "C" {

CatroVoiceRuntimeHandle catro_voice_runtime_create() noexcept {
    return new (std::nothrow) VoiceRuntime();
}

void catro_voice_runtime_destroy(CatroVoiceRuntimeHandle handle) noexcept {
    delete static_cast<VoiceRuntime*>(handle);
}

std::int32_t catro_voice_runtime_start(
    CatroVoiceRuntimeHandle handle, const CatroVoiceRuntimeConfig* config) noexcept {
    if (handle == nullptr || config == nullptr) {
        return catro::tools::voice_peer_invalid_arguments;
    }
    return host(handle).start(*config);
}

void catro_voice_runtime_stop(CatroVoiceRuntimeHandle handle) noexcept {
    if (handle != nullptr) {
        host(handle).stop();
    }
}

void catro_voice_runtime_set_muted(CatroVoiceRuntimeHandle handle, std::uint8_t muted) noexcept {
    if (handle != nullptr) {
        host(handle).set_muted(muted != 0);
    }
}

void catro_voice_runtime_set_deafened(CatroVoiceRuntimeHandle handle, std::uint8_t deafened) noexcept {
    if (handle != nullptr) {
        host(handle).set_deafened(deafened != 0);
    }
}

void catro_voice_runtime_set_user_volume(CatroVoiceRuntimeHandle handle, const char* user_id, float volume) noexcept {
    if (handle != nullptr) {
        host(handle).set_user_volume(user_id, volume);
    }
}

void catro_voice_runtime_set_processing(CatroVoiceRuntimeHandle handle, std::uint8_t echo_cancellation,
                                        std::uint8_t noise_suppression, std::uint8_t automatic_gain) noexcept {
    if (handle != nullptr) {
        host(handle).set_processing(echo_cancellation != 0, noise_suppression != 0, automatic_gain != 0);
    }
}

void catro_voice_runtime_set_input_threshold(CatroVoiceRuntimeHandle handle, float dbfs) noexcept {
    if (handle != nullptr) {
        host(handle).set_input_threshold(dbfs);
    }
}

void catro_voice_runtime_set_transmit(CatroVoiceRuntimeHandle handle, std::uint8_t transmit) noexcept {
    if (handle != nullptr) {
        host(handle).set_transmit(transmit != 0);
    }
}

std::uint8_t catro_voice_runtime_user_speaking(CatroVoiceRuntimeHandle handle, const char* user_id) noexcept {
    return handle != nullptr && host(handle).user_speaking(user_id) ? 1U : 0U;
}

CatroVoiceRuntimeSnapshot catro_voice_runtime_snapshot(CatroVoiceRuntimeHandle handle) noexcept {
    if (handle == nullptr) {
        CatroVoiceRuntimeSnapshot result{};
        result.state = CATRO_VOICE_FAILED;
        result.exit_code = catro::tools::voice_peer_invalid_arguments;
        std::snprintf(result.error, sizeof(result.error), "%s", "voice runtime is unavailable");
        return result;
    }
    return host(handle).snapshot();
}

} // extern "C"
