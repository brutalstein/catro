#pragma once

#include <catro/platform/macos/screen_capture.hpp>
#include <catro/screen_transport_runtime.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>

namespace catro::screen {

struct MacScreenShareConfig {
    platform::macos::CaptureSource source;
    // Production rooms only: macOS has no direct-UDP engineering transport.
    CatroRoomRuntimeHandle room_runtime = nullptr;
    // Ceilings. Aspect ratio is preserved, output is even for NV12, and smaller sources are not
    // upscaled.
    std::uint32_t max_width = 1920;
    std::uint32_t max_height = 1080;
    std::uint32_t fps = 30;
    std::uint32_t bitrate = 6'000'000;
    bool share_audio = true;
    std::int32_t stream_audio_bitrate = 128'000;
    std::uint32_t ssrc = 1;
    std::uint8_t payload_type = 96;
    std::uint16_t mtu_bytes = 1200;
    std::size_t max_access_unit_bytes = 4U * 1024U * 1024U;
};

// One full-duplex screen runtime per voice-room membership, mirroring WindowsScreenShareRuntime:
// listening receives (and, only while watched, decodes/presents) the room's stream; start() adds a
// ScreenCaptureKit -> VideoToolbox sender on the same room. One encode per source, no CPU pixel copy.
class MacScreenShareRuntime final {
public:
    MacScreenShareRuntime();
    // Tests inject a deterministic room.
    explicit MacScreenShareRuntime(const RoomScreenApi& room_api);
    ~MacScreenShareRuntime();

    MacScreenShareRuntime(const MacScreenShareRuntime&) = delete;
    MacScreenShareRuntime& operator=(const MacScreenShareRuntime&) = delete;

    [[nodiscard]] std::optional<ScreenShareError> start_listening(const ScreenTransportConfig& config);
    [[nodiscard]] std::optional<ScreenShareError> start(const MacScreenShareConfig& config);
    // Stops only local capture/encode; incoming video keeps flowing.
    void stop_sharing() noexcept;
    void set_local_preview_enabled(bool enabled) noexcept;
    void set_remote_viewing_enabled(bool enabled) noexcept;
    // 0 silences the watched stream's audio, 1 is unchanged, 2 doubles.
    void set_stream_volume(float volume) noexcept;
    // Before the first start: receives stream audio this Mac plays or shares, for the voice echo
    // canceller (48 kHz interleaved stereo).
    void set_echo_sink(std::function<void(std::span<const float>)> sink);
    void stop() noexcept;

    [[nodiscard]] ScreenShareSnapshot snapshot() const;
    // Main thread only. Hosts the local preview / remote stream inside a caller-owned CALayer*;
    // nullptr detaches it. Frames are dropped, never queued, while no surface is attached.
    [[nodiscard]] std::optional<ScreenShareError> attach_preview_surface(void* host_layer);
    [[nodiscard]] std::optional<ScreenShareError> attach_remote_surface(void* host_layer);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

[[nodiscard]] bool valid_share(const MacScreenShareConfig& config) noexcept;

} // namespace catro::screen
