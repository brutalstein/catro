#pragma once

#include <catro/audio/realtime.hpp>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <span>

namespace catro::audio {

// Carries captured frames to the render thread. The two device clocks are independent, so the
// render side keeps the fill near a target: it plays only once the target is buffered, re-primes
// after running dry, and drops the oldest frames when capture runs ahead past the high-water mark.
class MonitorPipe {
public:
    MonitorPipe(std::uint32_t target_frames, std::uint32_t high_water_frames)
        : ring_(std::size_t{high_water_frames} * 2), target_(target_frames),
          high_water_(std::max(high_water_frames, target_frames)) {}

    // Capture thread.
    void push(std::span<const float> frames) noexcept {
        if (ring_.write(frames) < frames.size()) {
            overruns_.fetch_add(1, std::memory_order_relaxed);
        }
    }

    // Render thread. Always fills `out` completely, with silence where no frames are available.
    void pull(std::span<float> out) noexcept {
        const auto available = ring_.size();
        if (available > high_water_) {
            ring_.discard(available - target_);
            drift_corrections_.fetch_add(1, std::memory_order_relaxed);
        }
        if (!primed_) {
            if (ring_.size() < target_) {
                std::ranges::fill(out, 0.0F);
                return;
            }
            primed_ = true;
        }
        const auto read = ring_.read(out);
        if (read < out.size()) {
            std::ranges::fill(out.subspan(read), 0.0F);
            underruns_.fetch_add(1, std::memory_order_relaxed);
            primed_ = false;
        }
    }

    [[nodiscard]] std::uint32_t target_frames() const noexcept { return target_; }
    [[nodiscard]] std::uint64_t underruns() const noexcept { return underruns_.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint64_t overruns() const noexcept { return overruns_.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint64_t drift_corrections() const noexcept {
        return drift_corrections_.load(std::memory_order_relaxed);
    }

private:
    SpscRing<float> ring_;
    std::uint32_t target_;
    std::uint32_t high_water_;
    // Render thread only.
    bool primed_ = false;
    std::atomic<std::uint64_t> underruns_{0};
    std::atomic<std::uint64_t> overruns_{0};
    std::atomic<std::uint64_t> drift_corrections_{0};
};

} // namespace catro::audio
