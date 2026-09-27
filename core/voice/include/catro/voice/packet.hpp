#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <variant>

namespace catro::voice {

inline constexpr std::size_t kVoiceHeaderBytes = 16;
inline constexpr std::size_t kVoiceMaxPayloadBytes = 1275;
inline constexpr std::uint8_t kVoicePacketVersion = 1;

enum class PacketError {
    output_too_small,
    too_short,
    bad_magic,
    unsupported_version,
    unsupported_flags,
    nonzero_reserved,
    invalid_stream_id,
    empty_payload,
    payload_too_large,
};

[[nodiscard]] constexpr std::string_view name(PacketError error) noexcept {
    switch (error) {
    case PacketError::output_too_small:
        return "output too small";
    case PacketError::too_short:
        return "packet too short";
    case PacketError::bad_magic:
        return "bad magic";
    case PacketError::unsupported_version:
        return "unsupported version";
    case PacketError::unsupported_flags:
        return "unsupported flags";
    case PacketError::nonzero_reserved:
        return "reserved field is nonzero";
    case PacketError::invalid_stream_id:
        return "stream id is zero";
    case PacketError::empty_payload:
        return "empty payload";
    case PacketError::payload_too_large:
        return "payload too large";
    }
    return "packet error";
}

struct VoicePacketView {
    std::uint32_t stream_id = 0;
    std::uint16_t sequence = 0;
    std::uint32_t timestamp = 0;
    std::span<const std::byte> payload;
};

[[nodiscard]] std::variant<std::size_t, PacketError> write_packet_header(std::uint32_t stream_id,
                                                                            std::uint16_t sequence,
                                                                            std::uint32_t timestamp,
                                                                            std::size_t payload_size,
                                                                            std::span<std::byte> output) noexcept;
[[nodiscard]] std::variant<std::size_t, PacketError> serialize_packet(const VoicePacketView& packet,
                                                                      std::span<std::byte> output) noexcept;
[[nodiscard]] std::variant<VoicePacketView, PacketError> parse_packet(std::span<const std::byte> datagram) noexcept;

} // namespace catro::voice
