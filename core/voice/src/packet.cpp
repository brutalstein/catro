#include <catro/voice/packet.hpp>

#include <algorithm>

namespace catro::voice {
namespace {

constexpr std::byte kMagic0{static_cast<unsigned char>('C')};
constexpr std::byte kMagic1{static_cast<unsigned char>('V')};

void put_u16(std::span<std::byte> out, std::size_t offset, std::uint16_t value) noexcept {
    out[offset] = std::byte{static_cast<unsigned char>((value >> 8U) & 0xffU)};
    out[offset + 1] = std::byte{static_cast<unsigned char>(value & 0xffU)};
}

void put_u32(std::span<std::byte> out, std::size_t offset, std::uint32_t value) noexcept {
    out[offset] = std::byte{static_cast<unsigned char>((value >> 24U) & 0xffU)};
    out[offset + 1] = std::byte{static_cast<unsigned char>((value >> 16U) & 0xffU)};
    out[offset + 2] = std::byte{static_cast<unsigned char>((value >> 8U) & 0xffU)};
    out[offset + 3] = std::byte{static_cast<unsigned char>(value & 0xffU)};
}

std::uint16_t get_u16(std::span<const std::byte> in, std::size_t offset) noexcept {
    return static_cast<std::uint16_t>((std::to_integer<std::uint16_t>(in[offset]) << 8U) |
                                      std::to_integer<std::uint16_t>(in[offset + 1]));
}

std::uint32_t get_u32(std::span<const std::byte> in, std::size_t offset) noexcept {
    return (std::to_integer<std::uint32_t>(in[offset]) << 24U) |
           (std::to_integer<std::uint32_t>(in[offset + 1]) << 16U) |
           (std::to_integer<std::uint32_t>(in[offset + 2]) << 8U) |
           std::to_integer<std::uint32_t>(in[offset + 3]);
}

} // namespace

std::variant<std::size_t, PacketError> write_packet_header(std::uint32_t stream_id,
                                                               std::uint16_t sequence,
                                                               std::uint32_t timestamp,
                                                               std::size_t payload_size,
                                                               std::span<std::byte> output) noexcept {
    if (stream_id == 0) {
        return PacketError::invalid_stream_id;
    }
    if (payload_size == 0) {
        return PacketError::empty_payload;
    }
    if (payload_size > kVoiceMaxPayloadBytes) {
        return PacketError::payload_too_large;
    }
    const auto total = kVoiceHeaderBytes + payload_size;
    if (output.size() < total) {
        return PacketError::output_too_small;
    }

    output[0] = kMagic0;
    output[1] = kMagic1;
    output[2] = std::byte{kVoicePacketVersion};
    output[3] = std::byte{0};
    put_u32(output, 4, stream_id);
    put_u16(output, 8, sequence);
    put_u16(output, 10, 0);
    put_u32(output, 12, timestamp);
    return total;
}

std::variant<std::size_t, PacketError> serialize_packet(const VoicePacketView& packet,
                                                         std::span<std::byte> output) noexcept {
    const auto header = write_packet_header(packet.stream_id, packet.sequence, packet.timestamp,
                                            packet.payload.size(), output);
    if (const auto* error = std::get_if<PacketError>(&header)) {
        return *error;
    }
    std::ranges::copy(packet.payload, output.begin() + static_cast<std::ptrdiff_t>(kVoiceHeaderBytes));
    return std::get<std::size_t>(header);
}

std::variant<VoicePacketView, PacketError> parse_packet(std::span<const std::byte> datagram) noexcept {
    if (datagram.size() < kVoiceHeaderBytes) {
        return PacketError::too_short;
    }
    if (datagram[0] != kMagic0 || datagram[1] != kMagic1) {
        return PacketError::bad_magic;
    }
    if (std::to_integer<std::uint8_t>(datagram[2]) != kVoicePacketVersion) {
        return PacketError::unsupported_version;
    }
    if (std::to_integer<std::uint8_t>(datagram[3]) != 0) {
        return PacketError::unsupported_flags;
    }
    if (get_u16(datagram, 10) != 0) {
        return PacketError::nonzero_reserved;
    }
    const auto payload = datagram.subspan(kVoiceHeaderBytes);
    if (payload.empty()) {
        return PacketError::empty_payload;
    }
    // Bound attacker-controlled datagrams before semantic field validation. This keeps malformed
    // packet classification deterministic and avoids accepting ambiguity from an oversized body.
    if (payload.size() > kVoiceMaxPayloadBytes) {
        return PacketError::payload_too_large;
    }
    if (get_u32(datagram, 4) == 0) {
        return PacketError::invalid_stream_id;
    }
    return VoicePacketView{
        .stream_id = get_u32(datagram, 4),
        .sequence = get_u16(datagram, 8),
        .timestamp = get_u32(datagram, 12),
        .payload = payload,
    };
}

} // namespace catro::voice
