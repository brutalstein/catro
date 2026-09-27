#include <catro/voice/audio_bridge.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <thread>

using namespace catro::voice;

TEST_CASE("capture bridge assembles arbitrary callback blocks into exact codec frames") {
    CaptureBridge bridge(2);
    std::array<float, 480> first{};
    std::array<float, 480> second{};
    std::fill(first.begin(), first.end(), 0.25F);
    std::fill(second.begin(), second.end(), -0.5F);

    bridge.on_captured(first);
    PcmFrame frame{};
    CHECK_FALSE(bridge.try_pop(frame));

    bridge.on_captured(second);
    REQUIRE(bridge.try_pop(frame));
    CHECK(std::ranges::all_of(std::span(frame).first(480), [](float sample) { return sample == 0.25F; }));
    CHECK(std::ranges::all_of(std::span(frame).last(480), [](float sample) { return sample == -0.5F; }));

    const auto stats = bridge.statistics();
    CHECK(stats.callbacks == 2);
    CHECK(stats.captured_samples == 960);
    CHECK(stats.dropped_callbacks == 0);
    CHECK(stats.dropped_samples == 0);
    CHECK(stats.frames_dequeued == 1);
    CHECK(stats.buffered_samples == 0);
    CHECK(stats.peak_buffered_samples >= 960);
}

TEST_CASE("capture overload drops whole callback blocks without corrupting queued frames") {
    CaptureBridge bridge(2);
    PcmFrame one{};
    PcmFrame two{};
    PcmFrame three{};
    std::fill(one.begin(), one.end(), 1.0F);
    std::fill(two.begin(), two.end(), 2.0F);
    std::fill(three.begin(), three.end(), 3.0F);

    bridge.on_captured(one);
    bridge.on_captured(two);
    bridge.on_captured(three);

    auto stats = bridge.statistics();
    CHECK(stats.dropped_callbacks == 1);
    CHECK(stats.dropped_samples == kFrameSamples);
    CHECK(stats.buffered_samples == 2U * kFrameSamples);

    PcmFrame out{};
    REQUIRE(bridge.try_pop(out));
    CHECK(out == one);
    REQUIRE(bridge.try_pop(out));
    CHECK(out == two);
    CHECK_FALSE(bridge.try_pop(out));
}

TEST_CASE("render bridge startup silence is not reported as an underrun") {
    RenderBridge bridge(2);
    std::array<float, 480> output{};
    std::fill(output.begin(), output.end(), 1.0F);
    bridge.on_render(output);

    CHECK(std::ranges::all_of(output, [](float sample) { return sample == 0.0F; }));
    const auto stats = bridge.statistics();
    CHECK(stats.underrun_callbacks == 0);
    CHECK(stats.startup_silence_samples == output.size());
    CHECK(stats.silence_samples_rendered == output.size());
}

TEST_CASE("render bridge emits queued PCM then silence instead of blocking on underrun") {
    RenderBridge bridge(2);
    PcmFrame frame{};
    for (std::size_t index = 0; index < frame.size(); ++index) {
        frame[index] = static_cast<float>(index);
    }
    REQUIRE(bridge.try_push(frame));

    std::array<float, 480> first{};
    std::array<float, 480> second{};
    std::array<float, 480> empty{};
    bridge.on_render(first);
    bridge.on_render(second);
    std::fill(empty.begin(), empty.end(), 99.0F);
    bridge.on_render(empty);

    CHECK(std::equal(first.begin(), first.end(), frame.begin()));
    CHECK(std::equal(second.begin(), second.end(), frame.begin() + 480));
    CHECK(std::ranges::all_of(empty, [](float sample) { return sample == 0.0F; }));

    const auto stats = bridge.statistics();
    CHECK(stats.callbacks == 3);
    CHECK(stats.requested_samples == 1440);
    CHECK(stats.pcm_samples_rendered == 960);
    CHECK(stats.silence_samples_rendered == 480);
    CHECK(stats.underrun_callbacks == 1);
    CHECK(stats.frames_enqueued == 1);
    CHECK(stats.push_rejections == 0);
}

