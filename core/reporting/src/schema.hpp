#pragma once

#include <catro/capabilities/media_plan.hpp>
#include <catro/capabilities/model.hpp>
#include <catro/capabilities/probe.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <string_view>
#include <tuple>
#include <type_traits>

// The serialized shape of the capability model, declared once and walked by both the canonical
// JSON codec and the human report. Names are stable schema: changing one is a schema change.
namespace catro::reporting::detail {

namespace caps = catro::capabilities;
using namespace std::string_view_literals;

// Serialized enumerator names, indexed by enumerator value. `last` ties each table to its enum
// so a table that falls out of step with its enum fails to compile.
template <class E>
struct EnumNames;

template <class E>
concept NamedEnum = requires { EnumNames<E>::names; };

template <NamedEnum E>
constexpr std::string_view enum_name(E value) {
    static_assert(EnumNames<E>::names.size() == static_cast<std::size_t>(EnumNames<E>::last) + 1);
    return EnumNames<E>::names[static_cast<std::size_t>(value)];
}

template <NamedEnum E>
std::optional<E> enum_from(std::string_view name) {
    const auto& names = EnumNames<E>::names;
    const auto found = std::ranges::find(names, name);
    if (found == names.end()) {
        return std::nullopt;
    }
    return static_cast<E>(found - names.begin());
}

// clang-format off
template <> struct EnumNames<caps::Knowledge> { static constexpr auto last = caps::Knowledge::unavailable;
    static constexpr std::array names{"known"sv, "unknown"sv, "unavailable"sv}; };
template <> struct EnumNames<caps::Support> { static constexpr auto last = caps::Support::unknown;
    static constexpr std::array names{"supported"sv, "unsupported"sv, "unknown"sv}; };
template <> struct EnumNames<caps::EvidenceMethod> { static constexpr auto last = caps::EvidenceMethod::cached;
    static constexpr std::array names{"measured"sv, "advertised"sv, "probe_validated"sv, "inferred"sv, "cached"sv}; };
template <> struct EnumNames<caps::Confidence> { static constexpr auto last = caps::Confidence::degraded;
    static constexpr std::array names{"high"sv, "degraded"sv}; };
template <> struct EnumNames<caps::IssueCode> { static constexpr auto last = caps::IssueCode::relationship_unprovable;
    static constexpr std::array names{"api_unavailable"sv, "permission_unavailable"sv, "timeout"sv, "os_failure"sv,
        "malformed_output"sv, "helper_terminated"sv, "not_reported"sv, "not_applicable"sv, "relationship_unprovable"sv}; };
template <> struct EnumNames<caps::IdentityScope> { static constexpr auto last = caps::IdentityScope::persistent;
    static constexpr std::array names{"snapshot"sv, "service_lifetime"sv, "os_session"sv, "persistent"sv}; };
template <> struct EnumNames<caps::OperatingSystem> { static constexpr auto last = caps::OperatingSystem::macos;
    static constexpr std::array names{"windows"sv, "macos"sv}; };
template <> struct EnumNames<caps::CpuArchitecture> { static constexpr auto last = caps::CpuArchitecture::arm64;
    static constexpr std::array names{"x86_64"sv, "arm64"sv}; };
template <> struct EnumNames<caps::TranslationState> { static constexpr auto last = caps::TranslationState::translated;
    static constexpr std::array names{"native"sv, "translated"sv}; };
template <> struct EnumNames<caps::SimdFeature> { static constexpr auto last = caps::SimdFeature::neon;
    static constexpr std::array names{"sse4_2"sv, "avx"sv, "avx2"sv, "avx512f"sv, "neon"sv}; };
template <> struct EnumNames<caps::PlatformRole> { static constexpr auto last = caps::PlatformRole::server;
    static constexpr std::array names{"desktop"sv, "mobile"sv, "server"sv}; };
template <> struct EnumNames<caps::GpuKind> { static constexpr auto last = caps::GpuKind::software;
    static constexpr std::array names{"integrated"sv, "discrete"sv, "external"sv, "software"sv}; };
template <> struct EnumNames<caps::GraphicsApi> { static constexpr auto last = caps::GraphicsApi::metal;
    static constexpr std::array names{"direct3d11"sv, "direct3d12"sv, "metal"sv}; };
template <> struct EnumNames<caps::ColorGamut> { static constexpr auto last = caps::ColorGamut::bt2020;
    static constexpr std::array names{"srgb"sv, "display_p3"sv, "bt2020"sv}; };
template <> struct EnumNames<caps::Codec> { static constexpr auto last = caps::Codec::av1;
    static constexpr std::array names{"h264"sv, "hevc"sv, "av1"sv}; };
template <> struct EnumNames<caps::CodecProfile> { static constexpr auto last = caps::CodecProfile::av1_main;
    static constexpr std::array names{"h264_baseline"sv, "h264_main"sv, "h264_high"sv, "hevc_main"sv, "hevc_main10"sv,
        "av1_main"sv}; };
template <> struct EnumNames<caps::EncoderBackend> { static constexpr auto last = caps::EncoderBackend::video_toolbox;
    static constexpr std::array names{"media_foundation"sv, "video_toolbox"sv}; };
template <> struct EnumNames<caps::ImplementationClass> { static constexpr auto last = caps::ImplementationClass::software;
    static constexpr std::array names{"hardware"sv, "software"sv}; };
template <> struct EnumNames<caps::PixelFormat> { static constexpr auto last = caps::PixelFormat::p010;
    static constexpr std::array names{"bgra8"sv, "rgba16f"sv, "nv12"sv, "p010"sv}; };
template <> struct EnumNames<caps::ChromaSubsampling> { static constexpr auto last = caps::ChromaSubsampling::yuv444;
    static constexpr std::array names{"yuv420"sv, "yuv422"sv, "yuv444"sv}; };
template <> struct EnumNames<caps::ColorRange> { static constexpr auto last = caps::ColorRange::full;
    static constexpr std::array names{"limited"sv, "full"sv}; };
template <> struct EnumNames<caps::TransferFunction> { static constexpr auto last = caps::TransferFunction::hlg;
    static constexpr std::array names{"srgb"sv, "bt709"sv, "pq"sv, "hlg"sv}; };
template <> struct EnumNames<caps::HdrMode> { static constexpr auto last = caps::HdrMode::hlg;
    static constexpr std::array names{"sdr"sv, "hdr10"sv, "hlg"sv}; };
template <> struct EnumNames<caps::CaptureApi> { static constexpr auto last = caps::CaptureApi::screen_capture_kit;
    static constexpr std::array names{"windows_graphics_capture"sv, "desktop_duplication"sv, "screen_capture_kit"sv}; };
template <> struct EnumNames<caps::SourceKind> { static constexpr auto last = caps::SourceKind::application;
    static constexpr std::array names{"display"sv, "window"sv, "application"sv}; };
template <> struct EnumNames<caps::TransferKind> { static constexpr auto last = caps::TransferKind::unknown;
    static constexpr std::array names{"same_resource"sv, "same_adapter_copy"sv, "cross_adapter_copy"sv, "cpu_staging"sv,
        "unknown"sv}; };
template <> struct EnumNames<caps::Conversion> { static constexpr auto last = caps::Conversion::hdr_to_sdr;
    static constexpr std::array names{"pixel_format"sv, "color_space"sv, "bit_depth"sv, "color_range"sv, "scaling"sv,
        "hdr_to_sdr"sv}; };
template <> struct EnumNames<caps::AudioDirection> { static constexpr auto last = caps::AudioDirection::output;
    static constexpr std::array names{"input"sv, "output"sv}; };
template <> struct EnumNames<caps::SampleFormat> { static constexpr auto last = caps::SampleFormat::pcm_f32;
    static constexpr std::array names{"pcm_s16"sv, "pcm_s24"sv, "pcm_s32"sv, "pcm_f32"sv}; };
template <> struct EnumNames<caps::PowerSource> { static constexpr auto last = caps::PowerSource::battery;
    static constexpr std::array names{"ac"sv, "battery"sv}; };
template <> struct EnumNames<caps::ThermalPressure> { static constexpr auto last = caps::ThermalPressure::critical;
    static constexpr std::array names{"nominal"sv, "fair"sv, "serious"sv, "critical"sv}; };
template <> struct EnumNames<caps::MemoryPressure> { static constexpr auto last = caps::MemoryPressure::critical;
    static constexpr std::array names{"normal"sv, "warning"sv, "critical"sv}; };
template <> struct EnumNames<caps::CapturePermission> { static constexpr auto last = caps::CapturePermission::not_determined;
    static constexpr std::array names{"granted"sv, "denied"sv, "not_determined"sv}; };
template <> struct EnumNames<caps::AudioRole> { static constexpr auto last = caps::AudioRole::communications;
    static constexpr std::array names{"console"sv, "multimedia"sv, "communications"sv}; };
template <> struct EnumNames<caps::ProbeFamily> { static constexpr auto last = caps::ProbeFamily::audio;
    static constexpr std::array names{"system"sv, "runtime"sv, "gpu_display"sv, "encoders"sv, "audio"sv}; };
template <> struct EnumNames<caps::ProbeOutcome> { static constexpr auto last = caps::ProbeOutcome::helper_terminated;
    static constexpr std::array names{"success"sv, "partial"sv, "api_unavailable"sv, "permission_unavailable"sv, "timeout"sv,
        "os_failure"sv, "malformed_output"sv, "helper_terminated"sv}; };
template <> struct EnumNames<caps::LatencyClass> { static constexpr auto last = caps::LatencyClass::standard;
    static constexpr std::array names{"interactive"sv, "standard"sv}; };
template <> struct EnumNames<caps::OperatingPreference> { static constexpr auto last = caps::OperatingPreference::efficiency;
    static constexpr std::array names{"automatic"sv, "performance"sv, "balanced"sv, "efficiency"sv}; };
template <> struct EnumNames<caps::OperatingProfile> { static constexpr auto last = caps::OperatingProfile::safe_local_envelope;
    static constexpr std::array names{"performance"sv, "balanced"sv, "efficiency"sv, "thermal_constrained"sv,
        "safe_local_envelope"sv}; };
template <> struct EnumNames<caps::ProfileRule> { static constexpr auto last = caps::ProfileRule::mains_power;
    static constexpr std::array names{"thermal_pressure"sv, "constrained_session"sv, "explicit_preference"sv,
        "battery_or_low_power"sv, "unknown_power_source"sv, "mains_power_desktop"sv, "mains_power"sv}; };
template <> struct EnumNames<caps::ReasonCode> { static constexpr auto last = caps::ReasonCode::stable_order;
    static constexpr std::array names{"no_source_display"sv, "no_capture_path"sv, "no_encoder_mode"sv,
        "limited_by_source"sv, "limited_by_capture"sv, "limited_by_encoder"sv, "limited_by_unknown_limits"sv,
        "limited_by_profile"sv, "hdr_unavailable"sv, "unsupported_policy_version"sv, "unsupported_schema"sv,
        "invalid_snapshot"sv, "invalid_request"sv, "source_kind_mismatch"sv, "capture_permission_denied"sv,
        "hdr_mode_not_requested"sv, "capture_unsupported"sv, "transfer_unsupported"sv, "encoder_unsupported"sv,
        "mode_unsupported"sv, "no_transfer_path"sv, "below_encoder_minimum"sv, "software_excluded_by_profile"sv,
        "unknown_support"sv, "unknown_limits"sv, "gpu_affinity_unknown"sv, "gpu_affinity_broken"sv, "same_adapter_copy"sv,
        "cross_adapter_transfer"sv, "cpu_staging_transfer"sv, "unknown_transfer"sv, "software_encoder"sv,
        "low_latency_unproven"sv, "low_latency_unsupported"sv, "sdr_only"sv, "lower_quality"sv,
        "less_conservative_codec"sv, "stable_order"sv}; };
template <> struct EnumNames<caps::PolicyRule> { static constexpr auto last = caps::PolicyRule::rank_fallbacks;
    static constexpr std::array names{"reject_invalid_input"sv, "hard_request_constraints"sv, "remove_unsupported"sv,
        "prefer_known_evidence"sv, "preserve_gpu_affinity"sv, "prefer_cheaper_transfer"sv, "prefer_low_latency_hardware"sv,
        "derive_operating_profile"sv, "apply_profile_ceilings"sv, "select_conservative_start"sv, "rank_fallbacks"sv}; };
template <> struct EnumNames<caps::Consequence> { static constexpr auto last = caps::Consequence::limited_quality;
    static constexpr std::array names{"cpu_load"sv, "power_draw"sv, "added_latency"sv, "limited_quality"sv}; };
template <> struct EnumNames<caps::DowngradeTrigger> { static constexpr auto last = caps::DowngradeTrigger::thermal_pressure;
    static constexpr std::array names{"battery_power"sv, "thermal_pressure"sv}; };
template <> struct EnumNames<caps::DecisionCategory> { static constexpr auto last = caps::DecisionCategory::path;
    static constexpr std::array names{"input"sv, "capture"sv, "transfer"sv, "encoder"sv, "mode"sv, "path"sv}; };
template <> struct EnumNames<caps::CandidateOutcome> { static constexpr auto last = caps::CandidateOutcome::rejected;
    static constexpr std::array names{"selected"sv, "fallback"sv, "rejected"sv}; };
template <> struct EnumNames<caps::PlanStatus> { static constexpr auto last = caps::PlanStatus::invalid_input;
    static constexpr std::array names{"planned"sv, "no_viable_path"sv, "invalid_input"sv}; };
// clang-format on

// One serialized member of a record.
template <class S, class M>
struct Field {
    std::string_view name;
    M S::*member;
};

template <class S, class M>
constexpr Field<S, M> field(std::string_view name, M S::*member) {
    return {name, member};
}

// Serialized members of each record type, in declaration order.
template <class T>
struct Schema;

template <class T>
concept Record = requires { Schema<std::remove_cvref_t<T>>::fields; };

// clang-format off
template <class Tag> struct Schema<caps::ScopedId<Tag>> { static constexpr auto fields = std::tuple{
    field("value", &caps::ScopedId<Tag>::value), field("scope", &caps::ScopedId<Tag>::scope)}; };
template <> struct Schema<caps::Provenance> { static constexpr auto fields = std::tuple{
    field("probe_id", &caps::Provenance::probe_id), field("method", &caps::Provenance::method),
    field("confidence", &caps::Provenance::confidence), field("issue", &caps::Provenance::issue)}; };
template <> struct Schema<caps::SupportFact> { static constexpr auto fields = std::tuple{
    field("status", &caps::SupportFact::status), field("provenance", &caps::SupportFact::provenance)}; };
template <> struct Schema<caps::SchemaVersion> { static constexpr auto fields = std::tuple{
    field("major", &caps::SchemaVersion::major), field("minor", &caps::SchemaVersion::minor)}; };
template <> struct Schema<caps::PolicyVersion> { static constexpr auto fields = std::tuple{
    field("major", &caps::PolicyVersion::major), field("minor", &caps::PolicyVersion::minor),
    field("patch", &caps::PolicyVersion::patch)}; };
template <> struct Schema<caps::Dimensions> { static constexpr auto fields = std::tuple{
    field("width", &caps::Dimensions::width), field("height", &caps::Dimensions::height)}; };
template <> struct Schema<caps::DimensionRange> { static constexpr auto fields = std::tuple{
    field("minimum", &caps::DimensionRange::minimum), field("maximum", &caps::DimensionRange::maximum)}; };
template <> struct Schema<caps::RationalRange> { static constexpr auto fields = std::tuple{
    field("minimum", &caps::RationalRange::minimum), field("maximum", &caps::RationalRange::maximum)}; };
template <> struct Schema<caps::OsVersion> { static constexpr auto fields = std::tuple{
    field("major", &caps::OsVersion::major), field("minor", &caps::OsVersion::minor),
    field("build", &caps::OsVersion::build)}; };
template <> struct Schema<caps::PlatformIdentity> { static constexpr auto fields = std::tuple{
    field("os", &caps::PlatformIdentity::os), field("version", &caps::PlatformIdentity::version)}; };
template <> struct Schema<caps::CpuCapability> { static constexpr auto fields = std::tuple{
    field("native_architecture", &caps::CpuCapability::native_architecture),
    field("process_architecture", &caps::CpuCapability::process_architecture),
    field("translation", &caps::CpuCapability::translation),
    field("physical_cores", &caps::CpuCapability::physical_cores),
    field("logical_cores", &caps::CpuCapability::logical_cores),
    field("performance_cores", &caps::CpuCapability::performance_cores),
    field("efficiency_cores", &caps::CpuCapability::efficiency_cores),
    field("simd", &caps::CpuCapability::simd)}; };
template <> struct Schema<caps::HardwareCapabilities> { static constexpr auto fields = std::tuple{
    field("cpu", &caps::HardwareCapabilities::cpu),
    field("installed_memory", &caps::HardwareCapabilities::installed_memory),
    field("platform_role", &caps::HardwareCapabilities::platform_role)}; };
template <> struct Schema<caps::GpuCapability> { static constexpr auto fields = std::tuple{
    field("id", &caps::GpuCapability::id), field("vendor_id", &caps::GpuCapability::vendor_id),
    field("device_id", &caps::GpuCapability::device_id), field("name", &caps::GpuCapability::name),
    field("kind", &caps::GpuCapability::kind), field("dedicated_memory", &caps::GpuCapability::dedicated_memory),
    field("shared_memory", &caps::GpuCapability::shared_memory),
    field("unified_memory", &caps::GpuCapability::unified_memory),
    field("graphics_apis", &caps::GpuCapability::graphics_apis),
    field("preferred_for_minimum_power", &caps::GpuCapability::preferred_for_minimum_power),
    field("preferred_for_high_performance", &caps::GpuCapability::preferred_for_high_performance)}; };
template <> struct Schema<caps::DisplayMode> { static constexpr auto fields = std::tuple{
    field("pixels", &caps::DisplayMode::pixels), field("logical", &caps::DisplayMode::logical),
    field("refresh_rate", &caps::DisplayMode::refresh_rate)}; };
template <> struct Schema<caps::DisplayCapability> { static constexpr auto fields = std::tuple{
    field("id", &caps::DisplayCapability::id), field("gpu", &caps::DisplayCapability::gpu),
    field("modes", &caps::DisplayCapability::modes), field("hdr", &caps::DisplayCapability::hdr),
    field("gamut", &caps::DisplayCapability::gamut),
    field("bits_per_channel", &caps::DisplayCapability::bits_per_channel)}; };
template <> struct Schema<caps::EncoderModeCapability> { static constexpr auto fields = std::tuple{
    field("codec", &caps::EncoderModeCapability::codec), field("profile", &caps::EncoderModeCapability::profile),
    field("dimensions", &caps::EncoderModeCapability::dimensions),
    field("frame_rates", &caps::EncoderModeCapability::frame_rates),
    field("input_format", &caps::EncoderModeCapability::input_format),
    field("chroma", &caps::EncoderModeCapability::chroma), field("bit_depth", &caps::EncoderModeCapability::bit_depth),
    field("color_range", &caps::EncoderModeCapability::color_range),
    field("transfer_function", &caps::EncoderModeCapability::transfer_function),
    field("hdr", &caps::EncoderModeCapability::hdr), field("low_latency", &caps::EncoderModeCapability::low_latency),
    field("support", &caps::EncoderModeCapability::support)}; };
template <> struct Schema<caps::EncoderModeKey> { static constexpr auto fields = std::tuple{
    field("input_format", &caps::EncoderModeKey::input_format), field("chroma", &caps::EncoderModeKey::chroma),
    field("profile", &caps::EncoderModeKey::profile), field("bit_depth", &caps::EncoderModeKey::bit_depth),
    field("hdr", &caps::EncoderModeKey::hdr)}; };
template <> struct Schema<caps::EncoderCapability> { static constexpr auto fields = std::tuple{
    field("id", &caps::EncoderCapability::id), field("codec", &caps::EncoderCapability::codec),
    field("backend", &caps::EncoderCapability::backend),
    field("implementation", &caps::EncoderCapability::implementation), field("gpu", &caps::EncoderCapability::gpu),
    field("name", &caps::EncoderCapability::name), field("support", &caps::EncoderCapability::support),
    field("modes", &caps::EncoderCapability::modes)}; };
template <> struct Schema<caps::CapturePathCapability> { static constexpr auto fields = std::tuple{
    field("id", &caps::CapturePathCapability::id), field("api", &caps::CapturePathCapability::api),
    field("source", &caps::CapturePathCapability::source), field("support", &caps::CapturePathCapability::support),
    field("gpu", &caps::CapturePathCapability::gpu),
    field("output_formats", &caps::CapturePathCapability::output_formats),
    field("hdr_output", &caps::CapturePathCapability::hdr_output),
    field("frame_rates", &caps::CapturePathCapability::frame_rates)}; };
template <> struct Schema<caps::TransferPathCapability> { static constexpr auto fields = std::tuple{
    field("source", &caps::TransferPathCapability::source),
    field("destination", &caps::TransferPathCapability::destination),
    field("source_gpu", &caps::TransferPathCapability::source_gpu),
    field("destination_gpu", &caps::TransferPathCapability::destination_gpu),
    field("transfer", &caps::TransferPathCapability::transfer),
    field("conversions", &caps::TransferPathCapability::conversions),
    field("evidence", &caps::TransferPathCapability::evidence)}; };
template <> struct Schema<caps::AudioEndpointCapability> { static constexpr auto fields = std::tuple{
    field("id", &caps::AudioEndpointCapability::id), field("direction", &caps::AudioEndpointCapability::direction),
    field("name", &caps::AudioEndpointCapability::name), field("channels", &caps::AudioEndpointCapability::channels),
    field("sample_rate_hz", &caps::AudioEndpointCapability::sample_rate_hz),
    field("sample_formats", &caps::AudioEndpointCapability::sample_formats)}; };
template <> struct Schema<caps::DeviceInventory> { static constexpr auto fields = std::tuple{
    field("gpus", &caps::DeviceInventory::gpus), field("encoders", &caps::DeviceInventory::encoders),
    field("capture_paths", &caps::DeviceInventory::capture_paths), field("displays", &caps::DeviceInventory::displays),
    field("audio_endpoints", &caps::DeviceInventory::audio_endpoints),
    field("transfer_paths", &caps::DeviceInventory::transfer_paths)}; };
template <> struct Schema<caps::DisplayState> { static constexpr auto fields = std::tuple{
    field("display", &caps::DisplayState::display), field("active_mode", &caps::DisplayState::active_mode),
    field("scale", &caps::DisplayState::scale), field("primary", &caps::DisplayState::primary),
    field("hdr_enabled", &caps::DisplayState::hdr_enabled)}; };
template <> struct Schema<caps::AudioEndpointState> { static constexpr auto fields = std::tuple{
    field("endpoint", &caps::AudioEndpointState::endpoint), field("active", &caps::AudioEndpointState::active),
    field("default_roles", &caps::AudioEndpointState::default_roles)}; };
template <> struct Schema<caps::CapturePermissionState> { static constexpr auto fields = std::tuple{
    field("path", &caps::CapturePermissionState::path),
    field("permission", &caps::CapturePermissionState::permission)}; };
template <> struct Schema<caps::RuntimeState> { static constexpr auto fields = std::tuple{
    field("power_source", &caps::RuntimeState::power_source),
    field("battery_present", &caps::RuntimeState::battery_present),
    field("low_power_mode", &caps::RuntimeState::low_power_mode), field("thermal", &caps::RuntimeState::thermal),
    field("memory_pressure", &caps::RuntimeState::memory_pressure),
    field("remote_session", &caps::RuntimeState::remote_session), field("headless", &caps::RuntimeState::headless),
    field("displays", &caps::RuntimeState::displays), field("audio_endpoints", &caps::RuntimeState::audio_endpoints),
    field("capture_permissions", &caps::RuntimeState::capture_permissions)}; };
template <> struct Schema<caps::ProbeRecord> { static constexpr auto fields = std::tuple{
    field("probe_id", &caps::ProbeRecord::probe_id), field("family", &caps::ProbeRecord::family),
    field("revision", &caps::ProbeRecord::revision), field("generation", &caps::ProbeRecord::generation),
    field("duration", &caps::ProbeRecord::duration), field("outcome", &caps::ProbeRecord::outcome),
    field("native_error", &caps::ProbeRecord::native_error), field("fact_count", &caps::ProbeRecord::fact_count)}; };
template <> struct Schema<caps::ProbeIssue> { static constexpr auto fields = std::tuple{
    field("probe_id", &caps::ProbeIssue::probe_id), field("code", &caps::ProbeIssue::code)}; };
// The schema identifier and version are serialized once, at the top of the report.
template <> struct Schema<caps::SnapshotHeader> { static constexpr auto fields = std::tuple{
    field("generation", &caps::SnapshotHeader::generation), field("captured_at", &caps::SnapshotHeader::captured_at),
    field("probe_revision", &caps::SnapshotHeader::probe_revision)}; };
template <> struct Schema<caps::CapabilitySnapshot> { static constexpr auto fields = std::tuple{
    field("header", &caps::CapabilitySnapshot::header), field("platform", &caps::CapabilitySnapshot::platform),
    field("hardware", &caps::CapabilitySnapshot::hardware), field("devices", &caps::CapabilitySnapshot::devices),
    field("runtime", &caps::CapabilitySnapshot::runtime), field("probes", &caps::CapabilitySnapshot::probes),
    field("issues", &caps::CapabilitySnapshot::issues)}; };
template <> struct Schema<caps::SystemProbeFacts> { static constexpr auto fields = std::tuple{
    field("platform", &caps::SystemProbeFacts::platform), field("hardware", &caps::SystemProbeFacts::hardware)}; };
template <> struct Schema<caps::RuntimeProbeFacts> { static constexpr auto fields = std::tuple{
    field("power_source", &caps::RuntimeProbeFacts::power_source),
    field("battery_present", &caps::RuntimeProbeFacts::battery_present),
    field("low_power_mode", &caps::RuntimeProbeFacts::low_power_mode), field("thermal", &caps::RuntimeProbeFacts::thermal),
    field("memory_pressure", &caps::RuntimeProbeFacts::memory_pressure),
    field("remote_session", &caps::RuntimeProbeFacts::remote_session), field("headless", &caps::RuntimeProbeFacts::headless)}; };
template <> struct Schema<caps::GpuDisplayProbeFacts> { static constexpr auto fields = std::tuple{
    field("gpus", &caps::GpuDisplayProbeFacts::gpus), field("displays", &caps::GpuDisplayProbeFacts::displays),
    field("capture_paths", &caps::GpuDisplayProbeFacts::capture_paths),
    field("display_states", &caps::GpuDisplayProbeFacts::display_states),
    field("capture_permissions", &caps::GpuDisplayProbeFacts::capture_permissions)}; };
template <> struct Schema<caps::EncoderProbeFacts> { static constexpr auto fields = std::tuple{
    field("encoders", &caps::EncoderProbeFacts::encoders),
    field("transfer_paths", &caps::EncoderProbeFacts::transfer_paths)}; };
template <> struct Schema<caps::AudioProbeFacts> { static constexpr auto fields = std::tuple{
    field("endpoints", &caps::AudioProbeFacts::endpoints), field("states", &caps::AudioProbeFacts::states)}; };
template <> struct Schema<caps::ProbeFragment> { static constexpr auto fields = std::tuple{
    field("schema_id", &caps::ProbeFragment::schema_id), field("schema_version", &caps::ProbeFragment::schema_version),
    field("probe_id", &caps::ProbeFragment::probe_id), field("family", &caps::ProbeFragment::family),
    field("revision", &caps::ProbeFragment::revision), field("duration", &caps::ProbeFragment::duration),
    field("outcome", &caps::ProbeFragment::outcome), field("native_error", &caps::ProbeFragment::native_error),
    field("system", &caps::ProbeFragment::system), field("runtime", &caps::ProbeFragment::runtime),
    field("gpu_display", &caps::ProbeFragment::gpu_display), field("encoders", &caps::ProbeFragment::encoders),
    field("audio", &caps::ProbeFragment::audio), field("issues", &caps::ProbeFragment::issues)}; };
template <> struct Schema<caps::RequestedQuality> { static constexpr auto fields = std::tuple{
    field("resolution", &caps::RequestedQuality::resolution), field("frame_rate", &caps::RequestedQuality::frame_rate),
    field("hdr", &caps::RequestedQuality::hdr)}; };
template <> struct Schema<caps::MediaDecisionRequest> { static constexpr auto fields = std::tuple{
    field("source", &caps::MediaDecisionRequest::source), field("display", &caps::MediaDecisionRequest::display),
    field("latency", &caps::MediaDecisionRequest::latency),
    field("preference", &caps::MediaDecisionRequest::preference),
    field("quality", &caps::MediaDecisionRequest::quality)}; };
template <> struct Schema<caps::ProfileDecision> { static constexpr auto fields = std::tuple{
    field("profile", &caps::ProfileDecision::profile), field("rule", &caps::ProfileDecision::rule)}; };
template <> struct Schema<caps::LocalQualityEnvelope> { static constexpr auto fields = std::tuple{
    field("viable", &caps::LocalQualityEnvelope::viable), field("resolution", &caps::LocalQualityEnvelope::resolution),
    field("frame_rate", &caps::LocalQualityEnvelope::frame_rate),
    field("bit_depth", &caps::LocalQualityEnvelope::bit_depth), field("hdr", &caps::LocalQualityEnvelope::hdr),
    field("confidence", &caps::LocalQualityEnvelope::confidence),
    field("reasons", &caps::LocalQualityEnvelope::reasons)}; };
template <> struct Schema<caps::MediaCandidate> { static constexpr auto fields = std::tuple{
    field("capture", &caps::MediaCandidate::capture), field("transfer", &caps::MediaCandidate::transfer),
    field("gpu", &caps::MediaCandidate::gpu), field("encoder", &caps::MediaCandidate::encoder),
    field("implementation", &caps::MediaCandidate::implementation), field("codec", &caps::MediaCandidate::codec),
    field("mode", &caps::MediaCandidate::mode), field("hdr", &caps::MediaCandidate::hdr),
    field("resolution", &caps::MediaCandidate::resolution), field("frame_rate", &caps::MediaCandidate::frame_rate),
    field("confidence", &caps::MediaCandidate::confidence),
    field("consequences", &caps::MediaCandidate::consequences)}; };
template <> struct Schema<caps::StartingQuality> { static constexpr auto fields = std::tuple{
    field("resolution", &caps::StartingQuality::resolution), field("frame_rate", &caps::StartingQuality::frame_rate),
    field("bit_depth", &caps::StartingQuality::bit_depth), field("hdr", &caps::StartingQuality::hdr)}; };
template <> struct Schema<caps::Downgrade> { static constexpr auto fields = std::tuple{
    field("trigger", &caps::Downgrade::trigger), field("profile", &caps::Downgrade::profile),
    field("quality", &caps::Downgrade::quality)}; };
template <> struct Schema<caps::CandidateRef> { static constexpr auto fields = std::tuple{
    field("capture", &caps::CandidateRef::capture), field("encoder", &caps::CandidateRef::encoder),
    field("mode", &caps::CandidateRef::mode)}; };
template <> struct Schema<caps::TraceRecord> { static constexpr auto fields = std::tuple{
    field("category", &caps::TraceRecord::category), field("outcome", &caps::TraceRecord::outcome),
    field("subject", &caps::TraceRecord::subject), field("rule", &caps::TraceRecord::rule),
    field("reasons", &caps::TraceRecord::reasons), field("omitted_reasons", &caps::TraceRecord::omitted_reasons)}; };
template <> struct Schema<caps::CategoryTruncation> { static constexpr auto fields = std::tuple{
    field("category", &caps::CategoryTruncation::category), field("omitted", &caps::CategoryTruncation::omitted)}; };
template <> struct Schema<caps::DecisionTrace> { static constexpr auto fields = std::tuple{
    field("records", &caps::DecisionTrace::records), field("truncated", &caps::DecisionTrace::truncated)}; };
template <> struct Schema<caps::MediaPlan> { static constexpr auto fields = std::tuple{
    field("schema_version", &caps::MediaPlan::schema_version), field("policy_version", &caps::MediaPlan::policy_version),
    field("status", &caps::MediaPlan::status), field("reasons", &caps::MediaPlan::reasons),
    field("request", &caps::MediaPlan::request), field("profile", &caps::MediaPlan::profile),
    field("envelope", &caps::MediaPlan::envelope), field("selected", &caps::MediaPlan::selected),
    field("start", &caps::MediaPlan::start), field("downgrades", &caps::MediaPlan::downgrades),
    field("fallbacks", &caps::MediaPlan::fallbacks), field("trace", &caps::MediaPlan::trace)}; };
// clang-format on

// Sorts every snapshot collection whose order carries no meaning, so every projection is
// independent of enumeration order. Plan data is already deterministic and keeps rank order.
caps::CapabilitySnapshot canonical_order(caps::CapabilitySnapshot snapshot);
caps::ProbeFragment canonical_order(caps::ProbeFragment fragment);

// Calls visit(name, member) for each serialized member of a record, in schema order.
template <Record T, class Visit>
void for_each_field(T& record, Visit&& visit) {
    std::apply([&](const auto&... fields) { (visit(fields.name, record.*(fields.member)), ...); },
               Schema<std::remove_cvref_t<T>>::fields);
}

} // namespace catro::reporting::detail
