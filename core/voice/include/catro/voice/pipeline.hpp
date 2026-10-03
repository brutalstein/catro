#pragma once

#include <catro/voice/audio_bridge.hpp>
#include <catro/voice/codec.hpp>
#include <catro/voice/jitter.hpp>
#include <catro/voice/packet.hpp>
#include <catro/voice/stream_controls.hpp>
#include <catro/voice/voice_processor.hpp>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <variant>

namespace catro::voice {

inline constexpr std::size_t kMaxVoiceDatagramBytes = kVoiceHeaderBytes + kVoiceMaxPayloadBytes;
inline constexpr std::size_t kMaxRemoteVoiceStreams = 64;
static_assert(kMaxVoiceDatagramBytes < 1400, "voice datagrams must stay below a conservative MTU payload budget");

struct VoicePipelineConfig {
    std::uint32_t local_stream_id = 1;
    std::uint16_t initial_sequence = 0;
    std::uint32_t initial_timestamp = 0;
    std::uint16_t jitter_target_packets = kDefaultJitterTargetPackets;
    std::size_t capture_queue_frames = kDefaultRealtimeQueueFrames;
    std::size_t render_queue_frames = kDefaultRealtimeQueueFrames;
    EncoderConfig encoder;
    // Echo cancellation, noise suppression, and gain control. Off unless a runtime asks for it.
    std::optional<VoiceProcessingConfig> processing;
    // Per-user volume in, speaking state out. Must outlive the pipeline; null disables both.
    StreamControls* controls = nullptr;
};

struct OutboundDatagram {
    std::array<std::byte, kMaxVoiceDatagramBytes> bytes{};
    std::size_t size = 0;

    [[nodiscard]] std::span<const std::byte> view() const noexcept {
        return std::span<const std::byte>(bytes).first(size);
    }
};

enum class EncodeStep {
    no_frame,
    packet_ready,
};

enum class DecodeStep {
    waiting,
    queued_packet,
    queued_fec,
    queued_plc,
    render_queue_full,
};

using ReceiveResult = std::variant<JitterPushResult, PacketError, CodecError>;

struct VoicePipelineStatistics {
    std::uint64_t encoded_frames = 0;
    std::uint64_t encode_errors = 0;
    std::uint64_t outbound_bytes = 0;
    std::uint64_t muted_frames = 0;
    std::uint64_t gated_frames = 0;
    std::uint64_t received_datagrams = 0;
    std::uint64_t malformed_datagrams = 0;
    std::uint64_t decoded_frames = 0;
    std::uint64_t decode_errors = 0;
    std::uint64_t render_queue_full = 0;
    std::uint64_t remote_stream_capacity_drops = 0;
    std::uint64_t mixed_frames = 0;
    std::uint64_t limiter_frames = 0;
    std::size_t remote_streams_active = 0;
    CaptureBridgeStatistics capture;
    RenderBridgeStatistics render;
    JitterStatistics jitter;
};

// Transport-agnostic worker-side voice pipeline. Audio callbacks only touch CaptureBridge and
// RenderBridge. Codec, packet, jitter, and transport-facing methods are called by worker threads.
class VoicePipeline {
public:
    using CreateResult = std::variant<std::unique_ptr<VoicePipeline>, CodecError>;

    [[nodiscard]] static CreateResult create(const VoicePipelineConfig& config) noexcept;

    ~VoicePipeline();

    VoicePipeline(const VoicePipeline&) = delete;
    VoicePipeline& operator=(const VoicePipeline&) = delete;

    [[nodiscard]] CaptureBridge& capture() noexcept { return capture_; }
    [[nodiscard]] RenderBridge& render() noexcept { return render_; }

    void set_muted(bool value) noexcept { muted_.store(value, std::memory_order_release); }
    [[nodiscard]] bool muted() const noexcept { return muted_.load(std::memory_order_acquire); }
    void set_deafened(bool value) noexcept { render_.set_deafened(value); }
    [[nodiscard]] bool deafened() const noexcept { return render_.deafened(); }
    // Applies new settings when processing was enabled at creation; otherwise does nothing.
    void set_processing(const VoiceProcessingConfig& config) noexcept;
    [[nodiscard]] bool processing_available() const noexcept { return processor_ != nullptr; }
    // Voice activity gate, worker thread only: microphone frames quieter than the threshold (dBFS)
    // are sent as silence. nullopt picks the threshold automatically. Off until first called.
    void set_input_threshold(std::optional<float> dbfs) noexcept;

    [[nodiscard]] std::variant<EncodeStep, CodecError> encode_next(OutboundDatagram& datagram) noexcept;
    [[nodiscard]] ReceiveResult receive(std::span<const std::byte> datagram) noexcept;
    [[nodiscard]] PlayoutKind next_playout_kind() const noexcept;
    [[nodiscard]] std::optional<CodecError> resynchronize_receiver() noexcept;
    [[nodiscard]] std::variant<DecodeStep, CodecError> decode_next() noexcept;

    [[nodiscard]] VoicePipelineStatistics statistics() const noexcept;

private:
    struct RemoteStream;

    VoicePipeline(const VoicePipelineConfig& config, std::unique_ptr<Encoder> encoder,
                  std::unique_ptr<VoiceProcessor> processor);

    [[nodiscard]] RemoteStream* find_remote(std::uint32_t stream_id) noexcept;
    [[nodiscard]] JitterStatistics aggregate_jitter_statistics() const noexcept;

    CaptureBridge capture_;
    RenderBridge render_;
    std::unique_ptr<Encoder> encoder_;
    std::unique_ptr<VoiceProcessor> processor_;
    StreamControls* controls_ = nullptr;
    std::array<std::unique_ptr<RemoteStream>, kMaxRemoteVoiceStreams> remotes_{};
    std::uint16_t jitter_target_packets_ = kDefaultJitterTargetPackets;
    PcmFrame capture_frame_{};
    PcmFrame mix_frame_{};
    PcmFrame reference_frame_{};
    std::uint32_t stream_id_ = 0;
    std::uint16_t next_sequence_ = 0;
    std::uint32_t next_timestamp_ = 0;

    std::atomic<std::uint64_t> encoded_frames_{0};
    std::atomic<std::uint64_t> encode_errors_{0};
    std::atomic<std::uint64_t> outbound_bytes_{0};
    std::atomic<std::uint64_t> muted_frames_{0};
    std::atomic<std::uint64_t> gated_frames_{0};
    std::atomic_bool muted_{false};
    std::atomic<std::uint64_t> received_datagrams_{0};
    std::atomic<std::uint64_t> malformed_datagrams_{0};
    std::atomic<std::uint64_t> decoded_frames_{0};
    std::atomic<std::uint64_t> decode_errors_{0};
    std::atomic<std::uint64_t> render_queue_full_{0};
    std::atomic<std::uint64_t> remote_stream_capacity_drops_{0};
    std::atomic<std::uint64_t> mixed_frames_{0};
    std::atomic<std::uint64_t> limiter_frames_{0};
    std::uint64_t playout_tick_ = 0;
    float limiter_gain_ = 1.0F;
    std::uint16_t local_speaking_hangover_ = 0;
    // Mean square per sample; 0 leaves the gate open.
    float gate_mean_square_ = 0.0F;
};

} // namespace catro::voice
