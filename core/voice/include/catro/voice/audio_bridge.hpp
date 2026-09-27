#pragma once

#include <catro/audio/engine.hpp>
#include <catro/audio/realtime.hpp>
#include <catro/voice/codec.hpp>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>

namespace catro::voice {

using PcmFrame = std::array<float, kFrameSamples>;

inline constexpr std::size_t kDefaultRealtimeQueueFrames = 8;
inline constexpr std::size_t kMaxRealtimeQueueFrames = 64;

struct CaptureBridgeStatistics {
    std::uint64_t callbacks = 0;
    std::uint64_t captured_samples = 0;
    std::uint64_t dropped_callbacks = 0;
    std::uint64_t dropped_samples = 0;
    std::uint64_t frames_dequeued = 0;
    std::size_t buffered_samples = 0;
    std::size_t peak_buffered_samples = 0;

    friend bool operator==(const CaptureBridgeStatistics&, const CaptureBridgeStatistics&) = default;
};

struct RenderBridgeStatistics {
    std::uint64_t callbacks = 0;
    std::uint64_t requested_samples = 0;
    std::uint64_t pcm_samples_rendered = 0;
    std::uint64_t silence_samples_rendered = 0;
    std::uint64_t startup_silence_samples = 0;
    std::uint64_t underrun_callbacks = 0;
    std::uint64_t frames_enqueued = 0;
    // Queue-full attempts. A caller may retry the same frame; actual media drops are owned by the pipeline.
    std::uint64_t push_rejections = 0;
    std::size_t buffered_samples = 0;
    std::size_t peak_buffered_samples = 0;

    friend bool operator==(const RenderBridgeStatistics&, const RenderBridgeStatistics&) = default;
};

// Audio-thread producer -> voice-worker consumer. Callback writes are all-or-nothing so an
// overloaded worker cannot splice half a callback into the next 20 ms codec frame.
class CaptureBridge final : public audio::CaptureSink {
public:
    explicit CaptureBridge(std::size_t queue_frames = kDefaultRealtimeQueueFrames);

    void on_captured(std::span<const float> frames) noexcept override;

    // Voice worker only. Returns one exact 20 ms / 960-sample frame when ready.
    [[nodiscard]] bool try_pop(PcmFrame& frame) noexcept;
    [[nodiscard]] CaptureBridgeStatistics statistics() const noexcept;
    [[nodiscard]] std::size_t capacity_samples() const noexcept { return ring_.capacity(); }

private:
    void update_peak(std::size_t depth) noexcept;

    audio::SpscRing<float> ring_;
    std::atomic<std::uint64_t> callbacks_{0};
    std::atomic<std::uint64_t> captured_samples_{0};
    std::atomic<std::uint64_t> dropped_callbacks_{0};
    std::atomic<std::uint64_t> dropped_samples_{0};
    std::atomic<std::uint64_t> frames_dequeued_{0};
    std::atomic<std::size_t> peak_buffered_samples_{0};
};

// Voice-worker producer -> audio-thread consumer. The worker enqueues complete decoded frames;
// the callback never waits and fills any shortage with silence.
class RenderBridge final : public audio::RenderSource {
public:
    explicit RenderBridge(std::size_t queue_frames = kDefaultRealtimeQueueFrames);

    // Voice worker only. Commits one whole 20 ms frame or rejects it if the bounded queue is full.
    [[nodiscard]] bool try_push(const PcmFrame& frame) noexcept;
    void on_render(std::span<float> frames) noexcept override;

    [[nodiscard]] RenderBridgeStatistics statistics() const noexcept;
    [[nodiscard]] std::size_t capacity_samples() const noexcept { return ring_.capacity(); }

private:
    void update_peak(std::size_t depth) noexcept;

    audio::SpscRing<float> ring_;
    std::atomic<std::uint64_t> callbacks_{0};
    std::atomic<std::uint64_t> requested_samples_{0};
    std::atomic<std::uint64_t> pcm_samples_rendered_{0};
    std::atomic<std::uint64_t> silence_samples_rendered_{0};
    std::atomic<std::uint64_t> startup_silence_samples_{0};
    std::atomic<std::uint64_t> underrun_callbacks_{0};
    std::atomic_bool primed_{false};
    std::atomic<std::uint64_t> frames_enqueued_{0};
    std::atomic<std::uint64_t> push_rejections_{0};
    std::atomic<std::size_t> peak_buffered_samples_{0};
};

} // namespace catro::voice
