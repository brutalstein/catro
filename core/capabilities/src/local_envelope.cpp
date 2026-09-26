#include <catro/capabilities/policy.hpp>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

namespace catro::capabilities {
namespace {

struct Ceiling {
    Dimensions box;
    Rational frame_rate;
    bool hdr = false;
};

Ceiling ceiling_for(OperatingProfile profile) {
    switch (profile) {
    case OperatingProfile::performance:
        return {{7680, 4320}, Rational{240, 1}, true};
    case OperatingProfile::balanced:
        return {{2560, 1440}, Rational{60, 1}, true};
    case OperatingProfile::efficiency:
        return {{1920, 1080}, Rational{30, 1}, false};
    case OperatingProfile::thermal_constrained:
    case OperatingProfile::safe_local_envelope:
        return {{1280, 720}, Rational{30, 1}, false};
    }
    return {{1280, 720}, Rational{30, 1}, false};
}

// Unknown limits or unknown support never justify more than this.
constexpr Dimensions kUnknownLimitBox{1920, 1080};
constexpr Rational kUnknownLimitRate{30, 1};

template <class T>
const T* known_value(const Observed<T>& observed) {
    return observed.knowledge() == Knowledge::known && observed.value() ? &*observed.value() : nullptr;
}

std::uint64_t pixels(Dimensions dimensions) {
    return std::uint64_t{dimensions.width} * dimensions.height;
}

Dimensions even(Dimensions dimensions) {
    return {std::max(2U, dimensions.width & ~1U), std::max(2U, dimensions.height & ~1U)};
}

// A landscape box applies to landscape sources and is turned for portrait ones.
Dimensions oriented(Dimensions box, Dimensions source) {
    return source.height > source.width ? Dimensions{box.height, box.width} : box;
}

// Largest size with the source aspect ratio inside the bound, never upscaled.
Dimensions fit(Dimensions source, Dimensions bound) {
    if (source.width <= bound.width && source.height <= bound.height) {
        return source;
    }
    const std::uint64_t width = source.width;
    const std::uint64_t height = source.height;
    if (width * bound.height >= height * bound.width) {
        return even({bound.width, static_cast<std::uint32_t>(height * bound.width / width)});
    }
    return even({static_cast<std::uint32_t>(width * bound.height / height), bound.height});
}

struct Point {
    Dimensions resolution;
    Rational frame_rate;
    bool degraded = false;
    std::vector<ReasonCode> reasons;

    void bound_size(Dimensions box, ReasonCode reason) {
        const auto bounded = fit(resolution, box);
        if (!(bounded == resolution)) {
            resolution = bounded;
            reasons.push_back(reason);
        }
    }

    void bound_rate(Rational limit, ReasonCode reason) {
        if (limit < frame_rate) {
            frame_rate = limit;
            reasons.push_back(reason);
        }
    }
};

// Higher resolution, then higher frame rate, then better evidence; lexicographic, no scoring.
bool better(const Point& lhs, const Point& rhs) {
    return std::tuple{pixels(lhs.resolution), lhs.frame_rate, !lhs.degraded} >
           std::tuple{pixels(rhs.resolution), rhs.frame_rate, !rhs.degraded};
}

const DisplayState* source_display(const CapabilitySnapshot& snapshot, const MediaDecisionRequest& request) {
    const auto& displays = snapshot.runtime.displays;
    if (const auto* headless = known_value(snapshot.runtime.headless); headless && *headless) {
        return nullptr;
    }
    if (request.display) {
        const auto found = std::ranges::find(displays, *request.display, &DisplayState::display);
        return found != displays.end() ? &*found : nullptr;
    }
    const auto primary = std::ranges::find_if(displays, [](const DisplayState& state) {
        const auto* is_primary = known_value(state.primary);
        return is_primary && *is_primary;
    });
    if (primary != displays.end()) {
        return &*primary;
    }
    return displays.size() == 1 ? &displays.front() : nullptr;
}

bool usable(const SupportFact& fact) {
    return fact.status != Support::unsupported;
}

bool supported(const SupportFact& fact) {
    return fact.status == Support::supported;
}

class EnvelopeBuilder {
public:
    EnvelopeBuilder(const CapabilitySnapshot& snapshot, const MediaDecisionRequest& request, OperatingProfile profile)
        : snapshot_(snapshot), request_(request), ceiling_(ceiling_for(profile)) {}

