#include <catro/capabilities/policy.hpp>
#include <catro/capabilities/validation.hpp>

#include "fixtures/capability_fixtures.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <random>
#include <utility>
#include <vector>

using namespace catro::capabilities;
namespace fx = catro::fixtures;

namespace {

MediaPlan plan_for(const CapabilitySnapshot& snapshot, const MediaDecisionRequest& request = fx::display_request()) {
    return derive_media_plan(snapshot, request, kPolicyVersion);
}

const TraceRecord& record_for(const MediaPlan& plan, DecisionCategory category, const EncoderId& encoder) {
    const auto found = std::ranges::find_if(plan.trace.records, [&](const TraceRecord& record) {
        return record.category == category && record.subject.encoder == encoder;
    });
    REQUIRE(found != plan.trace.records.end());
    return *found;
}

MediaDecisionRequest request_for(const DisplayId& display, Rational frame_rate = Rational{60, 1}) {
    auto request = fx::display_request();
    request.display = display;
    request.quality.frame_rate = frame_rate;
    return request;
}

// Reorders every collection whose order carries no meaning.
CapabilitySnapshot shuffled(CapabilitySnapshot snapshot, std::uint32_t seed) {
    std::mt19937 random(seed);
    const auto shuffle = [&random](auto& items) { std::ranges::shuffle(items, random); };
    auto& devices = snapshot.devices;
    shuffle(devices.gpus);
    shuffle(devices.encoders);
    for (auto& encoder : devices.encoders) {
        shuffle(encoder.modes);
    }
    shuffle(devices.capture_paths);
    shuffle(devices.displays);
    shuffle(devices.audio_endpoints);
    shuffle(devices.transfer_paths);
    shuffle(snapshot.runtime.displays);
    shuffle(snapshot.runtime.audio_endpoints);
    shuffle(snapshot.runtime.capture_permissions);
    shuffle(snapshot.probes);
    shuffle(snapshot.issues);
    return snapshot;
}

} // namespace

TEST_CASE("a missing GPU driver rejects the hardware encoder and falls back to software explicitly") {
    const auto plan = plan_for(fx::missing_gpu_driver());
    REQUIRE(plan.status == PlanStatus::planned);
    REQUIRE(plan.selected);
    REQUIRE(plan.selected->encoder == fx::software_h264());
    REQUIRE(plan.selected->consequences == std::vector{Consequence::cpu_load, Consequence::power_draw,
                                                       Consequence::added_latency, Consequence::limited_quality});
    REQUIRE(plan.selected->confidence == Confidence::degraded);
    REQUIRE(plan.fallbacks.empty());

    const auto& hardware = record_for(plan, DecisionCategory::encoder, fx::hardware_h264());
    REQUIRE(hardware.outcome == CandidateOutcome::rejected);
    REQUIRE(hardware.rule == PolicyRule::remove_unsupported);
    REQUIRE(hardware.reasons == std::vector{ReasonCode::encoder_unsupported});
}

TEST_CASE("software-only machines select software explicitly, never in the safe envelope") {
    const auto plan = plan_for(fx::software_only());
    REQUIRE(plan.selected);
    REQUIRE(plan.selected->implementation == ImplementationClass::software);
    REQUIRE(plan.start == StartingQuality{{1920, 1080}, Rational{30, 1}, 8, false});
    REQUIRE(plan.trace.records.front().reasons.front() == ReasonCode::unknown_limits);

    auto remote = fx::software_only();
    remote.runtime.remote_session = fx::known(true, fx::measured(fx::kRuntimeProbe));
    const auto safe = plan_for(remote);
    REQUIRE(safe.profile.profile == OperatingProfile::safe_local_envelope);
    REQUIRE(safe.status == PlanStatus::no_viable_path);
    REQUIRE(safe.reasons == std::vector{ReasonCode::no_encoder_mode});
    REQUIRE_FALSE(safe.selected);
    REQUIRE(safe.fallbacks.empty());
    const auto& software = record_for(safe, DecisionCategory::encoder, fx::software_h264());
    REQUIRE(software.rule == PolicyRule::apply_profile_ceilings);
    REQUIRE(software.reasons == std::vector{ReasonCode::software_excluded_by_profile});
}

