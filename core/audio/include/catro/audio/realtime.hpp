#pragma once

#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <numbers>
#include <span>
#include <type_traits>
#include <vector>

// Primitives that run on platform audio threads. Every operation marked noexcept here is
// real-time safe: no allocation, no lock, no system call, bounded time.
namespace catro::audio {

// Engine frames: 48 kHz, 32-bit float, mono. The OS converts at the device boundary.
inline constexpr std::uint32_t kSampleRate = 48000;

// Wait-free single-producer single-consumer ring. Indices grow monotonically; the capacity is a
// power of two so positions wrap with a mask.
#if defined(_MSC_VER)
#pragma warning(push)
// Intentional over-alignment isolates the audio and worker-owned indices. MSVC warning C4324
// otherwise turns that deliberate padding into an error under /W4 /WX.
#pragma warning(disable : 4324)
#endif
template <class T>
    requires std::is_trivially_copyable_v<T>
class SpscRing {
public:
    explicit SpscRing(std::size_t minimum_capacity)
        : buffer_(std::bit_ceil(std::max<std::size_t>(minimum_capacity, 2))), mask_(buffer_.size() - 1) {}

    SpscRing(const SpscRing&) = delete;
    SpscRing& operator=(const SpscRing&) = delete;

    [[nodiscard]] std::size_t capacity() const noexcept { return buffer_.size(); }

    // Either side; exact for the calling side, a lower or upper bound for the other.
    [[nodiscard]] std::size_t size() const noexcept {
        const auto head = head_.value.load(std::memory_order_acquire);
        const auto tail = tail_.value.load(std::memory_order_acquire);
        // A third-party diagnostics thread can observe the two independent atomics at slightly
        // different logical instants. Clamp that non-coherent snapshot instead of exposing an
        // underflow or a value larger than the bounded ring.
        if (head < tail) {
            return 0;
        }
        return std::min(head - tail, capacity());
    }

    // Producer only. Writes what fits and returns the count written.
    std::size_t write(std::span<const T> items) noexcept {
        const auto head = head_.value.load(std::memory_order_relaxed);
        const auto tail = tail_.value.load(std::memory_order_acquire);
        const auto count = std::min(items.size(), capacity() - (head - tail));
        copy_into(head, items.first(count));
        head_.value.store(head + count, std::memory_order_release);
        return count;
    }

    // Producer only. Commits the whole block or nothing. Useful when a partial write would
    // destroy a higher-level frame boundary.
    bool write_exact(std::span<const T> items) noexcept {
        const auto head = head_.value.load(std::memory_order_relaxed);
        const auto tail = tail_.value.load(std::memory_order_acquire);
        if (items.size() > capacity() - (head - tail)) {
            return false;
        }
        copy_into(head, items);
        head_.value.store(head + items.size(), std::memory_order_release);
        return true;
    }

    // Consumer only. Reads what is available and returns the count read.
    std::size_t read(std::span<T> out) noexcept {
        const auto tail = tail_.value.load(std::memory_order_relaxed);
        const auto head = head_.value.load(std::memory_order_acquire);
        const auto count = std::min(out.size(), head - tail);
        copy_out(tail, out.first(count));
        tail_.value.store(tail + count, std::memory_order_release);
        return count;
    }

    // Consumer only. Removes the whole requested block or nothing.
    bool read_exact(std::span<T> out) noexcept {
        const auto tail = tail_.value.load(std::memory_order_relaxed);
        const auto head = head_.value.load(std::memory_order_acquire);
        if (out.size() > head - tail) {
            return false;
        }
        copy_out(tail, out);
        tail_.value.store(tail + out.size(), std::memory_order_release);
        return true;
    }