TEST_CASE("render queue is bounded and drops complete decoded frames when full") {
    RenderBridge bridge(2);
    PcmFrame one{};
    PcmFrame two{};
    PcmFrame three{};
    std::fill(one.begin(), one.end(), 1.0F);
    std::fill(two.begin(), two.end(), 2.0F);
    std::fill(three.begin(), three.end(), 3.0F);

    CHECK(bridge.try_push(one));
    CHECK(bridge.try_push(two));
    CHECK_FALSE(bridge.try_push(three));

    const auto before = bridge.statistics();
    CHECK(before.frames_enqueued == 2);
    CHECK(before.push_rejections == 1);
    CHECK(before.buffered_samples == 2U * kFrameSamples);

    PcmFrame out{};
    bridge.on_render(out);
    CHECK(out == one);
    bridge.on_render(out);
    CHECK(out == two);
}

TEST_CASE("real-time bridge memory is clamped to a small fixed ceiling") {
    CaptureBridge minimum(0);
    RenderBridge maximum(1'000'000);
    CHECK(minimum.capacity_samples() == 2048);
    CHECK(maximum.capacity_samples() == 65536);
}

TEST_CASE("capture bridge preserves frame order under sustained two-thread load") {
    constexpr std::size_t kFrames = 1000;
    CaptureBridge bridge(64);
    std::atomic_bool ordered = true;

    std::thread producer([&] {
        std::array<float, 480> block{};
        for (std::size_t frame_index = 0; frame_index < kFrames; ++frame_index) {
            const auto marker = static_cast<float>(frame_index + 1);
            std::fill(block.begin(), block.end(), marker);
            for (int half = 0; half < 2; ++half) {
                while (bridge.capacity_samples() - bridge.statistics().buffered_samples < block.size()) {
                    std::this_thread::yield();
                }
                bridge.on_captured(block);
            }
        }
    });

    PcmFrame frame{};
    for (std::size_t frame_index = 0; frame_index < kFrames; ++frame_index) {
        while (!bridge.try_pop(frame)) {
            std::this_thread::yield();
        }
        const auto expected = static_cast<float>(frame_index + 1);
        if (!std::ranges::all_of(frame, [expected](float sample) { return sample == expected; })) {
            ordered.store(false, std::memory_order_relaxed);
        }
    }
    producer.join();

    CHECK(ordered.load(std::memory_order_relaxed));
    CHECK(bridge.statistics().dropped_callbacks == 0);
    CHECK(bridge.statistics().frames_dequeued == kFrames);
}

TEST_CASE("render bridge preserves decoded frame order under sustained two-thread load") {
    constexpr std::size_t kFrames = 1000;
    RenderBridge bridge(64);
    std::atomic_bool ordered = true;

    std::thread producer([&] {
        PcmFrame frame{};
        for (std::size_t frame_index = 0; frame_index < kFrames; ++frame_index) {
            const auto marker = static_cast<float>(frame_index + 1);
            std::fill(frame.begin(), frame.end(), marker);
            while (!bridge.try_push(frame)) {
                std::this_thread::yield();
            }
        }
    });

    std::array<float, 480> block{};
    for (std::size_t frame_index = 0; frame_index < kFrames; ++frame_index) {
        while (bridge.statistics().buffered_samples < kFrameSamples) {
            std::this_thread::yield();
        }
        const auto expected = static_cast<float>(frame_index + 1);
        bridge.on_render(block);
        if (!std::ranges::all_of(block, [expected](float sample) { return sample == expected; })) {
            ordered.store(false, std::memory_order_relaxed);
        }
        bridge.on_render(block);
        if (!std::ranges::all_of(block, [expected](float sample) { return sample == expected; })) {
            ordered.store(false, std::memory_order_relaxed);
        }
    }
    producer.join();

    const auto stats = bridge.statistics();
    CHECK(ordered.load(std::memory_order_relaxed));
    // The producer intentionally retries when the bounded queue is full. Rejections are
    // expected under scheduler pressure, but no unique frame is lost because the same frame is
    // retried until accepted.
    CHECK(stats.push_rejections > 0);
    CHECK(stats.underrun_callbacks == 0);
    CHECK(stats.frames_enqueued == kFrames);
    CHECK(stats.pcm_samples_rendered == kFrames * kFrameSamples);
}
