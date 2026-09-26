#include <catro/capabilities/policy.hpp>

#include "fixtures/capability_fixtures.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <vector>

using namespace catro::capabilities;
namespace fx = catro::fixtures;

namespace {

LocalQualityEnvelope envelope(const CapabilitySnapshot& snapshot, OperatingProfile profile,
                              MediaDecisionRequest request = fx::display_request()) {
    return derive_local_envelope(snapshot, request, profile);
}

bool has_reason(const LocalQualityEnvelope& envelope, ReasonCode reason) {
    return std::ranges::find(envelope.reasons, reason) != envelope.reasons.end();
}

void remove_encoder(CapabilitySnapshot& snapshot, const EncoderId& id) {
    std::erase_if(snapshot.devices.encoders, [&](const EncoderCapability& encoder) { return encoder.id == id; });
    std::erase_if(snapshot.devices.transfer_paths,
                  [&](const TransferPathCapability& transfer) { return transfer.destination == id; });
}

void set_active_rate(CapabilitySnapshot& snapshot, Rational rate) {
    auto& state = snapshot.runtime.displays.front();
    auto active = *state.active_mode.value();
    active.refresh_rate = rate;
    state.active_mode = fx::known(active, fx::measured(fx::kGpuDisplayProbe));
}

void enable_hdr_output(CapabilitySnapshot& snapshot) {
    snapshot.runtime.displays.front().hdr_enabled = fx::known(true, fx::measured(fx::kGpuDisplayProbe));
}

} // namespace

TEST_CASE("a known hardware path is bounded by the source display") {
    const auto result = envelope(fx::valid_snapshot(), OperatingProfile::performance);
    REQUIRE(result.viable);
    REQUIRE(result.resolution == Dimensions{2560, 1440});
    REQUIRE(result.frame_rate == Rational{144, 1});
    REQUIRE(result.confidence == Confidence::high);
    REQUIRE(result.reasons == std::vector{ReasonCode::limited_by_source});
}

TEST_CASE("unknown encoder limits cannot authorize 1440p60") {
    auto snapshot = fx::valid_snapshot();
    remove_encoder(snapshot, fx::hardware_h264());

    const auto result = envelope(snapshot, OperatingProfile::performance);
    REQUIRE(result.viable);
    REQUIRE(result.resolution == Dimensions{1920, 1080});
    REQUIRE(result.frame_rate == Rational{30, 1});
    REQUIRE(result.confidence == Confidence::degraded);
    REQUIRE(has_reason(result, ReasonCode::limited_by_unknown_limits));
}

TEST_CASE("unknown mode support is held to the conservative ceiling") {
    auto snapshot = fx::valid_snapshot();
    snapshot.devices.encoders.front().modes.front().support =
        fx::support_unknown(fx::kEncoderProbe, IssueCode::not_reported);

    const auto result = envelope(snapshot, OperatingProfile::performance);
    REQUIRE(result.resolution == Dimensions{1920, 1080});
    REQUIRE(result.frame_rate == Rational{30, 1});
    REQUIRE(result.confidence == Confidence::degraded);
}

TEST_CASE("exact 59.94 Hz survives the envelope") {
    auto snapshot = fx::valid_snapshot();
    set_active_rate(snapshot, Rational{60000, 1001});

    const auto result = envelope(snapshot, OperatingProfile::balanced);
    REQUIRE(result.frame_rate == Rational{60000, 1001});
    REQUIRE(result.frame_rate.numerator() == 60000);
    REQUIRE(result.frame_rate.denominator() == 1001);
}

TEST_CASE("profile ceilings preserve the source aspect ratio with even dimensions") {
    const auto balanced = envelope(fx::valid_snapshot(), OperatingProfile::balanced);
    REQUIRE(balanced.resolution == Dimensions{2560, 1440});
    REQUIRE(balanced.frame_rate == Rational{60, 1});
    REQUIRE(has_reason(balanced, ReasonCode::limited_by_profile));

    const auto efficient = envelope(fx::intel_laptop_on_battery(), OperatingProfile::efficiency);
    REQUIRE(efficient.resolution == Dimensions{1728, 1080});
    REQUIRE(efficient.frame_rate == Rational{30, 1});

    const auto hot = envelope(fx::hot_apple_silicon(), OperatingProfile::thermal_constrained);
    REQUIRE(hot.resolution == Dimensions{1108, 720});
    REQUIRE(hot.frame_rate == Rational{30, 1});
}

TEST_CASE("capture limits bound the frame rate when known") {
    auto snapshot = fx::valid_snapshot();
    snapshot.devices.capture_paths.front().frame_rates =
        fx::known(RationalRange{Rational{1, 1}, Rational{120, 1}}, fx::advertised(fx::kGpuDisplayProbe));

    const auto result = envelope(snapshot, OperatingProfile::performance);
    REQUIRE(result.frame_rate == Rational{120, 1});
    REQUIRE(has_reason(result, ReasonCode::limited_by_capture));
}

