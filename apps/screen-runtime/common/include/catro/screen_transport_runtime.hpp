#pragma once

#include <catro/audio/realtime.hpp>
#include <catro/room_runtime.h>
#include <catro/transport/udp_peer_socket.hpp>
#include <catro/video/rtp_h264.hpp>
#include <catro/voice/codec.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

// Platform-independent screen-share transport shared by the Windows and macOS runtimes. It owns
// RTP packetization/reassembly, stream identity, keyframe recovery, bounded receive draining,
// stream-audio Opus framing and every transport counter. Platform runtimes supply only native
// capture, codec, presentation and audio-device edges.
namespace catro::screen {

enum class ScreenShareState : std::uint8_t {
    idle,
    listening,
    starting,
    sharing,
    failed,
};

enum class ScreenShareErrorCode : std::uint8_t {
    invalid_config,
    worker_start_failed,
    network_failed,
    memory_failed,
    capture_failed,
    encoder_failed,
    packetization_failed,
    preview_failed,
    decoder_failed,
    remote_present_failed,
};

struct ScreenShareError {
    ScreenShareErrorCode code = ScreenShareErrorCode::invalid_config;
    std::string message;
    std::int64_t native_code = 0;
};

struct ScreenTransportConfig {
    // Production mode uses one shared WebRTC room runtime. Null keeps the direct UDP engineering
    // transport used by local diagnostics.
    CatroRoomRuntimeHandle room_runtime = nullptr;
    transport::UdpEndpoint bind;
    transport::UdpEndpoint peer;
    std::uint8_t payload_type = 96;
    std::uint16_t mtu_bytes = 1200;
    std::size_t max_access_unit_bytes = 4U * 1024U * 1024U;

    friend bool operator==(const ScreenTransportConfig&, const ScreenTransportConfig&) = default;
};

struct ScreenShareSnapshot {
    ScreenShareState state = ScreenShareState::idle;
    std::string source_title;
    std::string error;
    std::uint32_t source_width = 0;
    std::uint32_t source_height = 0;
    std::uint32_t encoded_width = 0;
    std::uint32_t encoded_height = 0;
    std::uint64_t frames_encoded = 0;
    std::uint64_t frames_sent = 0;
    std::uint64_t frames_dropped = 0;
    std::uint64_t packets_sent = 0;
    std::uint64_t wire_bytes = 0;
    std::uint64_t backpressure_events = 0;
    std::uint64_t peer_unreachable_events = 0;
    std::uint64_t preview_frames = 0;
    std::uint64_t preview_drops = 0;
    std::uint64_t encoder_input_failures = 0;
    std::uint64_t encoder_output_failures = 0;
    std::uint64_t encoder_timeouts = 0;
    std::uint64_t capture_contention_drops = 0;
    bool stream_audio_enabled = false;
    bool stream_audio_active = false;
    std::string stream_audio_error;
    std::uint64_t stream_audio_frames_encoded = 0;
    std::uint64_t stream_audio_packets_sent = 0;
    std::uint64_t stream_audio_capture_drops = 0;
    std::uint64_t stream_audio_encode_failures = 0;

