#include "udp_socket.hpp"
#include "voice_peer.hpp"

#include "helpers/fake_audio_platform.hpp"

#include <catro/audio/engine.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <numbers>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

using namespace catro;
using namespace catro::tools;
using namespace std::chrono_literals;

namespace {

using test::FakeAudioPlatform;

std::pair<std::uint16_t, std::uint16_t> reserve_ports() {
    auto first_result = UdpPeerSocket::bind({"127.0.0.1", 0});
    auto second_result = UdpPeerSocket::bind({"127.0.0.1", 0});
    REQUIRE(std::holds_alternative<std::unique_ptr<UdpPeerSocket>>(first_result));
    REQUIRE(std::holds_alternative<std::unique_ptr<UdpPeerSocket>>(second_result));
    auto first = std::move(std::get<std::unique_ptr<UdpPeerSocket>>(first_result));
    auto second = std::move(std::get<std::unique_ptr<UdpPeerSocket>>(second_result));
    const auto first_port = first->local_port();
    const auto second_port = second->local_port();
    REQUIRE(first_port != 0);
    REQUIRE(second_port != 0);
    REQUIRE(first_port != second_port);
    return {first_port, second_port};
}

std::vector<std::string_view> views(const std::vector<std::string>& storage) {
    return {storage.begin(), storage.end()};
}

std::vector<std::string> arguments(std::uint16_t local, std::uint16_t peer,
                                   std::string mode, std::uint32_t stream_id) {
    return {
        "--bind", "127.0.0.1:" + std::to_string(local),
        "--peer", "127.0.0.1:" + std::to_string(peer),
        "--mode", std::move(mode),
        "--seconds", "1",
        "--stream-id", std::to_string(stream_id),
        "--jitter", "2",
    };
}

} // namespace

TEST_CASE("voice peer runs one-way capture UDP jitter decode and render end to end") {
    const auto [send_port, receive_port] = reserve_ports();
    FakeAudioPlatform sender_audio(0.15F);
    FakeAudioPlatform receiver_audio(0.0F);
    auto sender_args = arguments(send_port, receive_port, "send", 1001);
    auto receiver_args = arguments(receive_port, send_port, "receive", 2001);
    std::ostringstream sender_out;
    std::ostringstream sender_error;
    std::ostringstream receiver_out;
    std::ostringstream receiver_error;
    int sender_exit = -1;
    int receiver_exit = -1;

    std::thread receiver([&] {
        const auto args = views(receiver_args);
        receiver_exit = run_voice_peer(args, receiver_audio, receiver_out, receiver_error);
    });
    std::this_thread::sleep_for(20ms);
    std::thread sender([&] {
        const auto args = views(sender_args);
        sender_exit = run_voice_peer(args, sender_audio, sender_out, sender_error);
    });

    sender.join();
    receiver.join();

    CHECK(sender_exit == voice_peer_ok);
    CHECK(receiver_exit == voice_peer_ok);
    CHECK(receiver_audio.rendered_nonzero_samples.load(std::memory_order_relaxed) > 0);
    CHECK(sender_out.str().find("final:") != std::string::npos);
    CHECK(receiver_out.str().find("final:") != std::string::npos);
    CHECK(sender_error.str().empty());
    CHECK(receiver_error.str().empty());
}

