#include <catro/voice/stream_controls.hpp>

#include <algorithm>
#include <bit>
#include <cmath>

namespace catro::voice {
namespace {

[[nodiscard]] int hex_digit(char value) noexcept {
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

[[nodiscard]] std::uint32_t slot_stream(std::uint64_t slot) noexcept {
    return static_cast<std::uint32_t>(slot >> 32U);
}

} // namespace

std::uint32_t user_stream_id(std::string_view user_id_hex) noexcept {
    std::uint32_t value = 0x4354524fU; // "CTRO"
    for (std::size_t index = 0; index < 4; ++index) {
        const auto high = index * 2 < user_id_hex.size() ? hex_digit(user_id_hex[index * 2]) : -1;
        const auto low = index * 2 + 1 < user_id_hex.size() ? hex_digit(user_id_hex[index * 2 + 1]) : -1;
        const auto byte = high < 0 || low < 0 ? 0U : static_cast<std::uint32_t>(high * 16 + low);
        value = (value << 5U) ^ (value >> 27U) ^ byte;
    }
    return value == 0 ? 1U : value;
}

void StreamControls::set_volume(std::uint32_t stream_id, float volume) noexcept {
    if (stream_id == 0 || !std::isfinite(volume)) {
        return;
    }
    volume = std::clamp(volume, 0.0F, kMaxStreamVolume);
    const auto packed = (static_cast<std::uint64_t>(stream_id) << 32U) | std::bit_cast<std::uint32_t>(volume);
    std::atomic<std::uint64_t>* free_slot = nullptr;
    for (auto& slot : volumes_) {
        const auto current = slot.load(std::memory_order_relaxed);
        if (slot_stream(current) == stream_id) {
            // Unity volume needs no slot; freeing it keeps the table small for the mixer.
            slot.store(volume == 1.0F ? 0U : packed, std::memory_order_relaxed);
            return;
        }
        if (current == 0 && free_slot == nullptr) {
            free_slot = &slot;
        }
    }
    if (free_slot != nullptr && volume != 1.0F) {
        free_slot->store(packed, std::memory_order_relaxed);
    }
}

float StreamControls::volume(std::uint32_t stream_id) const noexcept {
    for (const auto& slot : volumes_) {
        const auto current = slot.load(std::memory_order_relaxed);
        if (current != 0 && slot_stream(current) == stream_id) {
            return std::bit_cast<float>(static_cast<std::uint32_t>(current));
        }
    }
    return 1.0F;
}

void StreamControls::set_speaking(std::uint32_t stream_id, bool speaking) noexcept {
    if (stream_id == 0) {
        return;
    }
    std::atomic<std::uint32_t>* free_slot = nullptr;
    for (auto& slot : speaking_) {
        const auto current = slot.load(std::memory_order_relaxed);
        if (current == stream_id) {
            if (!speaking) {
                slot.store(0, std::memory_order_relaxed);
            }
            return;
        }
        if (current == 0 && free_slot == nullptr) {
            free_slot = &slot;
        }
    }
    if (speaking && free_slot != nullptr) {
        free_slot->store(stream_id, std::memory_order_relaxed);
    }
}

std::size_t StreamControls::speaking(std::span<std::uint32_t> out) const noexcept {
    std::size_t count = 0;
    for (const auto& slot : speaking_) {
        const auto current = slot.load(std::memory_order_relaxed);
        if (current != 0 && count < out.size()) {
            out[count++] = current;
        }
    }
    return count;
}

void StreamControls::add_echo_reference(std::span<const float> stereo) noexcept {
    const auto frames = stereo.size() / 2;
    std::scoped_lock lock(echo_writer_);
    const auto head = echo_head_.load(std::memory_order_relaxed);
    const auto tail = echo_tail_.load(std::memory_order_acquire);
    if (head - tail + frames > kEchoReferenceCapacity) {
        return;
    }
    for (std::size_t frame = 0; frame < frames; ++frame) {
        echo_ring_[(head + frame) % kEchoReferenceCapacity] =
            0.5F * (stereo[2 * frame] + stereo[2 * frame + 1]);
    }
    echo_head_.store(head + frames, std::memory_order_release);
}

void StreamControls::mix_echo_reference(std::span<float> frame) noexcept {
    const auto head = echo_head_.load(std::memory_order_acquire);
    auto tail = echo_tail_.load(std::memory_order_relaxed);
    if (head - tail > 3 * frame.size()) {
        tail = head - frame.size();
    }
    const auto count = std::min(head - tail, frame.size());
    for (std::size_t sample = 0; sample < count; ++sample) {
        frame[sample] = std::clamp(
            frame[sample] + echo_ring_[(tail + sample) % kEchoReferenceCapacity], -1.0F, 1.0F);
    }
    echo_tail_.store(tail + count, std::memory_order_release);
}

} // namespace catro::voice