    // A remote stream can be present in the voice room without being watched. This mirrors
    // Discord's voice/Go Live split and keeps decode/presentation GPU work opt-in.
    bool remote_available = false;
    bool remote_viewing = false;
    bool remote_active = false;
    std::uint32_t remote_width = 0;
    std::uint32_t remote_height = 0;
    std::uint64_t remote_packets = 0;
    std::uint64_t remote_wire_bytes = 0;
    std::uint64_t remote_frames = 0;
    std::uint64_t remote_decoded = 0;
    std::uint64_t remote_presented = 0;
    std::uint64_t remote_frame_drops = 0;
    std::uint64_t remote_packet_rejects = 0;
    std::uint64_t remote_decode_failures = 0;
    std::uint64_t remote_present_drops = 0;
    std::uint64_t remote_stream_resets = 0;
    bool remote_stream_audio_active = false;
    std::uint64_t remote_stream_audio_packets = 0;
    std::uint64_t remote_stream_audio_frames = 0;
    std::uint64_t remote_stream_audio_decode_failures = 0;
    std::uint64_t remote_stream_audio_render_drops = 0;
};

using Clock = std::chrono::steady_clock;

inline constexpr std::chrono::seconds kRemoteInactiveTimeout{2};
// Viewers repeat keyframe requests at this pace while they wait, and a sharer forces at most one
// IDR per interval, so loss recovery takes about one round trip instead of a 2 s GOP.
inline constexpr std::chrono::milliseconds kKeyframeRequestInterval{250};
inline constexpr std::chrono::milliseconds kReceiveWait{20};
inline constexpr std::size_t kReceiveDatagramBytes = 1500;
inline constexpr std::size_t kReceiveDrainLimit = 512;

inline constexpr std::chrono::milliseconds kStreamAudioFramePeriod{20};
inline constexpr std::size_t kStreamAudioChannels = 2;
inline constexpr std::size_t kStreamAudioFrameSamples =
    static_cast<std::size_t>(voice::kFrameSamples) * kStreamAudioChannels;
inline constexpr std::size_t kStreamAudioQueueFrames = 8;
inline constexpr std::size_t kStreamAudioJitterPackets = 3;
inline constexpr std::size_t kStreamAudioReceiveDrainLimit = 64;

using StreamAudioPcmFrame = std::array<float, kStreamAudioFrameSamples>;

// Room media entry points. Runtimes pass the real C ABI; tests pass a deterministic fake.
struct RoomScreenApi {
    CatroRoomRuntimeSnapshot (*snapshot)(CatroRoomRuntimeHandle) noexcept = nullptr;
    std::size_t (*send_video)(CatroRoomRuntimeHandle, const std::byte*, std::size_t) noexcept = nullptr;
    std::ptrdiff_t (*receive_video)(CatroRoomRuntimeHandle, std::byte*, std::size_t, std::uint32_t) noexcept =
        nullptr;
    std::size_t (*send_stream_audio)(CatroRoomRuntimeHandle, const std::byte*, std::size_t) noexcept = nullptr;
    std::ptrdiff_t (*receive_stream_audio)(CatroRoomRuntimeHandle, std::byte*, std::size_t,
                                           std::uint32_t) noexcept = nullptr;
    // Optional keyframe feedback; null disables it (direct engineering transport, older fakes).
    void (*request_keyframe)(CatroRoomRuntimeHandle) noexcept = nullptr;
    std::uint64_t (*keyframe_requests)(CatroRoomRuntimeHandle) noexcept = nullptr;
};

[[nodiscard]] bool valid_media_bounds(std::uint8_t payload_type, std::uint16_t mtu_bytes,
                                      std::size_t max_access_unit_bytes) noexcept;
[[nodiscard]] bool valid_direct_endpoints(const transport::UdpEndpoint& bind,
                                          const transport::UdpEndpoint& peer) noexcept;
[[nodiscard]] bool valid_transport(const ScreenTransportConfig& config) noexcept;
[[nodiscard]] std::chrono::nanoseconds frame_period(std::uint32_t fps) noexcept;
[[nodiscard]] std::uint32_t monotonic_rtp_timestamp(Clock::time_point started, Clock::time_point now) noexcept;
[[nodiscard]] std::int64_t steady_now_ns() noexcept;
[[nodiscard]] std::string udp_error_text(const transport::UdpError& error);
// The room's own error text when present, otherwise `fallback`.
[[nodiscard]] std::string room_error_text(const RoomScreenApi& api, CatroRoomRuntimeHandle room,
                                          std::string_view fallback);

// Every counter the shared transport owns. Platform runtimes keep capture/encoder/preview counters.
struct ScreenTransportCounters {
    std::atomic<std::uint64_t> frames_sent{0};
    std::atomic<std::uint64_t> frames_dropped{0};
    std::atomic<std::uint64_t> packets_sent{0};
    std::atomic<std::uint64_t> wire_bytes{0};
    std::atomic<std::uint64_t> backpressure_events{0};
    std::atomic<std::uint64_t> peer_unreachable_events{0};
    std::atomic<std::uint64_t> stream_audio_frames_encoded{0};
    std::atomic<std::uint64_t> stream_audio_packets_sent{0};
    std::atomic<std::uint64_t> stream_audio_capture_drops{0};
    std::atomic<std::uint64_t> stream_audio_encode_failures{0};

