#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace catro::video {

inline constexpr std::size_t kRtpHeaderBytes = 12;
inline constexpr std::size_t kH264FuAHeaderBytes = 2;

struct H264RtpConfig {
    std::uint32_t ssrc = 1;
    std::uint8_t payload_type = 96;
    std::uint16_t mtu_bytes = 1200;

    friend bool operator==(const H264RtpConfig&, const H264RtpConfig&) = default;
};

enum class H264PacketizeError : std::uint8_t {
    none,
    invalid_config,
    malformed_annex_b,
    sink_rejected,
};

struct RtpPacketSlice {
    // RTP header plus an optional two-byte FU-A header. The payload span always aliases the encoded
    // access unit; packetization therefore performs no heap allocation and no H.264 payload copy.
    std::array<std::byte, kRtpHeaderBytes + kH264FuAHeaderBytes> prefix{};
    std::uint8_t prefix_size = 0;
    std::span<const std::byte> payload{};
    std::uint16_t sequence = 0;
    bool marker = false;
};

using RtpPacketSink = bool (*)(void* context, const RtpPacketSlice& packet) noexcept;

struct H264PacketizeResult {
    H264PacketizeError error = H264PacketizeError::none;
    std::uint16_t next_sequence = 0;
    std::uint32_t packet_count = 0;
    bool keyframe = false;

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == H264PacketizeError::none;
    }
};

// Converts Media Foundation / VideoToolbox style 100 ns presentation timestamps into RTP's 90 kHz
// video clock without a floating-point operation and without overflowing during normal runtimes.
[[nodiscard]] std::uint32_t rtp_timestamp_90khz(std::uint64_t pts_100ns) noexcept;

// RFC 6184 packetization subset used by the two-client validation path:
// - Annex-B access units containing ordinary NAL unit types 1..23.
// - one NAL per RTP packet when it fits;
// - FU-A for larger NAL units;
// - no STAP aggregation.
//
// The sink is invoked synchronously. It must consume/send the prefix and payload before returning.
[[nodiscard]] H264PacketizeResult packetize_h264_annex_b(
    std::span<const std::byte> access_unit,
    std::uint32_t timestamp_90khz,
    std::uint16_t first_sequence,
    const H264RtpConfig& config,
    void* sink_context,
    RtpPacketSink sink) noexcept;

enum class H264ReassemblyStatus : std::uint8_t {
    packet_accepted,
    frame_ready,
    packet_rejected,
    frame_dropped,
};

enum class H264ReassemblyError : std::uint8_t {
    none,
    invalid_config,
    malformed_rtp,
    payload_type_mismatch,
    ssrc_mismatch,
    unsupported_nal,
    malformed_fragment,
    sequence_gap,
    frame_too_large,
};

struct ReassembledH264Frame {
    // Aliases the caller-owned storage passed to H264RtpReassembler. Consume before the next push.
    std::span<const std::byte> annex_b{};
    std::uint32_t timestamp_90khz = 0;
    bool keyframe = false;
};

struct H264ReassemblyResult {
    H264ReassemblyStatus status = H264ReassemblyStatus::packet_rejected;
    H264ReassemblyError error = H264ReassemblyError::none;
    ReassembledH264Frame frame{};
};

// Single-stream, single-frame bounded reassembler. No packet or frame allocation occurs internally;
// the caller supplies the maximum frame storage once. A damaged frame is discarded at its RTP
// marker and never exposed to a decoder.
class H264RtpReassembler final {
public:
    H264RtpReassembler(std::span<std::byte> frame_storage, H264RtpConfig config) noexcept;

    [[nodiscard]] H264ReassemblyResult push(std::span<const std::byte> datagram) noexcept;
    void reset() noexcept;

private:
    [[nodiscard]] bool append(std::span<const std::byte> bytes) noexcept;
    void begin_frame(std::uint32_t timestamp) noexcept;
    void clear_frame_state() noexcept;
    void mark_damage(H264ReassemblyError error) noexcept;
    [[nodiscard]] H264ReassemblyResult finish_or_drop(bool marker) noexcept;

    std::span<std::byte> storage_{};
    H264RtpConfig config_{};
    std::size_t size_ = 0;
    std::uint32_t timestamp_ = 0;
    std::uint32_t locked_ssrc_ = 0;
    std::uint16_t expected_sequence_ = 0;
    std::uint8_t fu_nal_type_ = 0;
    H264ReassemblyError damage_error_ = H264ReassemblyError::none;
    bool active_ = false;
    bool have_ssrc_ = false;
    bool have_sequence_ = false;
    bool damaged_ = false;
    bool fu_active_ = false;
    bool keyframe_ = false;
};

} // namespace catro::video
