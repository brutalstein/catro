#pragma once

#include <cstdint>

#if defined(_WIN32)
#if defined(CATRO_VOICE_RUNTIME_EXPORTS)
#define CATRO_VOICE_API __declspec(dllexport)
#else
#define CATRO_VOICE_API __declspec(dllimport)
#endif
#else
#define CATRO_VOICE_API
#endif

extern "C" {

using CatroVoiceRuntimeHandle = void*;

enum CatroVoiceRuntimeState : std::int32_t {
    CATRO_VOICE_IDLE = 0,
    CATRO_VOICE_STARTING = 1,
    CATRO_VOICE_JOINED = 2,
    CATRO_VOICE_FAILED = 3,
};

struct CatroVoiceRuntimeConfig {
    // When non-null, production media uses the shared WebRTC room runtime and the direct UDP
    // endpoint fields below are ignored. Null keeps the engineering direct-peer path.
    void* room_runtime;

    const char* bind_address;
    std::uint16_t bind_port;
    const char* peer_address;
    std::uint16_t peer_port;
    std::uint32_t stream_id;
    std::uint16_t jitter_packets;
    std::int32_t bitrate;
    const char* input_endpoint;
    const char* output_endpoint;
};

struct CatroVoiceRuntimeSnapshot {
    std::int32_t state;
    std::int32_t exit_code;
    std::uint8_t muted;
    std::uint8_t deafened;
    std::uint8_t peer_seen;
    std::uint8_t reserved;
    std::uint64_t sent_packets;
    std::uint64_t received_packets;
    std::uint64_t peer_unreachable_events;
    char error[192];
};

CATRO_VOICE_API CatroVoiceRuntimeHandle catro_voice_runtime_create() noexcept;
CATRO_VOICE_API void catro_voice_runtime_destroy(CatroVoiceRuntimeHandle handle) noexcept;

CATRO_VOICE_API std::int32_t catro_voice_runtime_start(
    CatroVoiceRuntimeHandle handle, const CatroVoiceRuntimeConfig* config) noexcept;
CATRO_VOICE_API void catro_voice_runtime_stop(CatroVoiceRuntimeHandle handle) noexcept;
CATRO_VOICE_API void catro_voice_runtime_set_muted(
    CatroVoiceRuntimeHandle handle, std::uint8_t muted) noexcept;
CATRO_VOICE_API void catro_voice_runtime_set_deafened(
    CatroVoiceRuntimeHandle handle, std::uint8_t deafened) noexcept;
CATRO_VOICE_API CatroVoiceRuntimeSnapshot catro_voice_runtime_snapshot(
    CatroVoiceRuntimeHandle handle) noexcept;

} // extern "C"
