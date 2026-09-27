#include <catro/video/rtp_h264.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

namespace catro::video {
namespace {

constexpr std::uint8_t kRtpVersion2 = 0x80;
constexpr std::uint8_t kRtpMarker = 0x80;
constexpr std::uint8_t kNalTypeMask = 0x1f;
constexpr std::uint8_t kNalNriMask = 0xe0;
constexpr std::uint8_t kNalIdr = 5;
constexpr std::uint8_t kNalFuA = 28;
constexpr std::uint8_t kFuStart = 0x80;
constexpr std::uint8_t kFuEnd = 0x40;
constexpr std::uint8_t kFuReserved = 0x20;
constexpr std::array<std::byte, 4> kAnnexBStartCode{
    std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x01}};

[[nodiscard]] std::uint8_t value(std::byte byte) noexcept {
    return std::to_integer<std::uint8_t>(byte);
}

void write_be16(std::byte* output, std::uint16_t number) noexcept {
    output[0] = static_cast<std::byte>((number >> 8U) & 0xffU);
    output[1] = static_cast<std::byte>(number & 0xffU);
}

void write_be32(std::byte* output, std::uint32_t number) noexcept {
    output[0] = static_cast<std::byte>((number >> 24U) & 0xffU);
    output[1] = static_cast<std::byte>((number >> 16U) & 0xffU);
    output[2] = static_cast<std::byte>((number >> 8U) & 0xffU);
    output[3] = static_cast<std::byte>(number & 0xffU);
}

[[nodiscard]] std::uint16_t read_be16(const std::byte* input) noexcept {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(value(input[0])) << 8U) |
        static_cast<std::uint16_t>(value(input[1])));
}

[[nodiscard]] std::uint32_t read_be32(const std::byte* input) noexcept {
    return (static_cast<std::uint32_t>(value(input[0])) << 24U) |
           (static_cast<std::uint32_t>(value(input[1])) << 16U) |
           (static_cast<std::uint32_t>(value(input[2])) << 8U) |
           static_cast<std::uint32_t>(value(input[3]));
}

[[nodiscard]] bool valid_config(const H264RtpConfig& config) noexcept {
    return config.ssrc != 0 && config.payload_type <= 127 &&
           config.mtu_bytes >= 256 && config.mtu_bytes <= 1500;
}

struct StartCode {
    std::size_t offset = 0;
    std::size_t size = 0;
    bool found = false;
};

[[nodiscard]] StartCode find_start_code(
    std::span<const std::byte> bytes, std::size_t from) noexcept {
    if (from >= bytes.size()) {
        return {bytes.size(), 0, false};
    }
    for (std::size_t index = from; index + 3 <= bytes.size(); ++index) {
        if (index + 4 <= bytes.size() &&
            value(bytes[index]) == 0 && value(bytes[index + 1]) == 0 &&
            value(bytes[index + 2]) == 0 && value(bytes[index + 3]) == 1) {
            return {index, 4, true};
        }
        if (value(bytes[index]) == 0 && value(bytes[index + 1]) == 0 &&
            value(bytes[index + 2]) == 1) {
            return {index, 3, true};
        }
    }
    return {bytes.size(), 0, false};
}

void fill_rtp_header(
    RtpPacketSlice& packet,
    const H264RtpConfig& config,
    std::uint16_t sequence,
    std::uint32_t timestamp,
    bool marker) noexcept {
    packet.prefix.fill(std::byte{0});
    packet.prefix[0] = static_cast<std::byte>(kRtpVersion2);
    packet.prefix[1] = static_cast<std::byte>(
        (marker ? kRtpMarker : 0U) | static_cast<std::uint8_t>(config.payload_type));
    write_be16(packet.prefix.data() + 2, sequence);
    write_be32(packet.prefix.data() + 4, timestamp);
    write_be32(packet.prefix.data() + 8, config.ssrc);
    packet.sequence = sequence;
    packet.marker = marker;
}

