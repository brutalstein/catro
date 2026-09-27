#pragma once

#include <catro/voice/codec.hpp>
#include <catro/voice/packet.hpp>
#include <catro/voice/sequence.hpp>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace catro::voice {

inline constexpr std::size_t kJitterCapacityPackets = 32;
inline constexpr std::uint16_t kDefaultJitterTargetPackets = 3;

enum class JitterPushResult {
    accepted,
    duplicate,
    late,
    outside_window,
    wrong_stream,
    invalid_payload,
};

[[nodiscard]] constexpr std::string_view name(JitterPushResult result) noexcept {
    switch (result) {
    case JitterPushResult::accepted:
        return "accepted";
    case JitterPushResult::duplicate:
        return "duplicate";
    case JitterPushResult::late:
        return "late";
    case JitterPushResult::outside_window:
        return "outside window";
    case JitterPushResult::wrong_stream:
        return "wrong stream";
    case JitterPushResult::invalid_payload:
        return "invalid payload";
    }
    return "jitter push error";
}

enum class PlayoutKind {
    waiting,
    packet,
    fec,
    plc,
};

[[nodiscard]] constexpr std::string_view name(PlayoutKind kind) noexcept {
    switch (kind) {
    case PlayoutKind::waiting:
        return "waiting";
    case PlayoutKind::packet:
        return "packet";
    case PlayoutKind::fec:
        return "fec";
    case PlayoutKind::plc:
        return "plc";
    }
    return "playout";
}

// A self-contained worker-thread playout decision. For FEC, payload_sequence is the following
// packet whose in-band FEC should be decoded with decode_fec=true. PLC has no payload.
struct PlayoutFrame {
    PlayoutKind kind = PlayoutKind::waiting;
    std::uint32_t stream_id = 0;
    std::uint16_t sequence = 0;
    std::uint16_t payload_sequence = 0;
    std::uint32_t timestamp = 0;
    std::uint16_t payload_size = 0;
    std::array<std::byte, kVoiceMaxPayloadBytes> payload{};

    [[nodiscard]] std::span<const std::byte> payload_view() const noexcept {
        return std::span<const std::byte>(payload).first(payload_size);
    }
};

struct JitterStatistics {
    std::uint64_t accepted = 0;
    std::uint64_t duplicates = 0;
    std::uint64_t late = 0;
    std::uint64_t reordered = 0;
    std::uint64_t outside_window = 0;
    std::uint64_t wrong_stream = 0;
    std::uint64_t timestamp_mismatches = 0;
    std::uint64_t played = 0;
    std::uint64_t fec = 0;
    std::uint64_t plc = 0;
    std::size_t buffered = 0;
    std::size_t peak_buffered = 0;

    friend bool operator==(const JitterStatistics&, const JitterStatistics&) = default;
};

// Fixed-storage single-thread jitter/reorder buffer. It is intended for the receive/decode worker,
// never the audio callback. No push/pull allocates and memory cannot grow with hostile traffic.
class JitterBuffer {
public:
    explicit JitterBuffer(std::uint16_t target_packets = kDefaultJitterTargetPackets) noexcept;

    [[nodiscard]] JitterPushResult push(const VoicePacketView& packet) noexcept;
    // Worker-side non-mutating preview used to avoid performing PLC before its playout deadline.
    [[nodiscard]] PlayoutKind peek() const noexcept;
    [[nodiscard]] PlayoutKind pull(PlayoutFrame& frame) noexcept;
    void reset() noexcept;

    [[nodiscard]] JitterStatistics statistics() const noexcept;
    [[nodiscard]] bool started() const noexcept { return started_.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint16_t target_packets() const noexcept { return target_packets_; }

private:
    struct Slot {
        bool occupied = false;
        std::int64_t extended_sequence = 0;
        std::uint16_t sequence = 0;
        std::uint32_t timestamp = 0;
        std::uint16_t payload_size = 0;
        std::array<std::byte, kVoiceMaxPayloadBytes> payload{};
    };

    [[nodiscard]] static std::size_t index_for(std::int64_t extended_sequence) noexcept;
    [[nodiscard]] std::int64_t unwrap_near(std::uint16_t sequence) const noexcept;
    [[nodiscard]] Slot* find(std::int64_t extended_sequence) noexcept;
    [[nodiscard]] const Slot* find(std::int64_t extended_sequence) const noexcept;
    [[nodiscard]] std::int64_t earliest_sequence() const noexcept;
    void copy_payload(const Slot& slot, PlayoutFrame& frame) const noexcept;
    void erase(Slot& slot) noexcept;
    void advance_playout() noexcept;

    std::array<Slot, kJitterCapacityPackets> slots_{};
    std::uint16_t target_packets_ = kDefaultJitterTargetPackets;
    bool stream_set_ = false;
    std::uint32_t stream_id_ = 0;
    bool have_reference_ = false;
    std::int64_t highest_seen_ = 0;
    std::atomic_bool started_{false};
    std::int64_t next_sequence_ = 0;
    std::uint32_t next_timestamp_ = 0;
    std::atomic<std::size_t> buffered_{0};
    std::atomic<std::uint64_t> accepted_{0};
    std::atomic<std::uint64_t> duplicates_{0};
    std::atomic<std::uint64_t> late_{0};
    std::atomic<std::uint64_t> reordered_{0};
    std::atomic<std::uint64_t> outside_window_{0};
    std::atomic<std::uint64_t> wrong_stream_{0};
    std::atomic<std::uint64_t> timestamp_mismatches_{0};
    std::atomic<std::uint64_t> played_{0};
    std::atomic<std::uint64_t> fec_{0};
    std::atomic<std::uint64_t> plc_{0};
    std::atomic<std::size_t> peak_buffered_{0};
};

} // namespace catro::voice
