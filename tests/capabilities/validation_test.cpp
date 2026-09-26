#include <catro/capabilities/validation.hpp>

#include "fixtures/capability_fixtures.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>

using namespace catro::capabilities;
namespace fx = catro::fixtures;

namespace {

std::string describe(const ValidationReport& report) {
    std::string text;
    for (const auto& error : report.errors) {
        text += std::to_string(static_cast<int>(error.code)) + " @ " + error.path + "\n";
    }
    return text;
}

template <class T>
Observed<T> invalid_unknown_with_value(T value) {
    return Observed<T>{Knowledge::unknown, std::move(value), fx::absent(fx::kSystemProbe, IssueCode::not_reported)};
}

} // namespace

TEST_CASE("the reference desktop fixture is structurally valid") {
    const auto report = validate(fx::valid_snapshot());
    INFO(describe(report));
    REQUIRE(report.ok());
}

TEST_CASE("every machine fixture is structurally valid") {
    for (const auto& snapshot : {fx::high_end_desktop(), fx::intel_laptop_on_battery(), fx::hybrid_laptop(),
                                 fx::apple_silicon_macbook(), fx::hot_apple_silicon(), fx::older_intel_mac(),
                                 fx::headless_session(), fx::partial_probe_failure()}) {
        const auto report = validate(snapshot);
        INFO(describe(report));
        REQUIRE(report.ok());
    }
}

TEST_CASE("encoder cannot reference a missing GPU") {
    auto snapshot = fx::valid_snapshot();
    snapshot.devices.encoders.front().gpu = fx::known(GpuId{"missing", IdentityScope::snapshot}, fx::advertised(fx::kEncoderProbe));
    REQUIRE(validate(snapshot).contains(ValidationCode::missing_gpu_reference));
}

TEST_CASE("a reference with the right value but wrong scope is dangling") {
    auto snapshot = fx::valid_snapshot();
    snapshot.devices.displays.front().gpu = fx::known(GpuId{fx::desktop_gpu().value, IdentityScope::persistent},
                                                      fx::advertised(fx::kGpuDisplayProbe));
    REQUIRE(validate(snapshot).contains(ValidationCode::missing_gpu_reference));
}

TEST_CASE("unknown observations cannot carry values") {
    auto snapshot = fx::valid_snapshot();
    snapshot.hardware.installed_memory = invalid_unknown_with_value(Bytes{1});
    REQUIRE(validate(snapshot).contains(ValidationCode::unexpected_value));
}

TEST_CASE("known observations must carry a value") {
    auto snapshot = fx::valid_snapshot();
    snapshot.hardware.cpu.logical_cores = Observed<std::uint32_t>{Knowledge::known, std::nullopt, fx::measured(fx::kSystemProbe)};
    REQUIRE(validate(snapshot).contains(ValidationCode::missing_value));
}

TEST_CASE("absent or degraded evidence must state why") {
    auto snapshot = fx::valid_snapshot();
    snapshot.runtime.thermal = Observed<ThermalPressure>::unavailable(fx::measured(fx::kRuntimeProbe));
    REQUIRE(validate(snapshot).contains(ValidationCode::missing_issue_code));
}

TEST_CASE("high-confidence known evidence cannot also report an issue") {
    auto snapshot = fx::valid_snapshot();
    auto provenance = fx::measured(fx::kRuntimeProbe);
    provenance.issue = IssueCode::timeout;
    snapshot.runtime.thermal = fx::known(ThermalPressure::nominal, provenance);
    REQUIRE(validate(snapshot).contains(ValidationCode::contradictory_evidence));
}

TEST_CASE("an unset observation has no provenance and is rejected") {
    auto snapshot = fx::valid_snapshot();
    snapshot.runtime.headless = {};
    REQUIRE(validate(snapshot).contains(ValidationCode::missing_probe_reference));
}

TEST_CASE("evidence must name a probe that ran") {
    auto snapshot = fx::valid_snapshot();
    snapshot.runtime.headless = fx::known(false, fx::measured("windows.unscheduled.v1"));
    REQUIRE(validate(snapshot).contains(ValidationCode::missing_probe_reference));
}

TEST_CASE("identifiers are unique within their domain") {
    auto snapshot = fx::valid_snapshot();
    snapshot.devices.gpus.push_back(snapshot.devices.gpus.front());
    REQUIRE(validate(snapshot).contains(ValidationCode::duplicate_id));
}

TEST_CASE("identifiers are bounded printable text") {
    auto snapshot = fx::valid_snapshot();
    snapshot.devices.gpus.front().id.value = std::string(kMaxIdentifierBytes + 1, 'g');
    REQUIRE(validate(snapshot).contains(ValidationCode::invalid_identifier));

    snapshot = fx::valid_snapshot();
    snapshot.devices.gpus.front().id.value = "gpu 0";
    REQUIRE(validate(snapshot).contains(ValidationCode::invalid_identifier));
}

