#include <catro/capabilities/policy.hpp>

#include "fixtures/capability_fixtures.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <utility>
#include <vector>

using namespace catro::capabilities;
namespace fx = catro::fixtures;

namespace {

// Sorts after hardware_h264(), so it can only win on a ranking rule, never on the tie-break.
const EncoderId kTwinH264{"mft:h264:hardware:9", IdentityScope::service_lifetime};

bool contains(const std::vector<ReasonCode>& reasons, ReasonCode reason) {
    return std::ranges::find(reasons, reason) != reasons.end();
}

std::vector<EncoderId> fallback_encoders(const MediaPlan& plan) {
    std::vector<EncoderId> encoders;
    for (const auto& candidate : plan.fallbacks) {
        encoders.push_back(candidate.encoder);
    }
    return encoders;
}

const TraceRecord& record_for(const MediaPlan& plan, DecisionCategory category, const EncoderId& encoder) {
    const auto found = std::ranges::find_if(plan.trace.records, [&](const TraceRecord& record) {
        return record.category == category && record.subject.encoder == encoder;
    });
    REQUIRE(found != plan.trace.records.end());
    return *found;
}

// valid_snapshot() plus a copy of the hardware H.264 encoder on the same GPU.
CapabilitySnapshot with_twin_encoder(TransferKind original, TransferKind twin) {
    auto snapshot = fx::valid_snapshot();
    auto& devices = snapshot.devices;
    auto encoder = *std::ranges::find(devices.encoders, fx::hardware_h264(), &EncoderCapability::id);
    auto& transfer = *std::ranges::find(devices.transfer_paths, fx::hardware_h264(), &TransferPathCapability::destination);
    auto twin_transfer = transfer;
    transfer.transfer = original;
    encoder.id = kTwinH264;
    twin_transfer.destination = kTwinH264;
    twin_transfer.transfer = twin;
    devices.encoders.push_back(std::move(encoder));
    devices.transfer_paths.push_back(std::move(twin_transfer));
    return snapshot;
}

MediaPlan plan_for(const CapabilitySnapshot& snapshot, const MediaDecisionRequest& request = fx::display_request()) {
    return derive_media_plan(snapshot, request, kPolicyVersion);
}

MediaDecisionRequest hdr_request() {
    auto request = fx::display_request();
    request.quality.hdr = true;
    return request;
}

} // namespace

TEST_CASE("a coherent desktop selects its same-resource hardware path") {
    const auto plan = plan_for(fx::valid_snapshot());
    REQUIRE(plan.status == PlanStatus::planned);
    REQUIRE(plan.reasons.empty());
    REQUIRE(plan.schema_version == kSchemaVersion);
    REQUIRE(plan.policy_version == kPolicyVersion);
    REQUIRE(plan.request == fx::display_request());
    REQUIRE(plan.profile == ProfileDecision{OperatingProfile::performance, ProfileRule::mains_power_desktop});
    REQUIRE(plan.envelope.viable);

    REQUIRE(plan.selected);
    const auto& selected = *plan.selected;
    REQUIRE(selected.capture == fx::display_capture());
    REQUIRE(selected.transfer == TransferKind::same_resource);
    REQUIRE(selected.gpu == fx::desktop_gpu());
    REQUIRE(selected.encoder == fx::hardware_h264());
    REQUIRE(selected.implementation == ImplementationClass::hardware);
    REQUIRE(selected.codec == Codec::h264);
    REQUIRE(selected.mode.profile == CodecProfile::h264_high);
    REQUIRE(selected.resolution == Dimensions{2560, 1440});
    REQUIRE(selected.frame_rate == Rational{144, 1});
    REQUIRE(selected.confidence == Confidence::high);
    REQUIRE(selected.consequences.empty());

    // The start is the selected path narrowed to the request, never the path's maximum.
    REQUIRE(plan.start == StartingQuality{{1920, 1080}, Rational{60, 1}, 8, false});

    const auto& record = plan.trace.records.front();
    REQUIRE(record.category == DecisionCategory::path);
    REQUIRE(record.outcome == CandidateOutcome::selected);
    REQUIRE(record.rule == PolicyRule::select_conservative_start);
    REQUIRE(record.reasons == std::vector{ReasonCode::low_latency_unproven, ReasonCode::limited_by_source});
}

