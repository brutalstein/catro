#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace catro::voice {

inline constexpr std::size_t kMaxControlledStreams = 64;
inline constexpr float kMaxStreamVolume = 2.0F;

// Room voice stream id of a user, from the hex identity id that the directory and the room share.
// Every platform derives it the same way, so a member row can find its speaker without signaling.
[[nodiscard]] std::uint32_t user_stream_id(std::string_view user_id_hex) noexcept;

// Lock-free state shared between the UI and the voice worker: per-user volume (written by the UI,
// read by the mixer) and who is speaking (written by the worker, read by the UI). One writer per
// table; readers never block the realtime path.
class StreamControls {
public:
    // 0 silences, 1 is unchanged, kMaxStreamVolume doubles. Ignored when the table is full.
    void set_volume(std::uint32_t stream_id, float volume) noexcept;
    [[nodiscard]] float volume(std::uint32_t stream_id) const noexcept;

    void set_speaking(std::uint32_t stream_id, bool speaking) noexcept;
    // Copies the remote streams speaking now into out and returns how many were copied.
    [[nodiscard]] std::size_t speaking(std::span<std::uint32_t> out) const noexcept;

    void set_local_speaking(bool speaking) noexcept { local_speaking_.store(speaking, std::memory_order_relaxed); }
    [[nodiscard]] bool local_speaking() const noexcept { return local_speaking_.load(std::memory_order_relaxed); }

private:
    // stream id in the high half, volume float bits in the low half; 0 marks a free slot.
    std::array<std::atomic<std::uint64_t>, kMaxControlledStreams> volumes_{};
    std::array<std::atomic<std::uint32_t>, kMaxControlledStreams> speaking_{};
    std::atomic_bool local_speaking_{false};
};

} // namespace catro::voice
