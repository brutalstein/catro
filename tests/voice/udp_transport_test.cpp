#include "udp_socket.hpp"

#include <catro/voice/pipeline.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <memory>
#include <span>
#include <variant>

using namespace catro::tools;
using namespace catro::voice;
using namespace std::chrono_literals;

namespace {

std::unique_ptr<UdpPeerSocket> bind_loopback() {
    auto result = UdpPeerSocket::bind({"127.0.0.1", 0});
    REQUIRE(std::holds_alternative<std::unique_ptr<UdpPeerSocket>>(result));
    auto socket = std::move(std::get<std::unique_ptr<UdpPeerSocket>>(result));
    REQUIRE(socket->local_port() != 0);
    return socket;
}

void connect_pair(UdpPeerSocket& first, UdpPeerSocket& second) {
    const auto first_connected = first.connect_peer({"127.0.0.1", second.local_port()});
    const auto second_connected = second.connect_peer({"127.0.0.1", first.local_port()});
    REQUIRE(std::holds_alternative<std::monostate>(first_connected));
    REQUIRE(std::holds_alternative<std::monostate>(second_connected));
}

std::unique_ptr<VoicePipeline> pipeline(std::uint32_t stream_id, std::uint16_t jitter) {
    VoicePipelineConfig config;
    config.local_stream_id = stream_id;
    config.jitter_target_packets = jitter;
    auto result = VoicePipeline::create(config);
    REQUIRE(std::holds_alternative<std::unique_ptr<VoicePipeline>>(result));
    return std::move(std::get<std::unique_ptr<VoicePipeline>>(result));
}

PcmFrame frame(double& phase, float amplitude) {
    PcmFrame pcm{};
    for (auto& sample : pcm) {
        sample = amplitude * static_cast<float>(std::sin(phase));
        phase += 2.0 * 3.14159265358979323846 * 260.0 / static_cast<double>(kSampleRate);
    }
    return pcm;
}

OutboundDatagram encode(VoicePipeline& sender, const PcmFrame& pcm) {
    sender.capture().on_captured(pcm);
    OutboundDatagram datagram;
    const auto result = sender.encode_next(datagram);
    REQUIRE(std::holds_alternative<EncodeStep>(result));
    REQUIRE(std::get<EncodeStep>(result) == EncodeStep::packet_ready);
    return datagram;
}

} // namespace

TEST_CASE("UDP transport rejects non-numeric endpoints and releases bound ports") {
    const auto invalid = UdpPeerSocket::bind({"not-an-ip", 0});
    REQUIRE(std::holds_alternative<UdpError>(invalid));
    CHECK(std::get<UdpError>(invalid).code == UdpErrorCode::invalid_endpoint);

    std::uint16_t port = 0;
    {
        auto socket = bind_loopback();
        port = socket->local_port();
        REQUIRE(port != 0);
    }

    const auto rebound = UdpPeerSocket::bind({"127.0.0.1", port});
    CHECK(std::holds_alternative<std::unique_ptr<UdpPeerSocket>>(rebound));
}

TEST_CASE("connected UDP sockets exchange one bounded datagram on loopback") {
    auto first = bind_loopback();
    auto second = bind_loopback();
    connect_pair(*first, *second);

    const std::array payload{std::byte{0x10}, std::byte{0x20}, std::byte{0x30}, std::byte{0x40}};
    const auto sent = first->send(payload);
    REQUIRE(std::holds_alternative<std::size_t>(sent));
    CHECK(std::get<std::size_t>(sent) == payload.size());

    const auto ready = second->wait_readable(500ms);
    REQUIRE(std::holds_alternative<bool>(ready));
    REQUIRE(std::get<bool>(ready));

    std::array<std::byte, 64> received{};
    const auto result = second->receive(received);
    REQUIRE(std::holds_alternative<std::size_t>(result));
    const auto size = std::get<std::size_t>(result);
    REQUIRE(size == payload.size());
    CHECK(std::equal(payload.begin(), payload.end(), received.begin()));

    const auto empty = second->receive(received);
    REQUIRE(std::holds_alternative<std::size_t>(empty));
    CHECK(std::get<std::size_t>(empty) == 0);
}

TEST_CASE("oversized UDP datagrams are rejected instead of silently truncated") {
    auto first = bind_loopback();
    auto second = bind_loopback();
    connect_pair(*first, *second);

    std::array<std::byte, kMaxVoiceDatagramBytes + 64> oversized{};
    const auto sent = first->send(oversized);
    REQUIRE(std::holds_alternative<std::size_t>(sent));
    REQUIRE(std::get<std::size_t>(sent) == oversized.size());

    const auto ready = second->wait_readable(500ms);
    REQUIRE(std::holds_alternative<bool>(ready));
    REQUIRE(std::get<bool>(ready));

    std::array<std::byte, kMaxVoiceDatagramBytes + 1> received{};
    const auto result = second->receive(received);
    REQUIRE(std::holds_alternative<UdpError>(result));
    CHECK(std::get<UdpError>(result).code == UdpErrorCode::datagram_too_large);
}

TEST_CASE("actual UDP loopback carries Opus packets into the jitter decoder") {
    auto tx = bind_loopback();
    auto rx = bind_loopback();
    connect_pair(*tx, *rx);

    auto sender = pipeline(0x11111111U, 1);
    auto receiver = pipeline(0x22222222U, 3);
    double phase = 0.0;

    for (int index = 0; index < 3; ++index) {
        const auto datagram = encode(*sender, frame(phase, 0.12F + 0.02F * static_cast<float>(index)));
        const auto sent = tx->send(datagram.view());
        REQUIRE(std::holds_alternative<std::size_t>(sent));
        REQUIRE(std::get<std::size_t>(sent) == datagram.size);
    }

    std::array<std::byte, kMaxVoiceDatagramBytes> buffer{};
    int packets = 0;
    while (packets < 3) {
        const auto ready = rx->wait_readable(500ms);
        REQUIRE(std::holds_alternative<bool>(ready));
        REQUIRE(std::get<bool>(ready));

        for (;;) {
            const auto received = rx->receive(buffer);
            REQUIRE(std::holds_alternative<std::size_t>(received));
            const auto size = std::get<std::size_t>(received);
            if (size == 0) {
                break;
            }
            const auto accepted = receiver->receive(std::span<const std::byte>(buffer).first(size));
            REQUIRE(std::holds_alternative<JitterPushResult>(accepted));
            CHECK(std::get<JitterPushResult>(accepted) == JitterPushResult::accepted);
            ++packets;
        }
    }

    for (int index = 0; index < 3; ++index) {
        const auto decoded = receiver->decode_next();
        REQUIRE(std::holds_alternative<DecodeStep>(decoded));
        CHECK(std::get<DecodeStep>(decoded) == DecodeStep::queued_packet);
    }

    PcmFrame output{};
    double energy = 0.0;
    for (int index = 0; index < 3; ++index) {
        receiver->render().on_render(output);
        for (const auto sample : output) {
            REQUIRE(std::isfinite(sample));
            energy += static_cast<double>(sample) * sample;
        }
    }
    CHECK(energy > 0.01);

    const auto stats = receiver->statistics();
    CHECK(stats.received_datagrams == 3);
    CHECK(stats.jitter.played == 3);
    CHECK(stats.decode_errors == 0);
    CHECK(stats.render_queue_full == 0);
}
