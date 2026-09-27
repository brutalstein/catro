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
        pending_gap_samples_.fetch_add(frames.size(), std::memory_order_relaxed);
        // Never splice pre-gap and post-gap PCM into one Opus frame. The worker will flush the
        // stale queued prefix at its next opportunity and restart framing on fresh capture data.
        resync_requested_.store(true, std::memory_order_release);
        return;
    }
    update_peak(ring_.size());
}

bool CaptureBridge::resynchronize_if_needed() noexcept {
    if (!resync_requested_.exchange(false, std::memory_order_acq_rel)) {
        return false;
    }
    const auto discarded = ring_.discard(ring_.size());
    const auto dropped = pending_gap_samples_.exchange(0, std::memory_order_acq_rel);
    const auto lost_samples = static_cast<std::uint64_t>(discarded) + dropped;
    const auto remainder =
        static_cast<std::size_t>(lost_samples % static_cast<std::uint64_t>(kFrameSamples));
    alignment_samples_pending_ =
        remainder == 0 ? 0 : static_cast<std::size_t>(kFrameSamples) - remainder;
    const auto skipped_samples =
        lost_samples + static_cast<std::uint64_t>(alignment_samples_pending_);
    const auto skipped_frames = skipped_samples / static_cast<std::uint64_t>(kFrameSamples);

    resync_events_.fetch_add(1, std::memory_order_relaxed);
    resync_samples_.fetch_add(discarded, std::memory_order_relaxed);
    timeline_frames_skipped_.fetch_add(skipped_frames, std::memory_order_relaxed);
    unreported_timeline_frames_ += skipped_frames;
    return true;
}

bool CaptureBridge::finish_alignment_discard() noexcept {
    if (alignment_samples_pending_ == 0) {
        return true;
    }
    const auto available = ring_.size();
    if (available == 0) {
        return false;
    }
    const auto requested = std::min(alignment_samples_pending_, available);
    const auto discarded = ring_.discard(requested);
    alignment_samples_pending_ -= discarded;
    resync_samples_.fetch_add(discarded, std::memory_order_relaxed);
    return alignment_samples_pending_ == 0;
}

std::uint64_t CaptureBridge::trim_backlog(std::size_t keep_frames) noexcept {
    (void)resynchronize_if_needed();
    if (!finish_alignment_discard()) {
        const auto skipped = unreported_timeline_frames_;
        unreported_timeline_frames_ = 0;
        return skipped;
    }
    keep_frames = std::clamp<std::size_t>(keep_frames, 1, kMaxRealtimeQueueFrames);
    const auto buffered = ring_.size();
    const auto keep_samples = keep_frames * static_cast<std::size_t>(kFrameSamples);
    if (buffered <= keep_samples) {
        const auto skipped = unreported_timeline_frames_;
        unreported_timeline_frames_ = 0;
        return skipped;
    }

    // Only discard complete 20 ms frames. Any partial native callback remains attached to the
    // same codec-frame phase, so subsequent read_exact() keeps deterministic frame boundaries.
    const auto discard_frames = (buffered - keep_samples) / static_cast<std::size_t>(kFrameSamples);
    if (discard_frames > 0) {
        const auto discard_samples = discard_frames * static_cast<std::size_t>(kFrameSamples);
        const auto discarded = ring_.discard(discard_samples) / static_cast<std::size_t>(kFrameSamples);
        stale_frames_discarded_.fetch_add(discarded, std::memory_order_relaxed);
        timeline_frames_skipped_.fetch_add(discarded, std::memory_order_relaxed);
        unreported_timeline_frames_ += discarded;
    }
    const auto skipped = unreported_timeline_frames_;
    unreported_timeline_frames_ = 0;
    return skipped;
}

