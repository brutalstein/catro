#pragma once

#include <catro/room_runtime.h>
#include <catro/voice_runtime.h>

#include <catro/audio/engine.hpp>
#include <catro/voice/stream_controls.hpp>

#include "voice_peer.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <mutex>
#include <ostream>
#include <span>
#include <string>
#include <thread>

namespace catro::voice_runtime {

// The room operations the voice loop uses. Production passes the catro_room_runtime_* C ABI; tests
// pass an in-memory room. The handle is CatroVoiceRuntimeConfig::room_runtime, passed through as-is.
struct RoomVoiceApi {
    CatroRoomRuntimeSnapshot (*snapshot)(CatroRoomRuntimeHandle) noexcept;
    std::size_t (*send_voice)(CatroRoomRuntimeHandle, const std::byte*, std::size_t) noexcept;
    std::ptrdiff_t (*receive_voice)(CatroRoomRuntimeHandle, std::byte*, std::size_t, std::uint32_t) noexcept;
};

// Settings the UI changes during a call. The media loop applies them when version moves.
struct LiveVoiceSettings {
    std::atomic<std::uint32_t> version{0};
    std::atomic_bool echo_cancellation{true};
    std::atomic_bool noise_suppression{true};
    std::atomic_bool automatic_gain{true};
    // NaN selects the automatic threshold.
    std::atomic<float> input_threshold_db{std::numeric_limits<float>::quiet_NaN()};
    // Push-to-talk releases this; the microphone is silent while it is false.
    std::atomic_bool transmit{true};
    // Devices picked during a call, empty for the system default. The loop reopens audio on them
    // when device_version moves; the strings are guarded by device_mutex.
    mutable std::mutex device_mutex;
    std::string input_device;
    std::string output_device;
    std::atomic<std::uint32_t> device_version{0};
};

// Engineering-only direct UDP media, supplied by adapters that keep it. Returns a VoicePeerExit.
using DirectPeerRunner =
    std::function<int(const tools::VoicePeerOptions&, tools::VoicePeerControl&, std::ostream& error)>;

// Owns one media worker thread and the CatroVoiceRuntime* state machine shared by every platform
// adapter. The UI thread only flips atomics and reads snapshots; it never waits on media work except
// to join the worker in stop().
class VoiceRuntimeHost final {
public:
    VoiceRuntimeHost(audio::AudioPlatform& platform, RoomVoiceApi room, DirectPeerRunner direct = {});
    ~VoiceRuntimeHost();

    VoiceRuntimeHost(const VoiceRuntimeHost&) = delete;
    VoiceRuntimeHost& operator=(const VoiceRuntimeHost&) = delete;

    std::int32_t start(const CatroVoiceRuntimeConfig& config) noexcept;
    void stop() noexcept;
    void set_muted(bool value) noexcept;
    void set_deafened(bool value) noexcept;
    [[nodiscard]] CatroVoiceRuntimeSnapshot snapshot() const noexcept;
    void set_user_volume(const char* user_id, float volume) noexcept;
    [[nodiscard]] bool user_speaking(const char* user_id) const noexcept;
    void set_processing(bool echo_cancellation, bool noise_suppression, bool automatic_gain) noexcept;
    void set_input_threshold(float dbfs) noexcept;
    void set_transmit(bool transmit) noexcept;
    void set_devices(const char* input_endpoint, const char* output_endpoint) noexcept;
    void add_echo_reference(std::span<const float> stereo) noexcept { streams_.add_echo_reference(stereo); }

private:
    void stop_locked() noexcept;
    void set_error(std::string value) noexcept;

    audio::AudioPlatform& platform_;
    RoomVoiceApi room_;
    DirectPeerRunner direct_;
    tools::VoicePeerControl control_;
    voice::StreamControls streams_;
    LiveVoiceSettings settings_;
    std::mutex lifecycle_mutex_;
    mutable std::mutex error_mutex_;
    std::thread worker_;
    std::atomic<std::int32_t> state_{CATRO_VOICE_IDLE};
    std::atomic<std::int32_t> exit_code_{-1};
    std::string error_;
};

} // namespace catro::voice_runtime
