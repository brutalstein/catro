#include <catro/voice/jitter.hpp>

#include <algorithm>
#include <limits>

namespace catro::voice {
namespace {

constexpr std::int64_t kWindow = static_cast<std::int64_t>(kJitterCapacityPackets);

} // namespace

JitterBuffer::JitterBuffer(std::uint16_t target_packets) noexcept
    : target_packets_(std::clamp<std::uint16_t>(
          target_packets, 1U, static_cast<std::uint16_t>(kJitterCapacityPackets - 1U))) {}

std::size_t JitterBuffer::index_for(std::int64_t extended_sequence) noexcept {
    auto value = extended_sequence % static_cast<std::int64_t>(kJitterCapacityPackets);
    if (value < 0) {
        value += static_cast<std::int64_t>(kJitterCapacityPackets);
    }
    return static_cast<std::size_t>(value);
}

std::int64_t JitterBuffer::unwrap_near(std::uint16_t sequence) const noexcept {
    const auto reference = static_cast<std::uint16_t>(highest_seen_);
    return highest_seen_ + static_cast<std::int64_t>(sequence_distance(sequence, reference));
}

JitterBuffer::Slot* JitterBuffer::find(std::int64_t extended_sequence) noexcept {
    auto& slot = slots_[index_for(extended_sequence)];
    return slot.occupied && slot.extended_sequence == extended_sequence ? &slot : nullptr;
}

const JitterBuffer::Slot* JitterBuffer::find(std::int64_t extended_sequence) const noexcept {
    const auto& slot = slots_[index_for(extended_sequence)];
    return slot.occupied && slot.extended_sequence == extended_sequence ? &slot : nullptr;
}

std::int64_t JitterBuffer::earliest_sequence() const noexcept {
    auto earliest = std::numeric_limits<std::int64_t>::max();
    for (const auto& slot : slots_) {
        if (slot.occupied) {
            earliest = std::min(earliest, slot.extended_sequence);
        }
    }
    return earliest;
}

void JitterBuffer::copy_payload(const Slot& slot, PlayoutFrame& frame) const noexcept {
    frame.payload_sequence = slot.sequence;
    frame.payload_size = slot.payload_size;
    std::copy_n(slot.payload.begin(), slot.payload_size, frame.payload.begin());
}

void JitterBuffer::erase(Slot& slot) noexcept {
    slot.occupied = false;
    slot.payload_size = 0;
    const auto buffered = buffered_.load(std::memory_order_relaxed);
    if (buffered > 0) {
        buffered_.store(buffered - 1, std::memory_order_relaxed);
    }
}

void JitterBuffer::advance_playout() noexcept {
    ++next_sequence_;
    next_timestamp_ += kFrameSamples;
}

JitterPushResult JitterBuffer::push(const VoicePacketView& packet) noexcept {
    if (packet.payload.empty() || packet.payload.size() > kVoiceMaxPayloadBytes) {
        return JitterPushResult::invalid_payload;
    }
    if (stream_set_ && packet.stream_id != stream_id_) {
        wrong_stream_.fetch_add(1, std::memory_order_relaxed);
        return JitterPushResult::wrong_stream;
    }

    std::int64_t extended = static_cast<std::int64_t>(packet.sequence);
    if (have_reference_) {
        extended = unwrap_near(packet.sequence);
    }

    if (started_.load(std::memory_order_relaxed)) {
        if (extended < next_sequence_) {
            late_.fetch_add(1, std::memory_order_relaxed);
            return JitterPushResult::late;
        }
        if (extended - next_sequence_ >= kWindow) {
            outside_window_.fetch_add(1, std::memory_order_relaxed);
            return JitterPushResult::outside_window;
        }
    } else if (have_reference_) {
        const auto distance = extended - highest_seen_;
        if (distance <= -kWindow || distance >= kWindow) {
            outside_window_.fetch_add(1, std::memory_order_relaxed);
            return JitterPushResult::outside_window;
        }
    }

    auto& slot = slots_[index_for(extended)];
    if (slot.occupied) {
        if (slot.extended_sequence == extended) {
            duplicates_.fetch_add(1, std::memory_order_relaxed);
            return JitterPushResult::duplicate;
        }
        outside_window_.fetch_add(1, std::memory_order_relaxed);
        return JitterPushResult::outside_window;
    }

    const bool reordered = have_reference_ && extended < highest_seen_;
    slot.occupied = true;
    slot.extended_sequence = extended;
    slot.sequence = packet.sequence;
    slot.timestamp = packet.timestamp;
    slot.payload_size = static_cast<std::uint16_t>(packet.payload.size());
    std::copy(packet.payload.begin(), packet.payload.end(), slot.payload.begin());

    if (!stream_set_) {
        stream_set_ = true;
        stream_id_ = packet.stream_id;
    }
    if (!have_reference_ || extended > highest_seen_) {
        highest_seen_ = extended;
        have_reference_ = true;
    }
    const auto buffered = buffered_.fetch_add(1, std::memory_order_relaxed) + 1;
    accepted_.fetch_add(1, std::memory_order_relaxed);
    if (reordered) {
        reordered_.fetch_add(1, std::memory_order_relaxed);
    }
    auto peak = peak_buffered_.load(std::memory_order_relaxed);
    while (buffered > peak &&
           !peak_buffered_.compare_exchange_weak(peak, buffered, std::memory_order_relaxed,
                                                 std::memory_order_relaxed)) {
    }
    return JitterPushResult::accepted;
}

PlayoutKind JitterBuffer::peek() const noexcept {
    if (!started_.load(std::memory_order_relaxed)) {
        if (buffered_.load(std::memory_order_relaxed) < target_packets_) {
            return PlayoutKind::waiting;
        }
        const auto first_sequence = earliest_sequence();
        const auto* first = find(first_sequence);
        return first == nullptr ? PlayoutKind::waiting : PlayoutKind::packet;
    }

    const auto* current = find(next_sequence_);
    if (current != nullptr && current->timestamp == next_timestamp_) {
        return PlayoutKind::packet;
    }

    const auto* following = find(next_sequence_ + 1);
    const auto following_timestamp = next_timestamp_ + kFrameSamples;
    if (following != nullptr && following->timestamp == following_timestamp) {
        return PlayoutKind::fec;
    }
    return PlayoutKind::plc;
}

PlayoutKind JitterBuffer::pull(PlayoutFrame& frame) noexcept {
    frame.kind = PlayoutKind::waiting;
    frame.stream_id = stream_id_;
    frame.sequence = 0;
    frame.payload_sequence = 0;
    frame.timestamp = 0;
    frame.payload_size = 0;

    if (!started_.load(std::memory_order_relaxed)) {
        if (buffered_.load(std::memory_order_relaxed) < target_packets_) {
            return frame.kind;
        }
        next_sequence_ = earliest_sequence();
        auto* first = find(next_sequence_);
        if (first == nullptr) {
            return frame.kind;
        }
        next_timestamp_ = first->timestamp;
        started_.store(true, std::memory_order_relaxed);
    }

    frame.stream_id = stream_id_;
    frame.sequence = static_cast<std::uint16_t>(next_sequence_);
    frame.timestamp = next_timestamp_;

    auto* current = find(next_sequence_);
    if (current != nullptr && current->timestamp == next_timestamp_) {
        frame.kind = PlayoutKind::packet;
        copy_payload(*current, frame);
        erase(*current);
        played_.fetch_add(1, std::memory_order_relaxed);
        advance_playout();
        return frame.kind;
    }

    if (current != nullptr) {
        erase(*current);
        timestamp_mismatches_.fetch_add(1, std::memory_order_relaxed);
    }

    const auto* following = find(next_sequence_ + 1);
    const auto following_timestamp = next_timestamp_ + kFrameSamples;
    if (following != nullptr && following->timestamp == following_timestamp) {
        frame.kind = PlayoutKind::fec;
        copy_payload(*following, frame);
        fec_.fetch_add(1, std::memory_order_relaxed);
    } else {
        frame.kind = PlayoutKind::plc;
        plc_.fetch_add(1, std::memory_order_relaxed);
    }

    advance_playout();
    return frame.kind;
}

void JitterBuffer::resynchronize() noexcept {
    const auto discarded = buffered_.exchange(0, std::memory_order_relaxed);
    for (auto& slot : slots_) {
        slot.occupied = false;
        slot.payload_size = 0;
    }
    stream_set_ = false;
    stream_id_ = 0;
    have_reference_ = false;
    highest_seen_ = 0;
    started_.store(false, std::memory_order_relaxed);
    next_sequence_ = 0;
    next_timestamp_ = 0;
    resyncs_.fetch_add(1, std::memory_order_relaxed);
    resync_discarded_packets_.fetch_add(discarded, std::memory_order_relaxed);
}

void JitterBuffer::reset() noexcept {
    resynchronize();
    accepted_.store(0, std::memory_order_relaxed);
    duplicates_.store(0, std::memory_order_relaxed);
    late_.store(0, std::memory_order_relaxed);
    reordered_.store(0, std::memory_order_relaxed);
    outside_window_.store(0, std::memory_order_relaxed);
    wrong_stream_.store(0, std::memory_order_relaxed);
    timestamp_mismatches_.store(0, std::memory_order_relaxed);
    played_.store(0, std::memory_order_relaxed);
    fec_.store(0, std::memory_order_relaxed);
    plc_.store(0, std::memory_order_relaxed);
    resyncs_.store(0, std::memory_order_relaxed);
    resync_discarded_packets_.store(0, std::memory_order_relaxed);
    peak_buffered_.store(0, std::memory_order_relaxed);
}

JitterStatistics JitterBuffer::statistics() const noexcept {
    return {
        .accepted = accepted_.load(std::memory_order_relaxed),
        .duplicates = duplicates_.load(std::memory_order_relaxed),
        .late = late_.load(std::memory_order_relaxed),
        .reordered = reordered_.load(std::memory_order_relaxed),
        .outside_window = outside_window_.load(std::memory_order_relaxed),
        .wrong_stream = wrong_stream_.load(std::memory_order_relaxed),
        .timestamp_mismatches = timestamp_mismatches_.load(std::memory_order_relaxed),
        .played = played_.load(std::memory_order_relaxed),
        .fec = fec_.load(std::memory_order_relaxed),
        .plc = plc_.load(std::memory_order_relaxed),
        .resyncs = resyncs_.load(std::memory_order_relaxed),
        .resync_discarded_packets = resync_discarded_packets_.load(std::memory_order_relaxed),
        .buffered = buffered_.load(std::memory_order_relaxed),
        .peak_buffered = peak_buffered_.load(std::memory_order_relaxed),
    };
}

} // namespace catro::voice
