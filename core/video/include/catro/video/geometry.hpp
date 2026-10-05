#pragma once

#include <cstdint>
#include <optional>

namespace catro::video {

struct VideoExtent {
    std::uint32_t width = 0;
    std::uint32_t height = 0;

    friend bool operator==(const VideoExtent&, const VideoExtent&) = default;
};

namespace detail {

[[nodiscard]] constexpr std::uint32_t min_u32(
    std::uint32_t left, std::uint32_t right) noexcept {
    return left < right ? left : right;
}

[[nodiscard]] constexpr std::uint64_t distance_u64(
    std::uint64_t left, std::uint64_t right) noexcept {
    return left >= right ? left - right : right - left;
}

// Returns the even integer <= max_even whose ratio is closest to numerator/denominator.
// The two adjacent even candidates bracket the optimum, so this remains constant-time.
[[nodiscard]] constexpr std::uint32_t nearest_even_ratio(
    std::uint64_t numerator,
    std::uint32_t denominator,
    std::uint32_t max_even) noexcept {
    if (denominator == 0 || max_even < 2) {
        return 0;
    }

    const auto quotient = numerator / denominator;
    const auto capped = quotient > max_even
        ? max_even
        : static_cast<std::uint32_t>(quotient);
    const auto lower = capped & ~std::uint32_t{1};

    std::uint32_t upper = 0;
    if (lower <= max_even - 2U) {
        upper = lower + 2U;
    }

    if (lower < 2U) {
        return upper;
    }
    if (upper < 2U) {
        return lower;
    }

    const auto lower_error = distance_u64(
        numerator,
        static_cast<std::uint64_t>(lower) * denominator);
    const auto upper_error = distance_u64(
        numerator,
        static_cast<std::uint64_t>(upper) * denominator);
    return upper_error < lower_error ? upper : lower;
}

} // namespace detail

// Fits a source inside the requested bounds without upscaling. H.264/NV12 requires even
// dimensions, so the limiting axis uses the largest legal even value and the other axis is
// rounded to the nearest even rational projection. This avoids floating point, loops, gcd-based
// pathological downscaling, and keeps aspect error below one output pixel on the rounded axis.
[[nodiscard]] constexpr std::optional<VideoExtent> fit_even_video_extent(
    std::uint32_t source_width,
    std::uint32_t source_height,
    std::uint32_t max_width,
    std::uint32_t max_height) noexcept {
    if (source_width == 0 || source_height == 0 ||
        max_width == 0 || max_height == 0) {
        return std::nullopt;
    }

    const auto width_bound =
        detail::min_u32(source_width, max_width) & ~std::uint32_t{1};
    const auto height_bound =
        detail::min_u32(source_height, max_height) & ~std::uint32_t{1};
    if (width_bound < 2U || height_bound < 2U) {
        return std::nullopt;
    }

    // Compare scales as exact rationals: width_bound/source_width versus
    // height_bound/source_height. Products of two uint32_t values fit in uint64_t.
    if (static_cast<std::uint64_t>(width_bound) * source_height <=
        static_cast<std::uint64_t>(height_bound) * source_width) {
        const auto height = detail::nearest_even_ratio(
            static_cast<std::uint64_t>(width_bound) * source_height,
            source_width,
            height_bound);
        if (height < 2U) {
            return std::nullopt;
        }
        return VideoExtent{width_bound, height};
    }

    const auto width = detail::nearest_even_ratio(
        static_cast<std::uint64_t>(height_bound) * source_width,
        source_height,
        width_bound);
    if (width < 2U) {
        return std::nullopt;
    }
    return VideoExtent{width, height_bound};
}

// Hardware H.264 encoders refuse tiny frames: NVENC rejects anything under about 145x49 at
// media-type negotiation. Smaller sources (a window caught mid-resize, a launcher splash) are
// scaled up by a whole factor, which keeps the aspect exact, until both sides reach this.
inline constexpr std::uint32_t kMinEncodedSide = 160;

// fit_even_video_extent for an encoder: the same fit, after scaling tiny sources up.
[[nodiscard]] constexpr std::optional<VideoExtent> fit_encodable_video_extent(
    std::uint32_t source_width,
    std::uint32_t source_height,
    std::uint32_t max_width,
    std::uint32_t max_height) noexcept {
    const auto shorter = detail::min_u32(source_width, source_height);
    if (shorter == 0) {
        return std::nullopt;
    }
    const std::uint64_t factor =
        shorter >= kMinEncodedSide ? 1U : (kMinEncodedSide + shorter - 1U) / shorter;
    const auto width = source_width * factor;
    const auto height = source_height * factor;
    if (width > UINT32_MAX || height > UINT32_MAX) {
        return std::nullopt;
    }
    return fit_even_video_extent(static_cast<std::uint32_t>(width),
                                 static_cast<std::uint32_t>(height), max_width, max_height);
}

} // namespace catro::video