TEST_CASE("unknown codec limits stay within the conservative ceiling") {
    const auto plan = plan_for(fx::unknown_codec_limits());
    REQUIRE(plan.selected);
    REQUIRE(plan.selected->encoder == fx::hardware_h264());
    REQUIRE(plan.selected->confidence == Confidence::degraded);
    REQUIRE(plan.selected->resolution == Dimensions{1920, 1080});
    REQUIRE(plan.selected->frame_rate == Rational{30, 1});
    REQUIRE(plan.start == StartingQuality{{1920, 1080}, Rational{30, 1}, 8, false});
    REQUIRE(plan.trace.records.front().reasons.front() == ReasonCode::unknown_limits);
    REQUIRE(plan.envelope.confidence == Confidence::degraded);
}

TEST_CASE("a missing microphone does not change the video plan") {
    REQUIRE(validate(fx::no_microphone()).ok());
    REQUIRE(plan_for(fx::no_microphone()) == plan_for(fx::valid_snapshot()));
}

TEST_CASE("remote sessions plan inside the safe envelope") {
    const auto plan = plan_for(fx::remote_session());
    REQUIRE(plan.profile == ProfileDecision{OperatingProfile::safe_local_envelope, ProfileRule::constrained_session});
    REQUIRE(plan.selected);
    REQUIRE(plan.selected->encoder == fx::hardware_h264());
    REQUIRE(plan.start == StartingQuality{{1280, 720}, Rational{30, 1}, 8, false});
    REQUIRE(plan.fallbacks.empty());
    REQUIRE(record_for(plan, DecisionCategory::encoder, fx::software_h264()).reasons ==
            std::vector{ReasonCode::software_excluded_by_profile});
}

TEST_CASE("headless sessions report no viable local path") {
    const auto plan = plan_for(fx::headless_session());
    REQUIRE(plan.status == PlanStatus::no_viable_path);
    REQUIRE(plan.reasons == std::vector{ReasonCode::no_source_display});
    REQUIRE_FALSE(plan.selected);
    REQUIRE(plan.fallbacks.empty());
    REQUIRE(plan.downgrades.empty());
}

TEST_CASE("mixed refresh displays keep exact rational rates") {
    const auto snapshot = fx::mixed_refresh_desktop();

    const auto ntsc = plan_for(snapshot, request_for(fx::extra_display(2)));
    REQUIRE(ntsc.selected);
    REQUIRE(ntsc.selected->frame_rate == Rational{60'000, 1'001});
    REQUIRE(ntsc.start == StartingQuality{{1920, 1080}, Rational{60'000, 1'001}, 8, false});

    const auto sixty = plan_for(snapshot, request_for(fx::extra_display(3)));
    REQUIRE(sixty.start.frame_rate == Rational{60, 1});

    const auto fast = plan_for(snapshot, request_for(fx::extra_display(4), Rational{144, 1}));
    REQUIRE(fast.start == StartingQuality{{1920, 1080}, Rational{120, 1}, 8, false});

    const auto primary = plan_for(snapshot);
    REQUIRE(primary.selected->frame_rate == Rational{144, 1});
    REQUIRE(primary.start.frame_rate == Rational{60, 1});

    const auto missing = plan_for(snapshot, request_for(DisplayId{"display:missing", IdentityScope::os_session}));
    REQUIRE(missing.status == PlanStatus::no_viable_path);
    REQUIRE(missing.reasons == std::vector{ReasonCode::no_source_display});
}

TEST_CASE("a partial probe failure reports no viable path with degraded confidence") {
    const auto plan = plan_for(fx::partial_probe_failure());
    REQUIRE(plan.status == PlanStatus::no_viable_path);
    REQUIRE(plan.reasons == std::vector{ReasonCode::no_encoder_mode});
    REQUIRE_FALSE(plan.selected);
    REQUIRE_FALSE(plan.envelope.viable);
    REQUIRE(plan.envelope.confidence == Confidence::degraded);
}