    std::atomic_bool remote_viewing_enabled{false};
    std::atomic<std::int64_t> remote_last_stream_ns{0};
    std::atomic<std::int64_t> remote_last_frame_ns{0};
    std::atomic<std::uint32_t> remote_width{0};
    std::atomic<std::uint32_t> remote_height{0};
    std::atomic<std::uint64_t> remote_packets{0};
    std::atomic<std::uint64_t> remote_wire_bytes{0};
    std::atomic<std::uint64_t> remote_frames{0};
    std::atomic<std::uint64_t> remote_decoded{0};
    std::atomic<std::uint64_t> remote_presented{0};
    std::atomic<std::uint64_t> remote_frame_drops{0};
    std::atomic<std::uint64_t> remote_packet_rejects{0};
    std::atomic<std::uint64_t> remote_decode_failures{0};
    std::atomic<std::uint64_t> remote_present_drops{0};
    std::atomic<std::uint64_t> remote_stream_resets{0};
    std::atomic_bool remote_stream_audio_active{false};
    std::atomic<std::uint64_t> remote_stream_audio_packets{0};
    std::atomic<std::uint64_t> remote_stream_audio_frames{0};
    std::atomic<std::uint64_t> remote_stream_audio_decode_failures{0};
    std::atomic<std::uint64_t> remote_stream_audio_render_drops{0};
    std::atomic<std::int64_t> remote_stream_audio_last_ns{0};
    // Viewer-side stream volume (0 silences, 1 unchanged, 2 doubles); kept across streams.
    std::atomic<float> remote_stream_volume{1.0F};

    void reset_local() noexcept;
    void reset_remote() noexcept;
    // Fills every transport-owned snapshot field, including the derived remote availability.
    void fill(ScreenShareSnapshot& snapshot) const noexcept;
};

enum class VideoSendStatus : std::uint8_t {
    sent,
    dropped,          // backpressure or a transiently unreachable peer; the stream stays live
    room_failed,      // the room runtime failed or no transport exists
    network_failed,   // fatal direct-UDP error, see VideoSendResult::network_error
    not_packetizable, // encoder output is not RFC 6184 packetizable
};

struct VideoSendResult {
    VideoSendStatus status = VideoSendStatus::sent;
    std::optional<transport::UdpError> network_error;
};

// Packetizes one Annex-B access unit and sends every RTP packet through the room (or the direct
// engineering socket). Zero room viewers is success: the stream fans out once a viewer arrives.
class VideoSender final {
public:
    VideoSender(const RoomScreenApi& api, CatroRoomRuntimeHandle room, transport::UdpPeerSocket* socket,
                video::H264RtpConfig rtp, ScreenTransportCounters& counters) noexcept;

    [[nodiscard]] VideoSendResult send(std::span<const std::byte> annex_b, std::uint32_t timestamp_90khz) noexcept;
    // True when a viewer asked for a keyframe since the last IDR this returned true for. Requests
    // from several viewers within kKeyframeRequestInterval share one IDR.
    [[nodiscard]] bool keyframe_requested() noexcept;

private:
    static bool send_packet(void* context, const video::RtpPacketSlice& packet) noexcept;