TEST_CASE("rational rates need a positive denominator and numerator") {
    auto snapshot = fx::valid_snapshot();
    snapshot.runtime.displays.front().active_mode = fx::known(
        DisplayMode{.pixels = {2560, 1440}, .logical = {2560, 1440}, .refresh_rate = Rational{60, 0}},
        fx::measured(fx::kGpuDisplayProbe));
    REQUIRE(validate(snapshot).contains(ValidationCode::invalid_rational));

    snapshot = fx::valid_snapshot();
    snapshot.runtime.displays.front().scale = fx::known(Rational{0, 1}, fx::measured(fx::kGpuDisplayProbe));
    REQUIRE(validate(snapshot).contains(ValidationCode::invalid_quantity));
}

TEST_CASE("ranges cannot be inverted and dimensions must be positive") {
    auto snapshot = fx::valid_snapshot();
    snapshot.devices.encoders.front().modes.front().frame_rates =
        fx::known(RationalRange{Rational{240, 1}, Rational{60000, 1001}}, fx::advertised(fx::kEncoderProbe));
    REQUIRE(validate(snapshot).contains(ValidationCode::invalid_range));

    snapshot = fx::valid_snapshot();
    snapshot.devices.encoders.front().modes.front().dimensions =
        fx::known(DimensionRange{{0, 128}, {4096, 4096}}, fx::advertised(fx::kEncoderProbe));
    REQUIRE(validate(snapshot).contains(ValidationCode::invalid_quantity));
}

TEST_CASE("logical cores cannot be fewer than physical cores") {
    auto snapshot = fx::valid_snapshot();
    snapshot.hardware.cpu.logical_cores = fx::known(8U, fx::measured(fx::kSystemProbe));
    REQUIRE(validate(snapshot).contains(ValidationCode::invalid_range));
}

TEST_CASE("translation state must agree with process and native architecture") {
    auto snapshot = fx::valid_snapshot();
    snapshot.hardware.cpu.translation = fx::known(TranslationState::translated, fx::measured(fx::kSystemProbe));
    REQUIRE(validate(snapshot).contains(ValidationCode::contradictory_evidence));
}

TEST_CASE("encoder modes agree with their parent codec") {
    auto snapshot = fx::valid_snapshot();
    snapshot.devices.encoders.front().modes.front().codec = Codec::hevc;
    REQUIRE(validate(snapshot).contains(ValidationCode::codec_mismatch));

    snapshot = fx::valid_snapshot();
    snapshot.devices.encoders.front().modes.front().profile = fx::known(CodecProfile::hevc_main10, fx::advertised(fx::kEncoderProbe));
    REQUIRE(validate(snapshot).contains(ValidationCode::codec_mismatch));
}

TEST_CASE("HDR modes require a compatible transfer function and bit depth") {
    auto snapshot = fx::valid_snapshot();
    snapshot.devices.encoders.front().modes.front().hdr = fx::known(HdrMode::hdr10, fx::advertised(fx::kEncoderProbe));
    const auto report = validate(snapshot);
    REQUIRE(report.contains(ValidationCode::inconsistent_color_format));
}

TEST_CASE("backends and capture APIs must belong to the snapshot platform") {
    auto snapshot = fx::valid_snapshot();
    snapshot.devices.encoders.front().backend = EncoderBackend::video_toolbox;
    REQUIRE(validate(snapshot).contains(ValidationCode::platform_mismatch));

    snapshot = fx::valid_snapshot();
    snapshot.devices.capture_paths.front().api = CaptureApi::screen_capture_kit;
    REQUIRE(validate(snapshot).contains(ValidationCode::platform_mismatch));
}

TEST_CASE("software encoders never carry a fabricated GPU") {
    auto snapshot = fx::valid_snapshot();
    snapshot.devices.encoders.back().gpu = fx::known(fx::desktop_gpu(), fx::advertised(fx::kEncoderProbe));
    REQUIRE(validate(snapshot).contains(ValidationCode::unexpected_gpu_reference));
}

