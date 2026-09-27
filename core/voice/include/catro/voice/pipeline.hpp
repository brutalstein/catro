#pragma once

#include <catro/voice/audio_bridge.hpp>
#include <catro/voice/codec.hpp>
#include <catro/voice/jitter.hpp>
#include <catro/voice/packet.hpp>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <variant>

namespace catro::voice {

inline constexpr std::size_t kMaxVoiceDatagramBytes = kVoiceHeaderBytes + kVoiceMaxPayloadBytes;
static_assert(kMaxVoiceDatagramBytes < 1400, "voice datagrams must stay below a conservative MTU payload budget");

struct VoicePipelineConfig {
    std::uint32_t local_stream_id = 1;
    std::uint16_t initial_sequence = 0;
    std::uint32_t initial_timestamp = 0;
    std::uint16_t jitter_target_packets = kDefaultJitterTargetPackets;
    std::size_t capture_queue_frames = kDefaultRealtimeQueueFrames;
    std::size_t render_queue_frames = kDefaultRealtimeQueueFrames;
    EncoderConfig encoder;
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

using ReceiveResult = std::variant<JitterPushResult, PacketError>;

struct VoicePipelineStatistics {
    std::uint64_t encoded_frames = 0;
    std::uint64_t encode_errors = 0;
    std::uint64_t outbound_bytes = 0;
    std::uint64_t received_datagrams = 0;
    std::uint64_t malformed_datagrams = 0;
    std::uint64_t decoded_frames = 0;
    std::uint64_t decode_errors = 0;
    std::uint64_t render_queue_full = 0;
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

    VoicePipeline(const VoicePipeline&) = delete;
    VoicePipeline& operator=(const VoicePipeline&) = delete;

    [[nodiscard]] CaptureBridge& capture() noexcept { return capture_; }
    [[nodiscard]] RenderBridge& render() noexcept { return render_; }

    [[nodiscard]] std::variant<EncodeStep, CodecError> encode_next(OutboundDatagram& datagram) noexcept;
    [[nodiscard]] ReceiveResult receive(std::span<const std::byte> datagram) noexcept;
    [[nodiscard]] PlayoutKind next_playout_kind() const noexcept { return jitter_.peek(); }
    [[nodiscard]] std::variant<DecodeStep, CodecError> decode_next() noexcept;

    [[nodiscard]] VoicePipelineStatistics statistics() const noexcept;

private:
    VoicePipeline(const VoicePipelineConfig& config, std::unique_ptr<Encoder> encoder,
                  std::unique_ptr<Decoder> decoder);

    CaptureBridge capture_;
    RenderBridge render_;
    std::unique_ptr<Encoder> encoder_;
    std::unique_ptr<Decoder> decoder_;
    JitterBuffer jitter_;
    PcmFrame capture_frame_{};
    PcmFrame decoded_frame_{};
    std::uint32_t stream_id_ = 0;
    std::uint16_t next_sequence_ = 0;
    std::uint32_t next_timestamp_ = 0;

    std::atomic<std::uint64_t> encoded_frames_{0};
    std::atomic<std::uint64_t> encode_errors_{0};
    std::atomic<std::uint64_t> outbound_bytes_{0};
    std::atomic<std::uint64_t> received_datagrams_{0};
    std::atomic<std::uint64_t> malformed_datagrams_{0};
    std::atomic<std::uint64_t> decoded_frames_{0};
    std::atomic<std::uint64_t> decode_errors_{0};
    std::atomic<std::uint64_t> render_queue_full_{0};
};

} // namespace catro::voice