TEST_CASE("invalid input is rejected before any planning") {
    const auto rejected = [](const MediaPlan& plan, ReasonCode reason) {
        REQUIRE(plan.status == PlanStatus::invalid_input);
        REQUIRE(plan.reasons == std::vector{reason});
        REQUIRE_FALSE(plan.selected);
        REQUIRE(plan.trace.records.size() == 1);
        const auto& record = plan.trace.records.front();
        REQUIRE(record.category == DecisionCategory::input);
        REQUIRE(record.rule == PolicyRule::reject_invalid_input);
        REQUIRE(record.reasons == std::vector{reason});
    };

    const PolicyVersion future{static_cast<std::uint16_t>(kPolicyVersion.major + 1), 0, 0};
    const auto unsupported_policy = derive_media_plan(fx::valid_snapshot(), fx::display_request(), future);
    rejected(unsupported_policy, ReasonCode::unsupported_policy_version);
    REQUIRE(unsupported_policy.policy_version == future);

    auto future_schema = fx::valid_snapshot();
    future_schema.header.schema_version = SchemaVersion{static_cast<std::uint16_t>(kSchemaVersion.major + 1), 0};
    rejected(plan_for(future_schema), ReasonCode::unsupported_schema);

    auto duplicate = fx::valid_snapshot();
    duplicate.devices.encoders.push_back(duplicate.devices.encoders.front());
    rejected(plan_for(duplicate), ReasonCode::invalid_snapshot);

    auto still = fx::display_request();
    still.quality.frame_rate = Rational{0, 1};
    rejected(plan_for(fx::valid_snapshot(), still), ReasonCode::invalid_request);

    auto empty = fx::display_request();
    empty.quality.resolution = Dimensions{0, 1080};
    rejected(plan_for(fx::valid_snapshot(), empty), ReasonCode::invalid_request);
}

TEST_CASE("plans do not depend on inventory order") {
    const std::vector<std::pair<CapabilitySnapshot, MediaDecisionRequest>> cases{
        {fx::valid_snapshot(), fx::display_request()},
        {fx::hybrid_laptop(), fx::display_request()},
        {fx::hdr_desktop(), [] {
             auto request = fx::display_request();
             request.quality.hdr = true;
             return request;
         }()},
        {fx::apple_silicon_macbook(), fx::display_request()},
        {fx::older_intel_mac(), fx::display_request()},
        {fx::mixed_refresh_desktop(), request_for(fx::extra_display(2))},
        {fx::missing_gpu_driver(), fx::display_request()},
        {fx::crowded_desktop(40), fx::display_request()},
    };
    for (const auto& [snapshot, request] : cases) {
        const auto expected = plan_for(snapshot, request);
        for (std::uint32_t seed = 1; seed <= 8; ++seed) {
            REQUIRE(plan_for(shuffled(snapshot, seed), request) == expected);
        }
    }
}

TEST_CASE("large inventories truncate deterministically within the trace bounds") {
    const auto plan = plan_for(fx::crowded_desktop(40));
    REQUIRE(plan.selected);
    REQUIRE(plan.selected->encoder == fx::hardware_h264());
    REQUIRE(plan.fallbacks.size() == kMaxTraceCandidates - 1);
    REQUIRE(plan.trace.records.size() <= kMaxTraceRecords);

    const auto paths = std::ranges::count(plan.trace.records, DecisionCategory::path, &TraceRecord::category);
    REQUIRE(static_cast<std::size_t>(paths) == kMaxTraceCandidates);
    REQUIRE(plan.trace.records.front().outcome == CandidateOutcome::selected);
    // 41 hardware paths and one software path compete for 32 retained entries.
    REQUIRE(plan.trace.truncated == std::vector{CategoryTruncation{DecisionCategory::path, 10}});
}

TEST_CASE("reason lists truncate but keep the decisive reason first") {
    auto snapshot = fx::hdr_desktop();
    auto& software = *std::ranges::find(snapshot.devices.encoders, fx::software_h264(), &EncoderCapability::id);
    software.codec = Codec::av1;
    software.support = fx::support_unknown(fx::kEncoderProbe, IssueCode::not_reported);
    auto& mode = software.modes.front();
    mode.codec = Codec::av1;
    mode.profile = fx::known(CodecProfile::av1_main, fx::advertised(fx::kEncoderProbe));
    mode.support = fx::support_unknown(fx::kEncoderProbe, IssueCode::not_reported);
    REQUIRE(validate(snapshot).ok());

    auto request = fx::display_request();
    request.quality.hdr = true;
    const auto plan = plan_for(snapshot, request);
    REQUIRE(plan.selected);
    REQUIRE(plan.selected->encoder == fx::hardware_hevc());

    // Nine drawbacks: evidence, limits, affinity, transfer, software, latency, SDR, quality, codec.
    const auto& record = record_for(plan, DecisionCategory::path, fx::software_h264());
    REQUIRE(record.outcome == CandidateOutcome::fallback);
    REQUIRE(record.reasons.size() == kMaxTraceReasons);
    REQUIRE(record.omitted_reasons == 1);
    REQUIRE(record.reasons.front() == ReasonCode::unknown_support);
    REQUIRE(record.rule == PolicyRule::prefer_known_evidence);
}