TEST_CASE("HDR requires compatible display, capture, mode, and transfer evidence") {
    auto snapshot = fx::high_end_desktop();
    enable_hdr_output(snapshot);
    const auto hdr = envelope(snapshot, OperatingProfile::performance);
    REQUIRE(hdr.hdr);
    REQUIRE(hdr.bit_depth == 10);

    auto sdr_modes_only = fx::valid_snapshot();
    enable_hdr_output(sdr_modes_only);
    REQUIRE_FALSE(envelope(sdr_modes_only, OperatingProfile::performance).hdr);

    auto display_off = fx::high_end_desktop();
    REQUIRE_FALSE(envelope(display_off, OperatingProfile::performance).hdr);

    auto tone_mapped = fx::high_end_desktop();
    enable_hdr_output(tone_mapped);
    for (auto& transfer : tone_mapped.devices.transfer_paths) {
        transfer.conversions = fx::known(std::vector{Conversion::pixel_format, Conversion::hdr_to_sdr},
                                         fx::advertised(fx::kEncoderProbe));
    }
    REQUIRE_FALSE(envelope(tone_mapped, OperatingProfile::performance).hdr);

    auto no_capture_hdr = fx::high_end_desktop();
    enable_hdr_output(no_capture_hdr);
    no_capture_hdr.devices.capture_paths.front().hdr_output = fx::unsupported(fx::advertised(fx::kGpuDisplayProbe));
    REQUIRE_FALSE(envelope(no_capture_hdr, OperatingProfile::performance).hdr);

    REQUIRE_FALSE(envelope(snapshot, OperatingProfile::efficiency).hdr);
}

TEST_CASE("requested HDR that cannot be met is explained") {
    auto request = fx::display_request();
    request.quality.hdr = true;
    const auto result = envelope(fx::valid_snapshot(), OperatingProfile::performance, request);
    REQUIRE_FALSE(result.hdr);
    REQUIRE(has_reason(result, ReasonCode::hdr_unavailable));
}

TEST_CASE("headless machines produce no display envelope") {
    const auto result = envelope(fx::headless_session(), OperatingProfile::safe_local_envelope);
    REQUIRE_FALSE(result.viable);
    REQUIRE(result.reasons == std::vector{ReasonCode::no_source_display});
}

TEST_CASE("a missing requested display produces no envelope") {
    auto request = fx::display_request();
    request.display = DisplayId{"display:missing", IdentityScope::os_session};
    const auto result = envelope(fx::valid_snapshot(), OperatingProfile::performance, request);
    REQUIRE_FALSE(result.viable);
    REQUIRE(result.reasons == std::vector{ReasonCode::no_source_display});
}

TEST_CASE("denied capture permission leaves no capture path") {
    auto snapshot = fx::apple_silicon_macbook();
    snapshot.runtime.capture_permissions.front().permission =
        fx::known(CapturePermission::denied, fx::advertised(fx::kMacGpuDisplayProbe));
    const auto result = envelope(snapshot, OperatingProfile::balanced);
    REQUIRE_FALSE(result.viable);
    REQUIRE(result.reasons == std::vector{ReasonCode::no_capture_path});
}

TEST_CASE("a timed-out encoder family leaves no encoder and degraded confidence") {
    const auto result = envelope(fx::partial_probe_failure(), OperatingProfile::performance);
    REQUIRE_FALSE(result.viable);
    REQUIRE(result.reasons == std::vector{ReasonCode::no_encoder_mode});
    REQUIRE(result.confidence == Confidence::degraded);
}

TEST_CASE("window capture without a capture path for that source kind is not viable") {
    auto request = fx::display_request();
    request.source = SourceKind::window;
    const auto result = envelope(fx::valid_snapshot(), OperatingProfile::performance, request);
    REQUIRE_FALSE(result.viable);
    REQUIRE(result.reasons == std::vector{ReasonCode::no_capture_path});
}

TEST_CASE("unprovable transfer relationships still reach their encoders") {
    const auto result = envelope(fx::older_intel_mac(), OperatingProfile::balanced);
    REQUIRE(result.viable);
    REQUIRE(result.resolution == Dimensions{2304, 1440});
    REQUIRE(result.frame_rate == Rational{60, 1});
}

TEST_CASE("the envelope is independent of inventory order") {
    auto snapshot = fx::hybrid_laptop();
    const auto expected = envelope(snapshot, OperatingProfile::balanced);
    std::ranges::reverse(snapshot.devices.encoders);
    std::ranges::reverse(snapshot.devices.transfer_paths);
    REQUIRE(envelope(snapshot, OperatingProfile::balanced) == expected);
}