TEST_CASE("software encoding stays an explicit fallback with stated consequences") {
    const auto plan = plan_for(fx::valid_snapshot());
    REQUIRE(fallback_encoders(plan) == std::vector{fx::software_h264()});

    const auto& software = plan.fallbacks.front();
    REQUIRE(software.implementation == ImplementationClass::software);
    REQUIRE(software.transfer == TransferKind::cpu_staging);
    REQUIRE_FALSE(software.gpu);
    REQUIRE(software.consequences == std::vector{Consequence::cpu_load, Consequence::power_draw, Consequence::added_latency,
                                                 Consequence::limited_quality});
    REQUIRE(software.confidence == Confidence::degraded);
    REQUIRE(software.resolution == Dimensions{1920, 1080});
    REQUIRE(software.frame_rate == Rational{30, 1});

    const auto& record = record_for(plan, DecisionCategory::path, fx::software_h264());
    REQUIRE(record.outcome == CandidateOutcome::fallback);
    REQUIRE(record.rule == PolicyRule::prefer_known_evidence);
    REQUIRE(record.reasons == std::vector{ReasonCode::unknown_limits, ReasonCode::gpu_affinity_unknown,
                                          ReasonCode::cpu_staging_transfer, ReasonCode::software_encoder,
                                          ReasonCode::low_latency_unproven, ReasonCode::lower_quality});
}

TEST_CASE("a same-resource path beats a same-adapter copy") {
    const auto plan = plan_for(with_twin_encoder(TransferKind::same_adapter_copy, TransferKind::same_resource));
    REQUIRE(plan.selected);
    REQUIRE(plan.selected->encoder == kTwinH264);
    REQUIRE(plan.selected->transfer == TransferKind::same_resource);

    const auto& original = record_for(plan, DecisionCategory::path, fx::hardware_h264());
    REQUIRE(original.outcome == CandidateOutcome::fallback);
    REQUIRE(original.rule == PolicyRule::prefer_cheaper_transfer);
    REQUIRE(original.reasons == std::vector{ReasonCode::same_adapter_copy});
    REQUIRE(fallback_encoders(plan) == std::vector{fx::hardware_h264(), fx::software_h264()});
}

TEST_CASE("a same-adapter copy beats a cross-adapter copy") {
    auto snapshot = fx::hybrid_laptop();
    std::ranges::find(snapshot.devices.transfer_paths, fx::hybrid_integrated_h264(), &TransferPathCapability::destination)->transfer =
        TransferKind::same_adapter_copy;
    const auto plan = plan_for(snapshot);
    REQUIRE(plan.selected);
    REQUIRE(plan.selected->encoder == fx::hybrid_integrated_h264());
    REQUIRE(plan.selected->transfer == TransferKind::same_adapter_copy);
    REQUIRE(contains(record_for(plan, DecisionCategory::path, fx::hybrid_discrete_h264()).reasons,
                     ReasonCode::cross_adapter_transfer));
}