bool CaptureBridge::try_pop(PcmFrame& frame) noexcept {
    if (resynchronize_if_needed()) {
        return false;
    }
    if (!finish_alignment_discard()) {
        return false;
    }
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
        .stale_frames_discarded = stale_frames_discarded_.load(std::memory_order_relaxed),
        .resync_events = resync_events_.load(std::memory_order_relaxed),
        .resync_samples = resync_samples_.load(std::memory_order_relaxed),
        .timeline_frames_skipped = timeline_frames_skipped_.load(std::memory_order_relaxed),
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
    primed_.store(true, std::memory_order_relaxed);
    update_peak(ring_.size());
    return true;
}

void RenderBridge::request_resync() noexcept {
    resync_requests_.fetch_add(1, std::memory_order_relaxed);
    resync_requested_.store(true, std::memory_order_release);
}

void RenderBridge::on_render(std::span<float> frames) noexcept {
    callbacks_.fetch_add(1, std::memory_order_relaxed);
    requested_samples_.fetch_add(frames.size(), std::memory_order_relaxed);

    if (resync_requested_.exchange(false, std::memory_order_acq_rel)) {
        // Consumer-owned operation: flush audio that became stale while the render callback was
        // not keeping up. A producer racing after this point sees the newly freed space and its
        // fresh frame is not part of this already-bounded discard request.
        const auto stale = ring_.discard(ring_.size());
        stale_samples_discarded_.fetch_add(stale, std::memory_order_relaxed);
        resync_events_.fetch_add(1, std::memory_order_relaxed);
    }

    if (frames.empty()) {
        return;
    }

    if (deafened_.load(std::memory_order_acquire)) {
        // Keep the consumer clock moving while making the callback output provably silent.
        // This avoids queue growth and means undeafen resumes at live audio rather than replaying
        // a stale backlog. No allocation, lock, or platform call occurs on the audio callback.
        const auto discarded = ring_.discard(frames.size());
        (void)discarded;
        std::fill(frames.begin(), frames.end(), 0.0F);
        silence_samples_rendered_.fetch_add(frames.size(), std::memory_order_relaxed);
        deafened_samples_rendered_.fetch_add(frames.size(), std::memory_order_relaxed);
        return;
    }

    const auto read = ring_.read(frames);
    pcm_samples_rendered_.fetch_add(read, std::memory_order_relaxed);
    if (read < frames.size()) {
        const auto silence = frames.size() - read;
        std::fill(frames.begin() + static_cast<std::ptrdiff_t>(read), frames.end(), 0.0F);
        silence_samples_rendered_.fetch_add(silence, std::memory_order_relaxed);
        if (primed_.load(std::memory_order_relaxed)) {
            underrun_callbacks_.fetch_add(1, std::memory_order_relaxed);
        } else {
            startup_silence_samples_.fetch_add(silence, std::memory_order_relaxed);
        }
    }
}

RenderBridgeStatistics RenderBridge::statistics() const noexcept {
    return {
        .callbacks = callbacks_.load(std::memory_order_relaxed),
        .requested_samples = requested_samples_.load(std::memory_order_relaxed),
        .pcm_samples_rendered = pcm_samples_rendered_.load(std::memory_order_relaxed),
        .silence_samples_rendered = silence_samples_rendered_.load(std::memory_order_relaxed),
        .startup_silence_samples = startup_silence_samples_.load(std::memory_order_relaxed),
        .underrun_callbacks = underrun_callbacks_.load(std::memory_order_relaxed),
        .frames_enqueued = frames_enqueued_.load(std::memory_order_relaxed),
        .push_rejections = push_rejections_.load(std::memory_order_relaxed),
        .resync_requests = resync_requests_.load(std::memory_order_relaxed),
        .resync_events = resync_events_.load(std::memory_order_relaxed),
        .stale_samples_discarded = stale_samples_discarded_.load(std::memory_order_relaxed),
        .deafened_samples_rendered = deafened_samples_rendered_.load(std::memory_order_relaxed),
        .buffered_samples = ring_.size(),
        .peak_buffered_samples = peak_buffered_samples_.load(std::memory_order_relaxed),
    };
}

} // namespace catro::voice
