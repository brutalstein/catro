#pragma once

#include <catro/audio/engine.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <numbers>
#include <optional>
#include <thread>

namespace catro::test {

// Paced 10 ms fake audio devices: capture produces a 300 Hz sine of the given amplitude and render
// counts audible samples it pulls. Threads stop and join before a stream is destroyed.
class FakeCaptureStream final : public audio::AudioStream {
public:
    FakeCaptureStream(audio::CaptureSink& sink, float sample)
        : sink_(sink), sample_(sample),
          info_{{"fake-in", capabilities::IdentityScope::persistent}, 48000, 1, 480, 0} {}

    ~FakeCaptureStream() override {
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
            using namespace std::chrono_literals;
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

class FakeRenderStream final : public audio::AudioStream {
public:
    FakeRenderStream(audio::RenderSource& source, std::atomic<std::uint64_t>& nonzero_samples)
        : source_(source), nonzero_samples_(nonzero_samples),
          info_{{"fake-out", capabilities::IdentityScope::persistent}, 48000, 2, 480, 0} {}

    ~FakeRenderStream() override {
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
            using namespace std::chrono_literals;
            std::array<float, 480> frames{};
            auto next = std::chrono::steady_clock::now();
            while (!stop_.load(std::memory_order_acquire)) {
                source_.on_render(frames);
                std::uint64_t audible = 0;
                for (const auto sample : frames) {
                    // -60 dBFS: Opus can decode digital silence to tiny non-zero residue (seen on
                    // arm64), which is not audible and must not count as leaked audio.
                    audible += std::abs(sample) > 1e-3F ? 1U : 0U;
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
        if (capture_error) {
            return *capture_error;
        }
        return std::unique_ptr<audio::AudioStream>(
            std::make_unique<FakeCaptureStream>(sink, capture_sample_));
    }

    audio::OpenResult open_render(const std::optional<capabilities::AudioEndpointId>&,
                                  audio::RenderSource& source,
                                  audio::StreamFailure) override {
        return std::unique_ptr<audio::AudioStream>(
            std::make_unique<FakeRenderStream>(source, rendered_nonzero_samples));
    }

    // Set before start to make capture open fail like a missing or denied device.
    std::optional<audio::AudioError> capture_error;
    std::atomic<std::uint64_t> rendered_nonzero_samples{0};

private:
    float capture_sample_;
};

} // namespace catro::test