    // Consumer only. Drops up to `count` of the oldest items and returns the count dropped.
    std::size_t discard(std::size_t count) noexcept {
        const auto tail = tail_.value.load(std::memory_order_relaxed);
        const auto head = head_.value.load(std::memory_order_acquire);
        const auto dropped = std::min(count, head - tail);
        tail_.value.store(tail + dropped, std::memory_order_release);
        return dropped;
    }

private:
    void copy_into(std::size_t position, std::span<const T> items) noexcept {
        if (items.empty()) {
            return;
        }
        const auto offset = position & mask_;
        const auto first = std::min(items.size(), capacity() - offset);
        std::memcpy(buffer_.data() + offset, items.data(), first * sizeof(T));
        const auto remaining = items.size() - first;
        if (remaining > 0) {
            std::memcpy(buffer_.data(), items.data() + first, remaining * sizeof(T));
        }
    }

    void copy_out(std::size_t position, std::span<T> out) noexcept {
        if (out.empty()) {
            return;
        }
        const auto offset = position & mask_;
        const auto first = std::min(out.size(), capacity() - offset);
        std::memcpy(out.data(), buffer_.data() + offset, first * sizeof(T));
        const auto remaining = out.size() - first;
        if (remaining > 0) {
            std::memcpy(out.data() + first, buffer_.data(), remaining * sizeof(T));
        }
    }

    // Keep producer and consumer ownership on distinct cache lines. 128 bytes is conservative
    // across the x86 machines and Apple Silicon machines Catro targets; this costs only 256 bytes
    // per ring and avoids callback/worker cache-line ping-pong.
    static_assert(sizeof(std::atomic<std::size_t>) <= 128);
    struct alignas(128) Index {
        std::atomic<std::size_t> value{0};
        char padding[128 - sizeof(std::atomic<std::size_t>)]{};
    };
    static_assert(sizeof(Index) == 128);
    static_assert(alignof(Index) >= 128);

    std::vector<T> buffer_;
    std::size_t mask_;
    Index head_;
    Index tail_;
};
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

// Peak and RMS of the most recent block, written by the audio thread and read by any thread.
// ponytail: last-block values; a UI polling slower than the block rate can miss a short peak.
class LevelMeter {
public:
    void process(std::span<const float> block) noexcept {
        float peak = 0.0F;
        double energy = 0.0;
        for (const auto sample : block) {
            peak = std::max(peak, std::abs(sample));
            energy += static_cast<double>(sample) * sample;
        }
        const auto rms = block.empty() ? 0.0F : static_cast<float>(std::sqrt(energy / static_cast<double>(block.size())));
        peak_.store(peak, std::memory_order_relaxed);
        rms_.store(rms, std::memory_order_relaxed);
    }

    void reset() noexcept {
        peak_.store(0.0F, std::memory_order_relaxed);
        rms_.store(0.0F, std::memory_order_relaxed);
    }

    [[nodiscard]] float peak() const noexcept { return peak_.load(std::memory_order_relaxed); }
    [[nodiscard]] float rms() const noexcept { return rms_.load(std::memory_order_relaxed); }

    // Full scale is 0 dBFS; silence is clamped to the floor.
    [[nodiscard]] static float to_dbfs(float linear) noexcept {
        constexpr float kFloor = -120.0F;
        return linear <= 0.0F ? kFloor : std::max(kFloor, 20.0F * std::log10(linear));
    }

private:
    std::atomic<float> peak_{0.0F};
    std::atomic<float> rms_{0.0F};
};

// Phase-continuous sine for the output test.
class ToneGenerator {
public:
    ToneGenerator(double frequency_hz, float amplitude) noexcept
        : step_(frequency_hz / kSampleRate), amplitude_(amplitude) {}

    void fill(std::span<float> out) noexcept {
        for (auto& sample : out) {
            sample = amplitude_ * static_cast<float>(std::sin(2.0 * std::numbers::pi * phase_));
            phase_ += step_;
            phase_ -= std::floor(phase_);
        }
    }

private:
    double phase_ = 0.0;
    double step_;
    float amplitude_;
};

} // namespace catro::audio
