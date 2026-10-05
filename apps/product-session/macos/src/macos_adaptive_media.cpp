#include <catro/macos_adaptive_media.hpp>

#include <catro/capabilities/policy.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <tuple>

namespace catro::product {
namespace {

namespace caps = catro::capabilities;

struct Limits {
    std::uint32_t max_height = 720;
    std::uint32_t fps = 30;
};

Limits camera_limits(caps::OperatingProfile profile) {
    switch (profile) {
    case caps::OperatingProfile::performance:
    case caps::OperatingProfile::balanced:
        return {2160, 60};
    case caps::OperatingProfile::efficiency:
        return {1080, 30};
    case caps::OperatingProfile::thermal_constrained:
    case caps::OperatingProfile::safe_local_envelope:
        return {720, 30};
    }
    return {720, 30};
}

std::string profile_detail(const caps::ProfileDecision& decision) {
    using enum caps::OperatingProfile;
    std::string profile;
    switch (decision.profile) {
    case performance:
        profile = "Performance";
        break;
    case balanced:
        profile = "Balanced";
        break;
    case efficiency:
        profile = "Efficiency";
        break;
    case thermal_constrained:
        profile = "Thermal-safe";
        break;
    case safe_local_envelope:
        profile = "Safe";
        break;
    }
    switch (decision.rule) {
    case caps::ProfileRule::battery_or_low_power:
        return profile + " · battery / Low Power Mode";
    case caps::ProfileRule::thermal_pressure:
        return profile + " · thermal pressure";
    case caps::ProfileRule::constrained_session:
        return profile + " · constrained session";
    case caps::ProfileRule::unknown_power_source:
        return profile + " · conservative power state";
    default:
        return profile + " · automatic";
    }
}

std::uint32_t initial_bitrate(std::uint32_t width, std::uint32_t height, std::uint32_t fps) {
    const double bits = static_cast<double>(width) * static_cast<double>(height) *
                        static_cast<double>(fps) * 0.06;
    const auto value = static_cast<std::uint64_t>(std::llround(bits));
    return static_cast<std::uint32_t>(
        std::clamp<std::uint64_t>(value, 2'500'000ULL, 25'000'000ULL));
}

std::uint32_t integer_fps(const caps::Rational& rate) {
    if (!rate.valid() || rate.denominator() == 0) {
        return 30;
    }
    const double value = static_cast<double>(rate.numerator()) /
                         static_cast<double>(rate.denominator());
    return static_cast<std::uint32_t>(std::clamp(std::llround(value), 1LL, 120LL));
}

caps::Dimensions fit_box(std::uint32_t width, std::uint32_t height, std::uint32_t max_height) {
    if (width == 0 || height == 0) {
        return {1280, 720};
    }
    if (height <= max_height) {
        return {width & ~1U, height & ~1U};
    }
    const auto scaled_width = static_cast<std::uint32_t>(
        (static_cast<std::uint64_t>(width) * max_height) / height);
    return {std::max(2U, scaled_width & ~1U), std::max(2U, max_height & ~1U)};
}

std::optional<caps::DisplayId> display_for(const platform::macos::CaptureSource& source) {
    if (source.display_id == 0) {
        return std::nullopt;
    }
    return caps::DisplayId{
        "cgdisplay:" + std::to_string(source.display_id),
        caps::IdentityScope::os_session};
}

caps::SourceKind source_kind(const platform::macos::CaptureSource& source) {
    return source.kind == platform::macos::CaptureSourceKind::window
               ? caps::SourceKind::window
               : caps::SourceKind::display;
}

AdaptiveShareQuality safe_quality(
    const platform::macos::CaptureSource& source,
    std::uint32_t max_width,
    std::uint32_t max_height,
    std::uint32_t fps,
    std::string detail = "Safe · capability scan pending") {
    const auto source_box = caps::Dimensions{
        std::max(2U, source.width & ~1U),
        std::max(2U, source.height & ~1U)};
    const auto requested = caps::Dimensions{
        std::max(2U, max_width & ~1U),
        std::max(2U, max_height & ~1U)};
    const auto target_height = std::min({source_box.height, requested.height, 720U});
    auto target = fit_box(source_box.width, source_box.height, target_height);
    target.width = std::min(target.width, requested.width);
    target.width &= ~1U;
    target.height &= ~1U;
    const auto target_fps = std::min(std::max(1U, fps), 30U);
    return {
        .label = target.height >= source_box.height ? "Source" : std::to_string(target.height) + "p",
        .detail = std::move(detail),
        .max_width = std::max(320U, target.width),
        .max_height = std::max(180U, target.height),
        .fps = target_fps,
        .bitrate = initial_bitrate(target.width, target.height, target_fps),
        .recommended = true,
        .profile = caps::OperatingProfile::safe_local_envelope,
    };
}

AdaptiveShareQuality camera_quality(
    const caps::CapabilitySnapshot* snapshot,
    const platform::macos::CaptureSource& source,
    std::uint32_t max_width,
    std::uint32_t max_height,
    std::uint32_t fps) {
    if (snapshot == nullptr) {
        return safe_quality(source, max_width, max_height, fps);
    }
    const auto decision = caps::derive_operating_profile(
        *snapshot, caps::OperatingPreference::automatic);
    const auto limits = camera_limits(decision.profile);
    const auto requested_height = std::min({
        std::max(2U, max_height),
        std::max(2U, source.height),
        limits.max_height});
    const auto target = fit_box(source.width, source.height, requested_height);
    const auto target_fps = std::min(std::max(1U, fps), limits.fps);
    return {
        .label = target.height >= source.height ? "Source" : std::to_string(target.height) + "p",
        .detail = profile_detail(decision),
        .max_width = std::max(320U, std::min(target.width, std::max(320U, max_width))),
        .max_height = std::max(180U, std::min(target.height, std::max(180U, max_height))),
        .fps = target_fps,
        .bitrate = initial_bitrate(target.width, target.height, target_fps),
        .recommended = false,
        .profile = decision.profile,
    };
}

AdaptiveShareQuality planned_quality(
    const caps::CapabilitySnapshot* snapshot,
    const platform::macos::CaptureSource& source,
    std::uint32_t max_width,
    std::uint32_t max_height,
    std::uint32_t fps) {
    if (snapshot == nullptr) {
        return safe_quality(source, max_width, max_height, fps);
    }
    if (source.kind == platform::macos::CaptureSourceKind::camera) {
        return camera_quality(snapshot, source, max_width, max_height, fps);
    }

    caps::MediaDecisionRequest request;
    request.source = source_kind(source);
    request.display = display_for(source);
    request.latency = caps::LatencyClass::interactive;
    request.preference = caps::OperatingPreference::automatic;
    request.quality.resolution = {
        std::max(2U, std::min(max_width, source.width) & ~1U),
        std::max(2U, std::min(max_height, source.height) & ~1U)};
    request.quality.frame_rate = caps::Rational{std::clamp(fps, 1U, 120U), 1};
    request.quality.hdr = false;

    const auto plan = caps::derive_media_plan(*snapshot, request, caps::kPolicyVersion);
    if (plan.status != caps::PlanStatus::planned || !plan.selected) {
        const auto decision = caps::derive_operating_profile(
            *snapshot, caps::OperatingPreference::automatic);
        auto fallback = safe_quality(source, max_width, max_height, fps, profile_detail(decision));
        fallback.profile = decision.profile;
        return fallback;
    }

    const auto width = std::max(2U, plan.start.resolution.width & ~1U);
    const auto height = std::max(2U, plan.start.resolution.height & ~1U);
    const auto target_fps = integer_fps(plan.start.frame_rate);
    return {
        .label = height >= source.height ? "Source" : std::to_string(height) + "p",
        .detail = profile_detail(plan.profile),
        .max_width = std::max(320U, width),
        .max_height = std::max(180U, height),
        .fps = target_fps,
        .bitrate = initial_bitrate(width, height, target_fps),
        .recommended = false,
        .profile = plan.profile.profile,
    };
}

} // namespace

AdaptiveShareQuality adaptive_share_quality(
    const caps::CapabilitySnapshot* snapshot,
    const platform::macos::CaptureSource& source,
    std::uint32_t max_width,
    std::uint32_t max_height,
    std::uint32_t fps) {
    return planned_quality(
        snapshot,
        source,
        std::clamp(max_width, 320U, 7680U),
        std::clamp(max_height, 180U, 4320U),
        std::clamp(fps, 1U, 120U));
}

std::vector<AdaptiveShareQuality> adaptive_share_qualities(
    const caps::CapabilitySnapshot* snapshot,
    const platform::macos::CaptureSource& source) {
    if (source.width < 2 || source.height < 2) {
        return {};
    }

    constexpr std::array<std::uint32_t, 4> heights{720, 1080, 1440, 2160};
    std::vector<AdaptiveShareQuality> result;
    const auto append = [&](std::uint32_t height, std::uint32_t fps) {
        const auto box = fit_box(source.width, source.height, std::min(height, source.height));
        auto quality = planned_quality(snapshot, source, box.width, box.height, fps);
        const auto duplicate = std::ranges::find_if(result, [&](const AdaptiveShareQuality& existing) {
            return existing.max_width == quality.max_width &&
                   existing.max_height == quality.max_height &&
                   existing.fps == quality.fps;
        });
        if (duplicate == result.end()) {
            result.push_back(std::move(quality));
        }
    };

    for (const auto height : heights) {
        if (height <= source.height) {
            append(height, 60);
        }
    }
    append(source.height, 60);

    if (result.empty()) {
        result.push_back(safe_quality(source, source.width, source.height, 30));
    }
    for (auto& quality : result) {
        quality.recommended = false;
    }

    // "Auto" keeps a generous user ceiling while the policy chooses the current operating point.
    // This is deliberately different from the current highest choice: starting on battery or under
    // thermal pressure must not permanently prevent the share from rising again after recovery.
    const auto auto_box = fit_box(source.width, source.height, std::min(source.height, 2160U));
    AdaptiveShareQuality automatic{
        .label = "Auto",
        .detail = "Adapts to hardware, display, power, thermals & network",
        .max_width = std::max(320U, auto_box.width),
        .max_height = std::max(180U, auto_box.height),
        .fps = 60,
        .bitrate = initial_bitrate(auto_box.width, auto_box.height, 60),
        .recommended = true,
        .profile = snapshot
            ? caps::derive_operating_profile(*snapshot, caps::OperatingPreference::automatic).profile
            : caps::OperatingProfile::safe_local_envelope,
    };
    result.insert(result.begin(), std::move(automatic));
    return result;
}

} // namespace catro::product