[[nodiscard]] bool emit_packet(
    H264PacketizeResult& result,
    const H264RtpConfig& config,
    std::uint32_t timestamp,
    bool marker,
    std::uint8_t prefix_size,
    std::span<const std::byte> payload,
    void* context,
    RtpPacketSink sink,
    std::uint8_t fu_indicator = 0,
    std::uint8_t fu_header = 0) noexcept {
    RtpPacketSlice packet;
    fill_rtp_header(packet, config, result.next_sequence, timestamp, marker);
    packet.prefix_size = prefix_size;
    if (prefix_size == kRtpHeaderBytes + kH264FuAHeaderBytes) {
        packet.prefix[kRtpHeaderBytes] = static_cast<std::byte>(fu_indicator);
        packet.prefix[kRtpHeaderBytes + 1] = static_cast<std::byte>(fu_header);
    }
    packet.payload = payload;

    if (!sink(context, packet)) {
        result.error = H264PacketizeError::sink_rejected;
        return false;
    }

    ++result.packet_count;
    result.next_sequence = static_cast<std::uint16_t>(result.next_sequence + 1U);
    return true;
}

} // namespace

std::uint32_t rtp_timestamp_90khz(std::uint64_t pts_100ns) noexcept {
    // 90'000 / 10'000'000 = 9 / 1000. Split the quotient/remainder so the multiplication
    // cannot overflow for any realistic process lifetime. Round the sub-millisecond remainder.
    const auto whole = (pts_100ns / 1000U) * 9U;
    const auto remainder = ((pts_100ns % 1000U) * 9U + 500U) / 1000U;
    return static_cast<std::uint32_t>(whole + remainder);
}

H264PacketizeResult packetize_h264_annex_b(
    std::span<const std::byte> access_unit,
    std::uint32_t timestamp_90khz,
    std::uint16_t first_sequence,
    const H264RtpConfig& config,
    void* sink_context,
    RtpPacketSink sink) noexcept {
    H264PacketizeResult result;
    result.next_sequence = first_sequence;

    if (!valid_config(config) || sink == nullptr || access_unit.empty()) {
        result.error = H264PacketizeError::invalid_config;
        return result;
    }

    const auto first = find_start_code(access_unit, 0);
    if (!first.found) {
        result.error = H264PacketizeError::malformed_annex_b;
        return result;
    }
    for (std::size_t index = 0; index < first.offset; ++index) {
        if (access_unit[index] != std::byte{0}) {
            result.error = H264PacketizeError::malformed_annex_b;
            return result;
        }
    }

    auto current = first;
    bool emitted_any = false;
    while (current.found) {
        const auto nal_begin = current.offset + current.size;
        const auto next = find_start_code(access_unit, nal_begin);
        auto nal_end = next.found ? next.offset : access_unit.size();

        // Annex-B may contain trailing_zero_8bits between NAL units.
        while (nal_end > nal_begin && access_unit[nal_end - 1] == std::byte{0}) {
            --nal_end;
        }
        if (nal_end <= nal_begin) {
            result.error = H264PacketizeError::malformed_annex_b;
            return result;
        }

        const auto nal = access_unit.subspan(nal_begin, nal_end - nal_begin);
        const auto nal_header = value(nal.front());
        const auto nal_type = static_cast<std::uint8_t>(nal_header & kNalTypeMask);
        if (nal_type == 0 || nal_type > 23) {
            result.error = H264PacketizeError::malformed_annex_b;
            return result;
        }

        const bool last_nal = !next.found;
        result.keyframe = result.keyframe || nal_type == kNalIdr;

        const auto single_payload_limit =
            static_cast<std::size_t>(config.mtu_bytes) - kRtpHeaderBytes;
        if (nal.size() <= single_payload_limit) {
            if (!emit_packet(
                    result, config, timestamp_90khz, last_nal,
                    static_cast<std::uint8_t>(kRtpHeaderBytes), nal,
                    sink_context, sink)) {
                return result;
            }
        } else {
            if (nal.size() <= 1) {
                result.error = H264PacketizeError::malformed_annex_b;
                return result;
            }
            const auto fragment_payload_limit =
                static_cast<std::size_t>(config.mtu_bytes) -
                kRtpHeaderBytes - kH264FuAHeaderBytes;
            const auto body = nal.subspan(1);
            std::size_t offset = 0;
            while (offset < body.size()) {
                const auto chunk_size =
                    std::min(fragment_payload_limit, body.size() - offset);
                const bool start = offset == 0;
                const bool end = offset + chunk_size == body.size();
                const bool marker = last_nal && end;

                const auto fu_indicator =
                    static_cast<std::uint8_t>((nal_header & kNalNriMask) | kNalFuA);
                const auto fu_header = static_cast<std::uint8_t>(
                    (start ? kFuStart : 0U) |
                    (end ? kFuEnd : 0U) |
                    nal_type);

                if (!emit_packet(
                        result, config, timestamp_90khz, marker,
                        static_cast<std::uint8_t>(
                            kRtpHeaderBytes + kH264FuAHeaderBytes),
                        body.subspan(offset, chunk_size),
                        sink_context, sink, fu_indicator, fu_header)) {
                    return result;
                }
                offset += chunk_size;
            }
        }

        emitted_any = true;
        current = next;
    }

    if (!emitted_any) {
        result.error = H264PacketizeError::malformed_annex_b;
    }
    return result;
}