    RoomScreenApi api_;
    CatroRoomRuntimeHandle room_ = nullptr;
    transport::UdpPeerSocket* socket_ = nullptr;
    video::H264RtpConfig rtp_;
    ScreenTransportCounters& counters_;
    std::uint16_t next_sequence_ = 1;
    std::uint64_t keyframe_requests_seen_ = 0;
    std::int64_t last_forced_keyframe_ns_ = 0;
    bool soft_drop_ = false;
    bool room_failed_ = false;
    std::optional<transport::UdpError> fatal_error_;
};

// Native decode/presentation edge of the receive loop. Implementations update the remote
// decoded/presented/size counters themselves; the loop owns stream identity and keyframe gating.
class RemoteVideoViewer {
public:
    virtual ~RemoteVideoViewer() = default;
    // Releases decoder and presenter resources. Called when viewing stops, the stream resets, or
    // the loop exits.
    virtual void release() noexcept = 0;
    [[nodiscard]] virtual std::optional<ScreenShareError> start_decoder() = 0;
    [[nodiscard]] virtual std::optional<ScreenShareError> decode_and_present(std::span<const std::byte> annex_b,
                                                                             std::int64_t pts_100ns) = 0;
};

struct VideoReceiveContext {
    RoomScreenApi api;
    ScreenTransportConfig config;
    transport::UdpPeerSocket* socket = nullptr;
    ScreenTransportCounters* counters = nullptr;
    const std::atomic_bool* stop_requested = nullptr;
    // Optional lifecycle breadcrumbs; never called per packet.
    void (*trace)(std::string_view event) noexcept = nullptr;
};

// Receives until stop is requested or a fatal error occurs. Each wakeup drains at most
// kReceiveDrainLimit datagrams so a burst cannot starve stop or viewing changes. A new SSRC only
// replaces an active stream after kRemoteInactiveTimeout of silence; deltas are discarded until a
// keyframe after every viewer start or stream reset.
[[nodiscard]] std::optional<ScreenShareError> run_video_receive_loop(const VideoReceiveContext& context,
                                                                     RemoteVideoViewer& viewer);

// Scales decoded stream audio by the viewer's stream volume, clipping at full scale.
void apply_stream_volume(std::span<float> samples, float volume) noexcept;

// Capture callback -> sender thread hand-off. Overflow requests a resync instead of growing latency.
class StreamAudioCaptureBridge final {
public:
    StreamAudioCaptureBridge();
    void on_captured(std::span<const float> samples) noexcept;
    [[nodiscard]] bool try_pop(StreamAudioPcmFrame& frame) noexcept;
    [[nodiscard]] std::uint64_t dropped_callbacks() const noexcept;

private:
    audio::SpscRing<float> ring_;
    std::atomic_bool resync_requested_{false};
    std::atomic<std::uint64_t> dropped_callbacks_{0};
};

// Receive thread -> device render callback hand-off.
class StreamAudioRenderBridge final {
public:
    StreamAudioRenderBridge();
    [[nodiscard]] bool try_push(const StreamAudioPcmFrame& frame) noexcept;
    void on_render(std::span<float> samples) noexcept;
    void request_resync() noexcept;
    [[nodiscard]] std::uint64_t underruns() const noexcept;

private:
    audio::SpscRing<float> ring_;
    std::atomic_bool primed_{false};
    std::atomic_bool request_resync_{false};
    std::atomic<std::uint64_t> underruns_{0};
};

// Opus-encodes 20 ms stereo frames and sends them as voice-format packets on the stream-audio lane.
class StreamAudioSender final {
public:
    StreamAudioSender(const RoomScreenApi& api, CatroRoomRuntimeHandle room, ScreenTransportCounters& counters);
    ~StreamAudioSender();
    StreamAudioSender(const StreamAudioSender&) = delete;
    StreamAudioSender& operator=(const StreamAudioSender&) = delete;

    [[nodiscard]] std::optional<voice::CodecError> start(std::int32_t bitrate, std::uint32_t video_ssrc);
    void send(const StreamAudioPcmFrame& pcm) noexcept;

private:
    RoomScreenApi api_;
    CatroRoomRuntimeHandle room_ = nullptr;
    ScreenTransportCounters& counters_;
    std::unique_ptr<voice::Encoder> encoder_;
    std::uint32_t stream_id_ = 1;
    std::uint16_t sequence_ = 1;
    std::uint32_t timestamp_ = 0;
};

// Native stream-audio output device. start() begins pulling from the bridge on the device thread.
class StreamAudioOutput {
public:
    virtual ~StreamAudioOutput() = default;
    [[nodiscard]] virtual bool start(StreamAudioRenderBridge& bridge) = 0;
    virtual void stop() noexcept = 0;
};

// Receives, de-jitters and decodes remote stream audio on a 20 ms playout clock while the stream is
// watched. Not watching keeps the device stopped and only counts packets.
void run_stream_audio_receive_loop(const RoomScreenApi& api, CatroRoomRuntimeHandle room,
                                   ScreenTransportCounters& counters, const std::atomic_bool& stop_requested,
                                   StreamAudioOutput& output);

} // namespace catro::screen