TEST_CASE("voice peer full duplex exchanges audible media in both directions") {
    const auto [first_port, second_port] = reserve_ports();
    FakeAudioPlatform first_audio(0.12F);
    FakeAudioPlatform second_audio(-0.18F);
    auto first_args = arguments(first_port, second_port, "duplex", 3001);
    auto second_args = arguments(second_port, first_port, "duplex", 3002);
    std::ostringstream first_out;
    std::ostringstream first_error;
    std::ostringstream second_out;
    std::ostringstream second_error;
    int first_exit = -1;
    int second_exit = -1;

    std::thread first([&] {
        const auto args = views(first_args);
        first_exit = run_voice_peer(args, first_audio, first_out, first_error);
    });
    std::this_thread::sleep_for(20ms);
    std::thread second([&] {
        const auto args = views(second_args);
        second_exit = run_voice_peer(args, second_audio, second_out, second_error);
    });

    first.join();
    second.join();

    CHECK(first_exit == voice_peer_ok);
    CHECK(second_exit == voice_peer_ok);
    CHECK(first_audio.rendered_nonzero_samples.load(std::memory_order_relaxed) > 0);
    CHECK(second_audio.rendered_nonzero_samples.load(std::memory_order_relaxed) > 0);
    CHECK(first_error.str().empty());
    CHECK(second_error.str().empty());
}

TEST_CASE("voice peer runtime applies mute and deafen without restarting the session") {
    const auto [first_port, second_port] = reserve_ports();
    FakeAudioPlatform first_audio(0.25F);
    FakeAudioPlatform second_audio(-0.20F);

    VoicePeerOptions first_options;
    first_options.bind = {"127.0.0.1", first_port};
    first_options.peer = {"127.0.0.1", second_port};
    first_options.mode = VoicePeerMode::duplex;
    first_options.duration = 5s;
    first_options.stream_id = 5101;
    first_options.jitter_packets = 2;

    VoicePeerOptions second_options = first_options;
    second_options.bind.port = second_port;
    second_options.peer.port = first_port;
    second_options.stream_id = 5102;

    VoicePeerControl first_control;
    VoicePeerControl second_control;
    std::ostringstream first_out;
    std::ostringstream first_error;
    std::ostringstream second_out;
    std::ostringstream second_error;
    int first_exit = -1;
    int second_exit = -1;

    std::thread first([&] {
        first_exit = run_voice_peer(first_options, first_audio, first_out, first_error, &first_control);
    });
    std::thread second([&] {
        second_exit = run_voice_peer(second_options, second_audio, second_out, second_error, &second_control);
    });

    std::this_thread::sleep_for(150ms);
    first_control.muted.store(true, std::memory_order_release);
    second_control.deafened.store(true, std::memory_order_release);
    std::this_thread::sleep_for(100ms);
    CHECK(first_control.muted.load(std::memory_order_acquire));
    CHECK(second_control.deafened.load(std::memory_order_acquire));

    first_control.muted.store(false, std::memory_order_release);
    second_control.deafened.store(false, std::memory_order_release);
    std::this_thread::sleep_for(100ms);

    first_control.stop_requested.store(true, std::memory_order_release);
    second_control.stop_requested.store(true, std::memory_order_release);
    first.join();
    second.join();

    CHECK(first_exit == voice_peer_ok);
    CHECK(second_exit == voice_peer_ok);
    CHECK(first_error.str().empty());
    CHECK(second_error.str().empty());
}

TEST_CASE("voice peer cooperative stop releases audio and UDP resources for immediate restart") {
    const auto [local_port, peer_port] = reserve_ports();
    auto args = arguments(local_port, peer_port, "send", 4001);
    args[7] = "300";

    for (int attempt = 0; attempt < 2; ++attempt) {
        FakeAudioPlatform audio(0.10F + 0.01F * static_cast<float>(attempt));
        std::ostringstream out;
        std::ostringstream error;
        VoicePeerControl control;
        int exit_code = -1;

        const auto started = std::chrono::steady_clock::now();
        std::thread peer([&] {
            const auto current = views(args);
            exit_code = run_voice_peer(current, audio, out, error, &control);
        });

        std::this_thread::sleep_for(50ms);
        control.stop_requested.store(true, std::memory_order_release);
        peer.join();
        const auto elapsed = std::chrono::steady_clock::now() - started;

        CHECK(exit_code == voice_peer_ok);
        CHECK(elapsed < 2s);
        CHECK(out.str().find("final:") != std::string::npos);
        CHECK(error.str().empty());
    }
}
