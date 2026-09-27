#include <catro/video/rtp_h264.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <utility>
#include <vector>

using namespace catro::video;

namespace {

struct PacketCollector {
    std::vector<std::vector<std::byte>> datagrams;

    static bool collect(void* context, const RtpPacketSlice& packet) noexcept {
        auto& self = *static_cast<PacketCollector*>(context);
        try {
            std::vector<std::byte> datagram;
            datagram.reserve(
                static_cast<std::size_t>(packet.prefix_size) + packet.payload.size());
            datagram.insert(
                datagram.end(), packet.prefix.begin(),
                packet.prefix.begin() + packet.prefix_size);
            datagram.insert(datagram.end(), packet.payload.begin(), packet.payload.end());
            self.datagrams.push_back(std::move(datagram));
            return true;
        } catch (...) {
            return false;
        }
    }
};

std::vector<std::byte> make_access_unit() {
    std::vector<std::byte> bytes;
    const auto append = [&bytes](std::initializer_list<std::uint8_t> values) {
        for (const auto value : values) {
            bytes.push_back(static_cast<std::byte>(value));
        }
    };

    append({0x00, 0x00, 0x00, 0x01, 0x67, 0x64, 0x00, 0x1f});
    append({0x00, 0x00, 0x00, 0x01, 0x68, 0xee, 0x3c, 0x80});
    append({0x00, 0x00, 0x00, 0x01, 0x65});
    bytes.insert(bytes.end(), 2500, std::byte{0x55});
    return bytes;
}

} // namespace

