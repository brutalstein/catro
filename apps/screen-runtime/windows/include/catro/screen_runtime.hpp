#pragma once

#include <catro/platform/windows/screen_capture.hpp>
#include <catro/room_runtime.h>
#include <catro/screen_transport_runtime.hpp>
#include <catro/transport/udp_peer_socket.hpp>

#include <dxgi1_2.h>
#include <wrl/client.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>

namespace catro::screen {

struct ScreenShareConfig {
    platform::windows::CaptureSource source;
    bool borderless = false;
    CatroRoomRuntimeHandle room_runtime = nullptr;
    transport::UdpEndpoint bind;
    transport::UdpEndpoint peer;

    // These are ceilings. Source aspect ratio is preserved, output is even for NV12, and the
    // runtime never upscales a smaller source.
    std::uint32_t max_width = 1920;
    std::uint32_t max_height = 1080;
    std::uint32_t fps = 30;
    std::uint32_t bitrate = 6'000'000;

    // A window share captures its process tree's audio; a display share captures computer audio
    // except Catro itself, so voices are never sent twice. The production room transport carries
    // it independently from microphone voice.
    bool share_audio = true;
    std::int32_t stream_audio_bitrate = 128'000;

    std::uint32_t ssrc = 1;
    std::uint8_t payload_type = 96;
    std::uint16_t mtu_bytes = 1200;
    std::size_t max_access_unit_bytes = 4U * 1024U * 1024U;
};

// One full-duplex video transport per voice-room membership. The connected UDP socket is shared by
// one receive worker and (only while the local user shares) one capture/encode worker. The socket
// lifetime outlives both workers, so there is no second port, packet relay, or encoded-frame copy.
class WindowsScreenShareRuntime final {
public:
    WindowsScreenShareRuntime();
    ~WindowsScreenShareRuntime();

    WindowsScreenShareRuntime(const WindowsScreenShareRuntime&) = delete;
    WindowsScreenShareRuntime& operator=(const WindowsScreenShareRuntime&) = delete;

    // Starts receive/decode/presentation only. Product code calls this when joining voice so an
    // incoming screen stream can appear even when the local user is not sharing.
    [[nodiscard]] std::optional<ScreenShareError> start_listening(
        const ScreenTransportConfig& config);

    // Starts (or reuses) the transport and adds the local capture/encode sender.
    [[nodiscard]] std::optional<ScreenShareError> start(
        const ScreenShareConfig& config);

    // Stops only local capture/encode. Incoming video remains active on the same socket.
    void stop_sharing() noexcept;

    // Local self-preview is presentation-only and must never be required for transport. Product UI
    // disables it while the voice page is hidden so screen sharing does not spend GPU time on an
    // invisible D3D11 VideoProcessor/swap-chain path.
    void set_local_preview_enabled(bool enabled) noexcept;
    // Full-size, full-rate preview while the user watches their own stream full screen.
    void set_local_preview_full(bool full) noexcept;

    // Receiving RTP is room state; decoding/presentation is viewer state. Keeping these separate
    // means a user can stay in voice while choosing whether to spend GPU time watching a stream.
    void set_remote_viewing_enabled(bool enabled) noexcept;

    // Playback volume of the watched stream's audio: 0 silences, 1 is unchanged, 2 doubles.
    void set_stream_volume(float volume) noexcept;

    // Receives stream audio this PC plays or shares, for the voice echo canceller. Set before
    // start_listening; called from media threads.
    void set_echo_sink(std::function<void(std::span<const float>)> sink);

    // Stops both directions and releases the transport.
    void stop() noexcept;

    [[nodiscard]] ScreenShareSnapshot snapshot() const;
    [[nodiscard]] Microsoft::WRL::ComPtr<IDXGISwapChain1>
    preview_swap_chain() const;
    [[nodiscard]] Microsoft::WRL::ComPtr<IDXGISwapChain1>
    remote_swap_chain() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace catro::screen
