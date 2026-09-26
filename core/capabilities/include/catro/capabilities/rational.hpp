#pragma once

#include <compare>
#include <cstdint>
#include <numeric>

namespace catro::capabilities {

// Exact non-negative ratio for refresh rates, frame rates, and scale factors.
// Values are reduced on construction; 60000/1001 stays 60000/1001 and is never rounded to 60.
// A zero denominator is representable so untrusted input can reach validation, but it is invalid.
class Rational {
public:
    constexpr Rational() noexcept = default;

    constexpr Rational(std::uint32_t numerator, std::uint32_t denominator) noexcept
        : numerator_(numerator), denominator_(denominator) {
        if (denominator_ != 0) {
            const auto divisor = std::gcd(numerator_, denominator_);
            numerator_ /= divisor;
            denominator_ /= divisor;
        }
    }

    [[nodiscard]] constexpr std::uint32_t numerator() const noexcept { return numerator_; }
    [[nodiscard]] constexpr std::uint32_t denominator() const noexcept { return denominator_; }
    [[nodiscard]] constexpr bool valid() const noexcept { return denominator_ != 0; }

    friend constexpr bool operator==(const Rational&, const Rational&) = default;

    // Exact cross-multiplication; 32-bit terms cannot overflow the 64-bit products.
    // Ordering is meaningful only between valid values.
    friend constexpr std::strong_ordering operator<=>(const Rational& lhs, const Rational& rhs) noexcept {
        return std::uint64_t{lhs.numerator_} * rhs.denominator_ <=>
               std::uint64_t{rhs.numerator_} * lhs.denominator_;
    }

private:
    std::uint32_t numerator_ = 0;
    std::uint32_t denominator_ = 1;
};

} // namespace catro::capabilities
