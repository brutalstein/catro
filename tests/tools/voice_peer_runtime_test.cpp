#include "udp_socket.hpp"
#include "voice_peer.hpp"

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

capabilities::AudioEndpointId endpoint(std::string value) {
    return {std::move(value), capabilities::IdentityScope::persistent};
}

class CaptureStream final : public audio::AudioStream {
public:
    CaptureStream(audio::CaptureSink& sink, float sample)
        : sink_(sink), sample_(sample),
          info_{endpoint("fake-in"), 48000, 1, 480, 0} {}

    ~CaptureStream() override {
        request_stop();
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    const audio::StreamInfo& info() const noexcept override {
        return info_;
    }

    std::optional<audio::AudioError> start() override {
        thread_ = std::thread([this] {
            std::array<float, 480> frames{};
            double phase = 0.0;
            constexpr double step = 2.0 * std::numbers::pi * 300.0 / 48000.0;
            auto next = std::chrono::steady_clock::now();
            while (!stop_.load(std::memory_order_acquire)) {
                for (auto& frame : frames) {
                    frame = sample_ * static_cast<float>(std::sin(phase));
                    phase += step;
                    if (phase >= 2.0 * std::numbers::pi) {
                        phase -= 2.0 * std::numbers::pi;
                    }
                }
                sink_.on_captured(frames);
                next += 10ms;
                std::this_thread::sleep_until(next);
            }
        });
        return std::nullopt;
    }

    void request_stop() noexcept override {
        stop_.store(true, std::memory_order_release);
    }

    std::uint64_t glitches() const noexcept override {
        return 0;
    }

private:
    audio::CaptureSink& sink_;
    float sample_;
    audio::StreamInfo info_;
    std::atomic_bool stop_{false};
    std::thread thread_;
};

class RenderStream final : public audio::AudioStream {
public:
    RenderStream(audio::RenderSource& source, std::atomic<std::uint64_t>& nonzero_samples)
        : source_(source), nonzero_samples_(nonzero_samples),
          info_{endpoint("fake-out"), 48000, 2, 480, 0} {}

    ~RenderStream() override {
        request_stop();
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    const audio::StreamInfo& info() const noexcept override {
        return info_;
    }

    std::optional<audio::AudioError> start() override {
        thread_ = std::thread([this] {
            std::array<float, 480> frames{};
            auto next = std::chrono::steady_clock::now();
            while (!stop_.load(std::memory_order_acquire)) {
                source_.on_render(frames);
                std::uint64_t audible = 0;
                for (const auto sample : frames) {
                    audible += std::abs(sample) > 1e-5F ? 1U : 0U;
                }
                nonzero_samples_.fetch_add(audible, std::memory_order_relaxed);
                next += 10ms;
                std::this_thread::sleep_until(next);
            }
        });
        return std::nullopt;
    }

    void request_stop() noexcept override {
        stop_.store(true, std::memory_order_release);
    }

    std::uint64_t glitches() const noexcept override {
        return 0;
    }

private:
    audio::RenderSource& source_;
    std::atomic<std::uint64_t>& nonzero_samples_;
    audio::StreamInfo info_;
    std::atomic_bool stop_{false};
    std::thread thread_;
};

class FakeAudioPlatform final : public audio::AudioPlatform {
public:
    explicit FakeAudioPlatform(float capture_sample) : capture_sample_(capture_sample) {}

    audio::OpenResult open_capture(const std::optional<capabilities::AudioEndpointId>&,
                                   audio::CaptureSink& sink,
                                   audio::StreamFailure) override {
        return std::unique_ptr<audio::AudioStream>(
            std::make_unique<CaptureStream>(sink, capture_sample_));
    }

    audio::OpenResult open_render(const std::optional<capabilities::AudioEndpointId>&,
                                  audio::RenderSource& source,
                                  audio::StreamFailure) override {
        return std::unique_ptr<audio::AudioStream>(
            std::make_unique<RenderStream>(source, rendered_nonzero_samples));
    }

    std::atomic<std::uint64_t> rendered_nonzero_samples{0};

private:
    float capture_sample_;
};

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