TEST_CASE("transfer endpoints must exist and agree with encoder adapters") {
    auto snapshot = fx::valid_snapshot();
    snapshot.devices.transfer_paths.front().source = CapturePathId{"wgc:missing", IdentityScope::persistent};
    REQUIRE(validate(snapshot).contains(ValidationCode::missing_capture_path_reference));

    snapshot = fx::valid_snapshot();
    snapshot.devices.transfer_paths.front().destination = EncoderId{"missing", IdentityScope::service_lifetime};
    REQUIRE(validate(snapshot).contains(ValidationCode::missing_encoder_reference));

    snapshot = fx::valid_snapshot();
    snapshot.devices.transfer_paths.back().destination_gpu = fx::desktop_gpu();
    REQUIRE(validate(snapshot).contains(ValidationCode::invalid_transfer));

    snapshot = fx::valid_snapshot();
    snapshot.devices.transfer_paths.back().transfer = TransferKind::same_resource;
    REQUIRE(validate(snapshot).contains(ValidationCode::invalid_transfer));

    snapshot = fx::valid_snapshot();
    snapshot.devices.transfer_paths.front().transfer = TransferKind::cross_adapter_copy;
    REQUIRE(validate(snapshot).contains(ValidationCode::invalid_transfer));
}

TEST_CASE("unordered sets cannot repeat entries") {
    auto snapshot = fx::valid_snapshot();
    snapshot.devices.transfer_paths.front().conversions =
        fx::known(std::vector{Conversion::pixel_format, Conversion::pixel_format}, fx::advertised(fx::kEncoderProbe));
    REQUIRE(validate(snapshot).contains(ValidationCode::duplicate_entry));

    snapshot = fx::valid_snapshot();
    snapshot.devices.transfer_paths.push_back(snapshot.devices.transfer_paths.front());
    REQUIRE(validate(snapshot).contains(ValidationCode::duplicate_entry));
}

TEST_CASE("runtime state references existing devices exactly once") {
    auto snapshot = fx::valid_snapshot();
    snapshot.runtime.displays.front().display = DisplayId{"display:missing", IdentityScope::os_session};
    REQUIRE(validate(snapshot).contains(ValidationCode::missing_display_reference));

    snapshot = fx::valid_snapshot();
    snapshot.runtime.audio_endpoints.push_back(snapshot.runtime.audio_endpoints.front());
    REQUIRE(validate(snapshot).contains(ValidationCode::duplicate_id));

    snapshot = fx::valid_snapshot();
    snapshot.runtime.capture_permissions.front().path = CapturePathId{"sck:display", IdentityScope::persistent};
    REQUIRE(validate(snapshot).contains(ValidationCode::missing_capture_path_reference));
}

TEST_CASE("battery power without a battery is contradictory") {
    auto snapshot = fx::valid_snapshot();
    snapshot.runtime.power_source = fx::known(PowerSource::battery, fx::measured(fx::kRuntimeProbe));
    REQUIRE(validate(snapshot).contains(ValidationCode::contradictory_evidence));
}

TEST_CASE("schema identity and generation are checked") {
    auto snapshot = fx::valid_snapshot();
    snapshot.header.schema_version = SchemaVersion{2, 0};
    REQUIRE(validate(snapshot).contains(ValidationCode::unsupported_schema));

    snapshot = fx::valid_snapshot();
    snapshot.header.schema_id = "catro.other";
    REQUIRE(validate(snapshot).contains(ValidationCode::unsupported_schema));

    snapshot = fx::valid_snapshot();
    snapshot.header.generation = 0;
    REQUIRE(validate(snapshot).contains(ValidationCode::invalid_generation));

    snapshot = fx::valid_snapshot();
    snapshot.probes.front().generation = 2;
    REQUIRE(validate(snapshot).contains(ValidationCode::invalid_generation));
}

TEST_CASE("probe issues must name a scheduled probe") {
    auto snapshot = fx::valid_snapshot();
    snapshot.issues.push_back(ProbeIssue{.probe_id = "windows.unscheduled.v1", .code = IssueCode::timeout});
    REQUIRE(validate(snapshot).contains(ValidationCode::missing_probe_reference));
}

TEST_CASE("a timed-out family yields a valid partial snapshot") {
    auto snapshot = fx::valid_snapshot();
    snapshot.probes[3].outcome = ProbeOutcome::timeout;
    snapshot.issues.push_back(ProbeIssue{.probe_id = std::string(fx::kEncoderProbe), .code = IssueCode::timeout});
    snapshot.devices.encoders.clear();
    snapshot.devices.transfer_paths.clear();

    const auto report = validate(snapshot);
    INFO(describe(report));
    REQUIRE(report.ok());
}

TEST_CASE("validation errors are reported in a deterministic order") {
    auto first = fx::valid_snapshot();
    first.devices.encoders.front().gpu = fx::known(GpuId{"missing-a", IdentityScope::snapshot}, fx::advertised(fx::kEncoderProbe));
    first.devices.encoders.back().gpu = fx::known(GpuId{"missing-b", IdentityScope::snapshot}, fx::advertised(fx::kEncoderProbe));
    auto second = first;
    std::ranges::reverse(second.devices.encoders);

    const auto lhs = validate(first);
    const auto rhs = validate(second);
    REQUIRE_FALSE(lhs.ok());
    REQUIRE(lhs.errors == rhs.errors);
}