    LocalQualityEnvelope build() {
        const auto* display = source_display(snapshot_, request_);
        if (display == nullptr) {
            return not_viable(ReasonCode::no_source_display);
        }
        const auto captures = usable_captures();
        if (captures.empty()) {
            return not_viable(ReasonCode::no_capture_path);
        }

        std::optional<Point> best;
        for (const auto* capture : captures) {
            for (const auto* mode : reachable_modes(*capture)) {
                auto point = candidate(*display, *capture, *mode);
                if (point && (!best || better(*point, *best))) {
                    best = std::move(point);
                }
            }
        }
        if (!best) {
            return not_viable(ReasonCode::no_encoder_mode);
        }

        LocalQualityEnvelope envelope;
        envelope.viable = true;
        envelope.resolution = best->resolution;
        envelope.frame_rate = best->frame_rate;
        envelope.hdr = hdr_achievable(*display, captures);
        envelope.bit_depth = envelope.hdr ? 10 : 8;
        envelope.confidence = best->degraded ? Confidence::degraded : Confidence::high;
        envelope.reasons = std::move(best->reasons);
        if (request_.quality.hdr && !envelope.hdr) {
            envelope.reasons.push_back(ReasonCode::hdr_unavailable);
        }
        std::ranges::sort(envelope.reasons);
        const auto duplicates = std::ranges::unique(envelope.reasons);
        envelope.reasons.erase(duplicates.begin(), duplicates.end());
        return envelope;
    }

private:
    LocalQualityEnvelope not_viable(ReasonCode reason) const {
        LocalQualityEnvelope envelope;
        envelope.frame_rate = Rational{0, 1};
        envelope.confidence = snapshot_.issues.empty() ? Confidence::high : Confidence::degraded;
        envelope.reasons = {reason};
        return envelope;
    }

    std::vector<const CapturePathCapability*> usable_captures() const {
        std::vector<const CapturePathCapability*> captures;
        for (const auto& capture : snapshot_.devices.capture_paths) {
            if (capture.source != request_.source || !usable(capture.support)) {
                continue;
            }
            const auto& permissions = snapshot_.runtime.capture_permissions;
            const auto state = std::ranges::find(permissions, capture.id, &CapturePermissionState::path);
            if (state != permissions.end()) {
                const auto* permission = known_value(state->permission);
                if (permission && *permission == CapturePermission::denied) {
                    continue;
                }
            }
            captures.push_back(&capture);
        }
        return captures;
    }

    const EncoderCapability* encoder(const EncoderId& id) const {
        const auto& encoders = snapshot_.devices.encoders;
        const auto found = std::ranges::find(encoders, id, &EncoderCapability::id);
        return found != encoders.end() ? &*found : nullptr;
    }

    std::vector<const EncoderModeCapability*> reachable_modes(const CapturePathCapability& capture) const {
        std::vector<const EncoderModeCapability*> modes;
        for (const auto& transfer : snapshot_.devices.transfer_paths) {
            if (transfer.source != capture.id || !usable(transfer.evidence)) {
                continue;
            }
            const auto* target = encoder(transfer.destination);
            if (target == nullptr || !usable(target->support)) {
                continue;
            }
            for (const auto& mode : target->modes) {
                if (usable(mode.support)) {
                    modes.push_back(&mode);
                }
            }
        }
        return modes;
    }