TEST_CASE("100ns timestamps convert to the 90kHz RTP clock with integer math") {
    CHECK(rtp_timestamp_90khz(0) == 0);
    CHECK(rtp_timestamp_90khz(10'000'000) == 90'000);
    CHECK(rtp_timestamp_90khz(333'333) == 3'000);
    CHECK(rtp_timestamp_90khz(666'666) == 6'000);
}

TEST_CASE("H264 Annex-B packetizer fragments large NALs without copying payloads") {
    const auto access_unit = make_access_unit();
    const H264RtpConfig config{0x10203040U, 96, 1200};
    PacketCollector collector;

    const auto result = packetize_h264_annex_b(
        access_unit, 90'000, 100, config, &collector, &PacketCollector::collect);

    REQUIRE(result);
    CHECK(result.packet_count == 5);
    CHECK(result.next_sequence == 105);
    CHECK(result.keyframe);
    REQUIRE(collector.datagrams.size() == 5);

    for (std::size_t index = 0; index < collector.datagrams.size(); ++index) {
        const auto& packet = collector.datagrams[index];
        REQUIRE(packet.size() <= config.mtu_bytes);
        CHECK((std::to_integer<std::uint8_t>(packet[0]) & 0xc0U) == 0x80U);
        CHECK((std::to_integer<std::uint8_t>(packet[1]) & 0x7fU) == config.payload_type);
        CHECK((std::to_integer<std::uint8_t>(packet[1]) & 0x80U) ==
              (index + 1 == collector.datagrams.size() ? 0x80U : 0U));
    }

    CHECK((std::to_integer<std::uint8_t>(collector.datagrams[2][12]) & 0x1fU) == 28U);
    CHECK((std::to_integer<std::uint8_t>(collector.datagrams[2][13]) & 0x80U) != 0);
    CHECK((std::to_integer<std::uint8_t>(collector.datagrams[4][13]) & 0x40U) != 0);
}

TEST_CASE("H264 RTP reassembler reconstructs one bounded Annex-B access unit") {
    const auto access_unit = make_access_unit();
    const H264RtpConfig config{0x10203040U, 96, 1200};
    PacketCollector collector;
    REQUIRE(packetize_h264_annex_b(
        access_unit, 123'456, 65000, config, &collector, &PacketCollector::collect));

    std::array<std::byte, 4096> storage{};
    H264RtpReassembler reassembler(storage, config);

    H264ReassemblyResult last;
    for (const auto& datagram : collector.datagrams) {
        last = reassembler.push(datagram);
    }

    REQUIRE(last.status == H264ReassemblyStatus::frame_ready);
    CHECK(last.error == H264ReassemblyError::none);
    CHECK(last.frame.timestamp_90khz == 123'456);
    CHECK(last.frame.keyframe);
    REQUIRE(last.frame.annex_b.size() == access_unit.size());
    CHECK(std::equal(
        last.frame.annex_b.begin(), last.frame.annex_b.end(), access_unit.begin()));
}

TEST_CASE("H264 RTP reassembler drops a frame with a sequence gap") {
    const auto access_unit = make_access_unit();
    const H264RtpConfig config{0x55667788U, 97, 1200};
    PacketCollector collector;
    REQUIRE(packetize_h264_annex_b(
        access_unit, 77, 10, config, &collector, &PacketCollector::collect));
    REQUIRE(collector.datagrams.size() >= 5);

    std::array<std::byte, 4096> storage{};
    H264RtpReassembler reassembler(storage, config);

    H264ReassemblyResult result;
    for (std::size_t index = 0; index < collector.datagrams.size(); ++index) {
        if (index == 3) {
            continue;
        }
        result = reassembler.push(collector.datagrams[index]);
    }

    CHECK(result.status == H264ReassemblyStatus::frame_dropped);
    CHECK(result.error == H264ReassemblyError::sequence_gap);
    CHECK(result.frame.annex_b.empty());
}

TEST_CASE("H264 RTP sequence continuity spans frame boundaries and detects a fully lost frame") {
    const auto access_unit = make_access_unit();
    const H264RtpConfig config{0x11223344U, 98, 1200};

    PacketCollector first;
    const auto first_result = packetize_h264_annex_b(
        access_unit, 1'000, 100, config, &first, &PacketCollector::collect);
    REQUIRE(first_result);

    std::array<std::byte, 4096> storage{};
    H264RtpReassembler reassembler(storage, config);
    H264ReassemblyResult result;
    for (const auto& datagram : first.datagrams) {
        result = reassembler.push(datagram);
    }
    REQUIRE(result.status == H264ReassemblyStatus::frame_ready);

    // Pretend every packet with the next five RTP sequence numbers was lost.
    PacketCollector after_loss;
    const auto after_loss_result = packetize_h264_annex_b(
        access_unit, 2'000,
        static_cast<std::uint16_t>(first_result.next_sequence + 5U),
        config, &after_loss, &PacketCollector::collect);
    REQUIRE(after_loss_result);

    for (const auto& datagram : after_loss.datagrams) {
        result = reassembler.push(datagram);
    }
    CHECK(result.status == H264ReassemblyStatus::frame_dropped);
    CHECK(result.error == H264ReassemblyError::sequence_gap);
}

TEST_CASE("H264 RTP sequence tracking handles uint16 wrap across frames") {
    const auto access_unit = make_access_unit();
    const H264RtpConfig config{0x99aabbccU, 99, 1200};

    PacketCollector first;
    const auto first_result = packetize_h264_annex_b(
        access_unit, 10'000, 65'534, config, &first, &PacketCollector::collect);
    REQUIRE(first_result);

    std::array<std::byte, 4096> storage{};
    H264RtpReassembler reassembler(storage, config);
    H264ReassemblyResult result;
    for (const auto& datagram : first.datagrams) {
        result = reassembler.push(datagram);
    }
    REQUIRE(result.status == H264ReassemblyStatus::frame_ready);

    PacketCollector second;
    REQUIRE(packetize_h264_annex_b(
        access_unit, 13'000, first_result.next_sequence,
        config, &second, &PacketCollector::collect));
    for (const auto& datagram : second.datagrams) {
        result = reassembler.push(datagram);
    }
    CHECK(result.status == H264ReassemblyStatus::frame_ready);
    CHECK(result.error == H264ReassemblyError::none);
}

TEST_CASE("H264 RTP reassembler rejects datagrams above its configured MTU") {
    const H264RtpConfig config{0x01020304U, 100, 576};
    std::array<std::byte, 4096> storage{};
    H264RtpReassembler reassembler(storage, config);
    std::array<std::byte, 577> oversized{};
    oversized[0] = std::byte{0x80};
    oversized[1] = std::byte{100};

    const auto result = reassembler.push(oversized);
    CHECK(result.status == H264ReassemblyStatus::packet_rejected);
    CHECK(result.error == H264ReassemblyError::malformed_rtp);
}

TEST_CASE("H264 packetizer rejects bytes that are not Annex-B") {
    const std::array<std::byte, 4> bytes{
        std::byte{0x65}, std::byte{0x01}, std::byte{0x02}, std::byte{0x03}};
    PacketCollector collector;
    const auto result = packetize_h264_annex_b(
        bytes, 0, 1, {}, &collector, &PacketCollector::collect);

    CHECK_FALSE(result);
    CHECK(result.error == H264PacketizeError::malformed_annex_b);
    CHECK(collector.datagrams.empty());
}