H264RtpReassembler::H264RtpReassembler(
    std::span<std::byte> frame_storage, H264RtpConfig config) noexcept
    : storage_(frame_storage), config_(config) {}

void H264RtpReassembler::reset() noexcept {
    size_ = 0;
    timestamp_ = 0;
    expected_sequence_ = 0;
    fu_nal_type_ = 0;
    damage_error_ = H264ReassemblyError::none;
    active_ = false;
    damaged_ = false;
    fu_active_ = false;
    keyframe_ = false;
}

bool H264RtpReassembler::append(std::span<const std::byte> bytes) noexcept {
    if (bytes.size() > storage_.size() - std::min(size_, storage_.size())) {
        return false;
    }
    std::memcpy(storage_.data() + size_, bytes.data(), bytes.size());
    size_ += bytes.size();
    return true;
}

void H264RtpReassembler::begin_frame(
    std::uint32_t timestamp, std::uint16_t sequence) noexcept {
    size_ = 0;
    timestamp_ = timestamp;
    expected_sequence_ = sequence;
    fu_nal_type_ = 0;
    damage_error_ = H264ReassemblyError::none;
    active_ = true;
    damaged_ = false;
    fu_active_ = false;
    keyframe_ = false;
}

void H264RtpReassembler::mark_damage(H264ReassemblyError error) noexcept {
    if (!damaged_) {
        damaged_ = true;
        damage_error_ = error;
    }
}

H264ReassemblyResult H264RtpReassembler::finish_or_drop(bool marker) noexcept {
    if (!marker) {
        return {
            H264ReassemblyStatus::packet_accepted,
            damaged_ ? damage_error_ : H264ReassemblyError::none,
            {}};
    }

    if (fu_active_) {
        mark_damage(H264ReassemblyError::malformed_fragment);
    }

    if (damaged_) {
        const auto error = damage_error_;
        reset();
        return {H264ReassemblyStatus::frame_dropped, error, {}};
    }

    const ReassembledH264Frame frame{
        std::span<const std::byte>(storage_.data(), size_),
        timestamp_,
        keyframe_};
    active_ = false;
    size_ = 0;
    fu_active_ = false;
    return {H264ReassemblyStatus::frame_ready, H264ReassemblyError::none, frame};
}