    std::optional<Point> candidate(const DisplayState& display, const CapturePathCapability& capture,
                                   const EncoderModeCapability& mode) const {
        Point point;
        if (const auto* active = known_value(display.active_mode)) {
            point.resolution = even(active->pixels);
            point.frame_rate = active->refresh_rate;
        } else {
            point.resolution = kUnknownLimitBox;
            point.frame_rate = kUnknownLimitRate;
            point.degraded = true;
            point.reasons.push_back(ReasonCode::limited_by_unknown_limits);
        }
        const auto source_resolution = point.resolution;
        const auto source_rate = point.frame_rate;

        if (const auto* rates = known_value(capture.frame_rates)) {
            point.bound_rate(rates->maximum, ReasonCode::limited_by_capture);
        }

        const bool trusted = supported(mode.support);
        const auto* dimensions = trusted ? known_value(mode.dimensions) : nullptr;
        if (dimensions) {
            point.bound_size(dimensions->maximum, ReasonCode::limited_by_encoder);
            if (point.resolution.width < dimensions->minimum.width || point.resolution.height < dimensions->minimum.height) {
                return std::nullopt;
            }
        } else {
            point.degraded = true;
            point.bound_size(oriented(kUnknownLimitBox, point.resolution), ReasonCode::limited_by_unknown_limits);
        }
        if (const auto* rates = trusted ? known_value(mode.frame_rates) : nullptr) {
            point.bound_rate(rates->maximum, ReasonCode::limited_by_encoder);
        } else {
            point.degraded = true;
            point.bound_rate(kUnknownLimitRate, ReasonCode::limited_by_unknown_limits);
        }

        point.bound_size(oriented(ceiling_.box, point.resolution), ReasonCode::limited_by_profile);
        point.bound_rate(ceiling_.frame_rate, ReasonCode::limited_by_profile);

        if (point.resolution == source_resolution || point.frame_rate == source_rate) {
            point.reasons.push_back(ReasonCode::limited_by_source);
        }
        return point;
    }

    // HDR needs an enabled HDR display, HDR capture output, a transfer that keeps HDR, and a
    // supported HDR encoder mode with a matching transfer function and bit depth.
    bool hdr_achievable(const DisplayState& display, const std::vector<const CapturePathCapability*>& captures) const {
        if (!ceiling_.hdr) {
            return false;
        }
        const auto* enabled = known_value(display.hdr_enabled);
        const auto& displays = snapshot_.devices.displays;
        const auto capability = std::ranges::find(displays, display.display, &DisplayCapability::id);
        if (!enabled || !*enabled || capability == displays.end() || !supported(capability->hdr)) {
            return false;
        }
        for (const auto* capture : captures) {
            if (!supported(capture->hdr_output)) {
                continue;
            }
            for (const auto& transfer : snapshot_.devices.transfer_paths) {
                const auto* conversions = known_value(transfer.conversions);
                if (transfer.source != capture->id || !usable(transfer.evidence) || !conversions ||
                    std::ranges::find(*conversions, Conversion::hdr_to_sdr) != conversions->end()) {
                    continue;
                }
                const auto* target = encoder(transfer.destination);
                if (target != nullptr && usable(target->support) && std::ranges::any_of(target->modes, hdr_mode)) {
                    return true;
                }
            }
        }
        return false;
    }

    static bool hdr_mode(const EncoderModeCapability& mode) {
        const auto* hdr = known_value(mode.hdr);
        const auto* transfer = known_value(mode.transfer_function);
        const auto* bit_depth = known_value(mode.bit_depth);
        return supported(mode.support) && hdr && *hdr != HdrMode::sdr && transfer &&
               (*transfer == TransferFunction::pq || *transfer == TransferFunction::hlg) && bit_depth && *bit_depth >= 10;
    }

    const CapabilitySnapshot& snapshot_;
    const MediaDecisionRequest& request_;
    Ceiling ceiling_;
};

} // namespace

LocalQualityEnvelope derive_local_envelope(const CapabilitySnapshot& snapshot, const MediaDecisionRequest& request,
                                           OperatingProfile profile) {
    return EnvelopeBuilder(snapshot, request, profile).build();
}

} // namespace catro::capabilities