TEST_CASE("hybrid GPU rejection explains cross-adapter cost") {
    const auto plan = plan_for(fx::hybrid_laptop());
    REQUIRE(plan.selected);
    REQUIRE(plan.selected->transfer == TransferKind::same_resource);
    REQUIRE(plan.selected->encoder == fx::hybrid_integrated_h264());
    REQUIRE(plan.selected->gpu == fx::hybrid_integrated_gpu());
    REQUIRE(plan.trace.has_rejection(ReasonCode::cross_adapter_transfer));
    REQUIRE(fallback_encoders(plan) == std::vector{fx::hybrid_discrete_h264(), fx::hybrid_discrete_av1()});

    const auto& discrete = record_for(plan, DecisionCategory::path, fx::hybrid_discrete_h264());
    REQUIRE(discrete.rule == PolicyRule::preserve_gpu_affinity);
    REQUIRE(discrete.reasons == std::vector{ReasonCode::gpu_affinity_broken, ReasonCode::cross_adapter_transfer});
    const auto& av1 = record_for(plan, DecisionCategory::path, fx::hybrid_discrete_av1());
    REQUIRE(av1.reasons == std::vector{ReasonCode::gpu_affinity_broken, ReasonCode::cross_adapter_transfer,
                                       ReasonCode::less_conservative_codec});
    REQUIRE(plan.fallbacks.front().consequences == std::vector{Consequence::power_draw, Consequence::added_latency});
}

TEST_CASE("measured low-latency hardware beats merely advertised modes for interactive requests") {
    auto snapshot = with_twin_encoder(TransferKind::same_resource, TransferKind::same_resource);
    std::ranges::find(snapshot.devices.encoders, kTwinH264, &EncoderCapability::id)->modes.front().low_latency =
        fx::supported(fx::measured(fx::kEncoderProbe));

    const auto interactive = plan_for(snapshot);
    REQUIRE(interactive.selected);
    REQUIRE(interactive.selected->encoder == kTwinH264);
    const auto& advertised = record_for(interactive, DecisionCategory::path, fx::hardware_h264());
    REQUIRE(advertised.rule == PolicyRule::prefer_low_latency_hardware);
    REQUIRE(advertised.reasons == std::vector{ReasonCode::low_latency_unproven});

    // Latency evidence does not rank standard-latency requests; only the stable order remains.
    auto request = fx::display_request();
    request.latency = LatencyClass::standard;
    const auto standard = plan_for(snapshot, request);
    REQUIRE(standard.selected);
    REQUIRE(standard.selected->encoder == fx::hardware_h264());
    const auto& twin = record_for(standard, DecisionCategory::path, kTwinH264);
    REQUIRE(twin.rule == PolicyRule::rank_fallbacks);
    REQUIRE(twin.reasons == std::vector{ReasonCode::stable_order});
}

TEST_CASE("HDR requests select an HDR-preserving path when the local envelope allows it") {
    const auto plan = plan_for(fx::hdr_desktop(), hdr_request());
    REQUIRE(plan.envelope.hdr);
    REQUIRE(plan.selected);
    REQUIRE(plan.selected->encoder == fx::hardware_hevc());
    REQUIRE(plan.selected->hdr);
    REQUIRE(plan.selected->mode.hdr == HdrMode::hdr10);
    REQUIRE(plan.selected->mode.bit_depth == std::uint8_t{10});
    REQUIRE(plan.start == StartingQuality{{1920, 1080}, Rational{60, 1}, 10, true});
    REQUIRE(record_for(plan, DecisionCategory::path, fx::hardware_h264()).reasons.front() == ReasonCode::sdr_only);
}

TEST_CASE("HDR modes are rejected when HDR is not requested or not achievable") {
    const auto sdr = plan_for(fx::hdr_desktop());
    REQUIRE(sdr.selected);
    REQUIRE(sdr.selected->encoder == fx::hardware_h264());
    REQUIRE_FALSE(sdr.selected->hdr);
    const auto& not_requested = record_for(sdr, DecisionCategory::mode, fx::hardware_hevc());
    REQUIRE(not_requested.outcome == CandidateOutcome::rejected);
    REQUIRE(not_requested.subject.mode->hdr == HdrMode::hdr10);
    REQUIRE(not_requested.rule == PolicyRule::hard_request_constraints);
    REQUIRE(not_requested.reasons == std::vector{ReasonCode::hdr_mode_not_requested});

    // HDR is off on this display, so an HDR request keeps SDR and says why.
    const auto unavailable = plan_for(fx::high_end_desktop(), hdr_request());
    REQUIRE_FALSE(unavailable.envelope.hdr);
    REQUIRE(contains(unavailable.envelope.reasons, ReasonCode::hdr_unavailable));
    REQUIRE(unavailable.selected);
    REQUIRE_FALSE(unavailable.selected->hdr);
    REQUIRE(unavailable.start.bit_depth == 8);
    REQUIRE(record_for(unavailable, DecisionCategory::mode, fx::hardware_hevc()).reasons ==
            std::vector{ReasonCode::hdr_unavailable});
}

