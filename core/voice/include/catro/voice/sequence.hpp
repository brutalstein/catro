#pragma once

#include <cstdint>

namespace catro::voice {

// Signed modular distance from reference to candidate in the 16-bit sequence space.
// The half-range rule makes comparisons unambiguous for every bounded Catro reorder window.
[[nodiscard]] constexpr std::int32_t sequence_distance(std::uint16_t candidate,
                                                        std::uint16_t reference) noexcept {
    const auto raw = static_cast<std::uint16_t>(candidate - reference);
    return raw <= 0x7fffU ? static_cast<std::int32_t>(raw)
                          : static_cast<std::int32_t>(raw) - 0x10000;
}

[[nodiscard]] constexpr bool sequence_ahead(std::uint16_t candidate,
                                             std::uint16_t reference) noexcept {
    return sequence_distance(candidate, reference) > 0;
}

} // namespace catro::voice
