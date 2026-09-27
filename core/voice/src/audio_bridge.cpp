#include <catro/voice/audio_bridge.hpp>

#include <algorithm>

namespace catro::voice {
namespace {

std::size_t requested_samples(std::size_t queue_frames) noexcept {
    const auto frames = std::clamp<std::size_t>(queue_frames, 2, kMaxRealtimeQueueFrames);
    return frames * static_cast<std::size_t>(kFrameSamples);
}

void update_atomic_peak(std::atomic<std::size_t>& peak, std::size_t value) noexcept {
    auto current = peak.load(std::memory_order_relaxed);
    while (value > current &&
           !peak.compare_exchange_weak(current, value, std::memory_order_relaxed, std::memory_order_relaxed)) {
    }
}

} // namespace

static_assert(kSampleRate == audio::kSampleRate);

CaptureBridge::CaptureBridge(std::size_t queue_frames) : ring_(requested_samples(queue_frames)) {}

void CaptureBridge::update_peak(std::size_t depth) noexcept {
    update_atomic_peak(peak_buffered_samples_, depth);
}

void CaptureBridge::on_captured(std::span<const float> frames) noexcept {
    callbacks_.fetch_add(1, std::memory_order_relaxed);
    captured_samples_.fetch_add(frames.size(), std::memory_order_relaxed);
    if (frames.empty()) {
        return;
    }
    if (!ring_.write_exact(frames)) {
        dropped_callbacks_.fetch_add(1, std::memory_order_relaxed);
        dropped_samples_.fetch_add(frames.size(), std::memory_order_relaxed);
        return;
    }
    update_peak(ring_.size());
}

bool CaptureBridge::try_pop(PcmFrame& frame) noexcept {
    if (!ring_.read_exact(std::span<float>(frame))) {
        return false;
    }
    frames_dequeued_.fetch_add(1, std::memory_order_relaxed);
    return true;
}

CaptureBridgeStatistics CaptureBridge::statistics() const noexcept {
    return {
        .callbacks = callbacks_.load(std::memory_order_relaxed),
        .captured_samples = captured_samples_.load(std::memory_order_relaxed),
        .dropped_callbacks = dropped_callbacks_.load(std::memory_order_relaxed),
        .dropped_samples = dropped_samples_.load(std::memory_order_relaxed),
        .frames_dequeued = frames_dequeued_.load(std::memory_order_relaxed),
        .buffered_samples = ring_.size(),
        .peak_buffered_samples = peak_buffered_samples_.load(std::memory_order_relaxed),
    };
}

RenderBridge::RenderBridge(std::size_t queue_frames) : ring_(requested_samples(queue_frames)) {}

void RenderBridge::update_peak(std::size_t depth) noexcept {
    update_atomic_peak(peak_buffered_samples_, depth);
}

bool RenderBridge::try_push(const PcmFrame& frame) noexcept {
    if (!ring_.write_exact(std::span<const float>(frame))) {
        push_rejections_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    frames_enqueued_.fetch_add(1, std::memory_order_relaxed);
    update_peak(ring_.size());
    return true;
}

void RenderBridge::on_render(std::span<float> frames) noexcept {
    callbacks_.fetch_add(1, std::memory_order_relaxed);
    requested_samples_.fetch_add(frames.size(), std::memory_order_relaxed);
    if (frames.empty()) {
        return;
    }
    const auto read = ring_.read(frames);
    pcm_samples_rendered_.fetch_add(read, std::memory_order_relaxed);
    if (read < frames.size()) {
        std::fill(frames.begin() + static_cast<std::ptrdiff_t>(read), frames.end(), 0.0F);
        underrun_callbacks_.fetch_add(1, std::memory_order_relaxed);
        silence_samples_rendered_.fetch_add(frames.size() - read, std::memory_order_relaxed);
    }
}

RenderBridgeStatistics RenderBridge::statistics() const noexcept {
    return {
        .callbacks = callbacks_.load(std::memory_order_relaxed),
        .requested_samples = requested_samples_.load(std::memory_order_relaxed),
        .pcm_samples_rendered = pcm_samples_rendered_.load(std::memory_order_relaxed),
        .silence_samples_rendered = silence_samples_rendered_.load(std::memory_order_relaxed),
        .underrun_callbacks = underrun_callbacks_.load(std::memory_order_relaxed),
        .frames_enqueued = frames_enqueued_.load(std::memory_order_relaxed),
        .push_rejections = push_rejections_.load(std::memory_order_relaxed),
        .buffered_samples = ring_.size(),
        .peak_buffered_samples = peak_buffered_samples_.load(std::memory_order_relaxed),
    };
}

} // namespace catro::voice