TEST_CASE("the operating profile bounds the start and the downgrade behaviour") {
    const auto battery = plan_for(fx::intel_laptop_on_battery());
    REQUIRE(battery.profile.profile == OperatingProfile::efficiency);
    REQUIRE(battery.selected);
    REQUIRE(battery.selected->codec == Codec::h264);
    REQUIRE(battery.selected->implementation == ImplementationClass::hardware);
    REQUIRE(battery.start == StartingQuality{{1728, 1080}, Rational{30, 1}, 8, false});

    const auto desktop = plan_for(fx::valid_snapshot());
    REQUIRE(desktop.downgrades ==
            std::vector{Downgrade{DowngradeTrigger::battery_power, OperatingProfile::efficiency,
                                  StartingQuality{{1920, 1080}, Rational{30, 1}, 8, false}},
                        Downgrade{DowngradeTrigger::thermal_pressure, OperatingProfile::thermal_constrained,
                                  StartingQuality{{1280, 720}, Rational{30, 1}, 8, false}}});

    const auto hot = plan_for(fx::hot_apple_silicon());
    REQUIRE(hot.profile.profile == OperatingProfile::thermal_constrained);
    REQUIRE(hot.selected);
    REQUIRE(hot.start.resolution.height <= 720);
    REQUIRE(hot.start.frame_rate == Rational{30, 1});
}

TEST_CASE("Apple Silicon keeps unprovable encoder affinity explicit") {
    const auto plan = plan_for(fx::apple_silicon_macbook());
    REQUIRE(plan.profile.profile == OperatingProfile::balanced);
    REQUIRE(plan.selected);
    REQUIRE(plan.selected->encoder == fx::mac_h264());
    REQUIRE(plan.selected->transfer == TransferKind::same_resource);
    REQUIRE_FALSE(plan.selected->gpu);
    // Categories are ordered input, capture, transfer, encoder, mode, path; the HDR mode
    // rejection precedes the selected path.
    REQUIRE(plan.trace.records.front().category == DecisionCategory::mode);
    const auto& selected = record_for(plan, DecisionCategory::path, fx::mac_h264());
    REQUIRE(selected.outcome == CandidateOutcome::selected);
    REQUIRE(contains(selected.reasons, ReasonCode::gpu_affinity_unknown));
    REQUIRE(record_for(plan, DecisionCategory::mode, fx::mac_hevc()).reasons == std::vector{ReasonCode::hdr_mode_not_requested});
}

TEST_CASE("unprovable transfers degrade confidence on an older Intel Mac") {
    const auto plan = plan_for(fx::older_intel_mac());
    REQUIRE(plan.selected);
    REQUIRE(plan.selected->encoder == fx::mac_h264());
    REQUIRE(plan.selected->transfer == TransferKind::unknown);
    REQUIRE(plan.selected->confidence == Confidence::degraded);
    const auto& reasons = plan.trace.records.front().reasons;
    REQUIRE(reasons.front() == ReasonCode::unknown_support);
    REQUIRE(contains(reasons, ReasonCode::gpu_affinity_unknown));
    REQUIRE(contains(reasons, ReasonCode::unknown_transfer));
}
