#pragma once

#include <cstddef>
#include <cstdint>

#if defined(_WIN32)
#if defined(CATRO_ROOM_RUNTIME_EXPORTS)
#define CATRO_ROOM_API __declspec(dllexport)
#else
#define CATRO_ROOM_API __declspec(dllimport)
#endif
#else
#define CATRO_ROOM_API
#endif

extern "C" {

using CatroRoomRuntimeHandle = void*;

enum CatroRoomRuntimeState : std::int32_t {
    CATRO_ROOM_IDLE = 0,
    CATRO_ROOM_CONNECTING = 1,
    CATRO_ROOM_JOINED = 2,
    CATRO_ROOM_FAILED = 3,
};

struct CatroRoomRuntimeConfig {
    const char* signaling_url;
    const char* access_token;
    const char* server_id;
    const char* channel_id;
    const char* user_id;

    const char* const* ice_server_urls;
    std::size_t ice_server_count;

    std::uint8_t allow_insecure_signaling;
    std::uint8_t allow_no_turn;
};

struct CatroRoomRuntimeSnapshot {
    std::int32_t state;
    std::uint32_t peer_count;
    std::uint64_t voice_sent_datagrams;
    std::uint64_t voice_received_datagrams;
    std::uint64_t video_sent_datagrams;
    std::uint64_t video_received_datagrams;
    std::uint64_t stream_audio_sent_datagrams;
    std::uint64_t stream_audio_received_datagrams;
    std::uint64_t voice_queue_drops;
    std::uint64_t video_queue_drops;
    std::uint64_t stream_audio_queue_drops;
    char error[256];
};

CATRO_ROOM_API CatroRoomRuntimeHandle catro_room_runtime_create() noexcept;
CATRO_ROOM_API void catro_room_runtime_destroy(CatroRoomRuntimeHandle handle) noexcept;

CATRO_ROOM_API std::int32_t catro_room_runtime_start(
    CatroRoomRuntimeHandle handle,
    const CatroRoomRuntimeConfig* config) noexcept;
CATRO_ROOM_API void catro_room_runtime_stop(
    CatroRoomRuntimeHandle handle) noexcept;

CATRO_ROOM_API std::size_t catro_room_runtime_send_voice(
    CatroRoomRuntimeHandle handle,
    const std::byte* data,
    std::size_t size) noexcept;
CATRO_ROOM_API std::size_t catro_room_runtime_send_video(
    CatroRoomRuntimeHandle handle,
    const std::byte* data,
    std::size_t size) noexcept;
CATRO_ROOM_API std::size_t catro_room_runtime_send_stream_audio(
    CatroRoomRuntimeHandle handle,
    const std::byte* data,
    std::size_t size) noexcept;

// Returns bytes copied, 0 on timeout/no data, and -1 on invalid arguments.
// Receive queues are bounded and latest-edge oriented: overflow drops the oldest complete datagram.
CATRO_ROOM_API std::ptrdiff_t catro_room_runtime_receive_voice(
    CatroRoomRuntimeHandle handle,
    std::byte* destination,
    std::size_t capacity,
    std::uint32_t timeout_ms) noexcept;
CATRO_ROOM_API std::ptrdiff_t catro_room_runtime_receive_video(
    CatroRoomRuntimeHandle handle,
    std::byte* destination,
    std::size_t capacity,
    std::uint32_t timeout_ms) noexcept;
CATRO_ROOM_API std::ptrdiff_t catro_room_runtime_receive_stream_audio(
    CatroRoomRuntimeHandle handle,
    std::byte* destination,
    std::size_t capacity,
    std::uint32_t timeout_ms) noexcept;

CATRO_ROOM_API CatroRoomRuntimeSnapshot catro_room_runtime_snapshot(
    CatroRoomRuntimeHandle handle) noexcept;

} // extern "C"
