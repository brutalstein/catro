#pragma once

#include <catro/room_runtime.h>
#include <catro/voice_runtime.h>

#include <catro/audio/engine.hpp>

#include "voice_peer.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <ostream>
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

private:
    void stop_locked() noexcept;
    void set_error(std::string value) noexcept;

    audio::AudioPlatform& platform_;
    RoomVoiceApi room_;
    DirectPeerRunner direct_;
    tools::VoicePeerControl control_;
    std::mutex lifecycle_mutex_;
    mutable std::mutex error_mutex_;
    std::thread worker_;
    std::atomic<std::int32_t> state_{CATRO_VOICE_IDLE};
    std::atomic<std::int32_t> exit_code_{-1};
    std::string error_;
};

} // namespace catro::voice_runtime