H264ReassemblyResult H264RtpReassembler::push(
    std::span<const std::byte> datagram) noexcept {
    if (!valid_config(config_) || storage_.empty()) {
        return {
            H264ReassemblyStatus::packet_rejected,
            H264ReassemblyError::invalid_config,
            {}};
    }
    if (datagram.size() <= kRtpHeaderBytes) {
        return {
            H264ReassemblyStatus::packet_rejected,
            H264ReassemblyError::malformed_rtp,
            {}};
    }

    const auto first = value(datagram[0]);
    const auto second = value(datagram[1]);
    if ((first & 0xc0U) != kRtpVersion2 || (first & 0x3fU) != 0) {
        return {
            H264ReassemblyStatus::packet_rejected,
            H264ReassemblyError::malformed_rtp,
            {}};
    }
    if ((second & 0x7fU) != config_.payload_type) {
        return {
            H264ReassemblyStatus::packet_rejected,
            H264ReassemblyError::payload_type_mismatch,
            {}};
    }

    const auto sequence = read_be16(datagram.data() + 2);
    const auto timestamp = read_be32(datagram.data() + 4);
    const auto ssrc = read_be32(datagram.data() + 8);
    const bool marker = (second & kRtpMarker) != 0;
    if (ssrc != config_.ssrc) {
        return {
            H264ReassemblyStatus::packet_rejected,
            H264ReassemblyError::ssrc_mismatch,
            {}};
    }

    if (!active_ || timestamp != timestamp_) {
        begin_frame(timestamp, sequence);
    }

    if (sequence != expected_sequence_) {
        mark_damage(H264ReassemblyError::sequence_gap);
    }
    expected_sequence_ = static_cast<std::uint16_t>(sequence + 1U);

    if (damaged_) {
        return finish_or_drop(marker);
    }

    const auto payload = datagram.subspan(kRtpHeaderBytes);
    if (payload.empty()) {
        mark_damage(H264ReassemblyError::malformed_rtp);
        return finish_or_drop(marker);
    }

    const auto nal_type = static_cast<std::uint8_t>(value(payload[0]) & kNalTypeMask);
    if (nal_type >= 1 && nal_type <= 23) {
        if (fu_active_) {
            mark_damage(H264ReassemblyError::malformed_fragment);
            return finish_or_drop(marker);
        }
        if (!append(kAnnexBStartCode) || !append(payload)) {
            mark_damage(H264ReassemblyError::frame_too_large);
            return finish_or_drop(marker);
        }
        keyframe_ = keyframe_ || nal_type == kNalIdr;
        return finish_or_drop(marker);
    }

    if (nal_type != kNalFuA || payload.size() < 3) {
        mark_damage(H264ReassemblyError::unsupported_nal);
        return finish_or_drop(marker);
    }

    const auto fu_header = value(payload[1]);
    const auto fragment_type = static_cast<std::uint8_t>(fu_header & kNalTypeMask);
    const bool start = (fu_header & kFuStart) != 0;
    const bool end = (fu_header & kFuEnd) != 0;
    if ((fu_header & kFuReserved) != 0 || fragment_type == 0 ||
        (start && end) || (start && fu_active_) ||
        (!start && (!fu_active_ || fragment_type != fu_nal_type_))) {
        mark_damage(H264ReassemblyError::malformed_fragment);
        return finish_or_drop(marker);
    }

    if (start) {
        const auto reconstructed = static_cast<std::byte>(
            (value(payload[0]) & kNalNriMask) | fragment_type);
        if (!append(kAnnexBStartCode) ||
            !append(std::span<const std::byte>(&reconstructed, 1))) {
            mark_damage(H264ReassemblyError::frame_too_large);
            return finish_or_drop(marker);
        }
        fu_active_ = true;
        fu_nal_type_ = fragment_type;
        keyframe_ = keyframe_ || fragment_type == kNalIdr;
    }

    if (!append(payload.subspan(2))) {
        mark_damage(H264ReassemblyError::frame_too_large);
        return finish_or_drop(marker);
    }

    if (end) {
        fu_active_ = false;
        fu_nal_type_ = 0;
    }
    return finish_or_drop(marker);
}

} // namespace catro::video
