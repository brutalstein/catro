#pragma once

#include <catro/capabilities/evidence.hpp>
#include <catro/capabilities/ids.hpp>
#include <catro/capabilities/rational.hpp>
#include <catro/capabilities/version.hpp>

#include <chrono>
#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Authoritative, platform-independent capability model. It contains no native OS, UI,
// graphics, or serialization types; platform adapters translate into these values.
namespace catro::capabilities {

// Microseconds since the Unix epoch, UTC. Recorded by probes, never read by serializers.
using UtcTimestamp = std::chrono::sys_time<std::chrono::microseconds>;

struct Bytes {
    std::uint64_t value = 0;

    friend auto operator<=>(const Bytes&, const Bytes&) = default;
};

struct Dimensions {
    std::uint32_t width = 0;
    std::uint32_t height = 0;

    friend bool operator==(const Dimensions&, const Dimensions&) = default;
};

struct DimensionRange {
    Dimensions minimum;
    Dimensions maximum;

    friend bool operator==(const DimensionRange&, const DimensionRange&) = default;
};

struct RationalRange {
    Rational minimum;
    Rational maximum;

    friend bool operator==(const RationalRange&, const RationalRange&) = default;
};

// ---------------------------------------------------------------------------------------------
// Platform identity

enum class OperatingSystem {
    windows,
    macos,
};

struct OsVersion {
    std::uint32_t major = 0;
    std::uint32_t minor = 0;
    std::uint32_t build = 0;

    friend bool operator==(const OsVersion&, const OsVersion&) = default;
};

struct PlatformIdentity {
    OperatingSystem os = OperatingSystem::windows;
    Observed<OsVersion> version;

    friend bool operator==(const PlatformIdentity&, const PlatformIdentity&) = default;
};

// ---------------------------------------------------------------------------------------------
// Mostly static hardware facts

enum class CpuArchitecture {
    x86_64,
    arm64,
};

// Whether the process runs under binary translation (Rosetta, Windows x64 emulation).
enum class TranslationState {
    native,
    translated,
};

enum class SimdFeature {
    sse4_2,
    avx,
    avx2,
    avx512f,
    neon,
};

enum class PlatformRole {
    desktop,
    mobile,
    server,
};

struct CpuCapability {
    Observed<CpuArchitecture> native_architecture;
    Observed<CpuArchitecture> process_architecture;
    Observed<TranslationState> translation;
    Observed<std::uint32_t> physical_cores;
    Observed<std::uint32_t> logical_cores;
    Observed<std::uint32_t> performance_cores;
    Observed<std::uint32_t> efficiency_cores;
    Observed<std::vector<SimdFeature>> simd;

    friend bool operator==(const CpuCapability&, const CpuCapability&) = default;
};

struct HardwareCapabilities {
    CpuCapability cpu;
    Observed<Bytes> installed_memory;
    Observed<PlatformRole> platform_role;

    friend bool operator==(const HardwareCapabilities&, const HardwareCapabilities&) = default;
};

// ---------------------------------------------------------------------------------------------
// GPUs and displays

enum class GpuKind {
    integrated,
    discrete,
    external,
    software,
};

enum class GraphicsApi {
    direct3d11,
    direct3d12,
    metal,
};

struct GpuCapability {
    GpuId id;
    // PCI identity for diagnostics and future measured compatibility entries only.
    // Policy never treats vendor, device, or name as performance evidence.
    Observed<std::uint32_t> vendor_id;
    Observed<std::uint32_t> device_id;
    Observed<std::string> name;
    Observed<GpuKind> kind;
    Observed<Bytes> dedicated_memory;
    Observed<Bytes> shared_memory;
    Observed<bool> unified_memory;
    Observed<std::vector<GraphicsApi>> graphics_apis;
    // Operating-system adapter preference rankings, not Catro performance judgments.
    Observed<bool> preferred_for_minimum_power;
    Observed<bool> preferred_for_high_performance;

    friend bool operator==(const GpuCapability&, const GpuCapability&) = default;
};

enum class ColorGamut {
    srgb,
    display_p3,
    bt2020,
};

struct DisplayMode {
    Dimensions pixels;
    Dimensions logical;
    Rational refresh_rate;

    friend bool operator==(const DisplayMode&, const DisplayMode&) = default;
};

struct DisplayCapability {
    DisplayId id;
    // Known only when a documented relationship proves it; never guessed from display order.
    Observed<GpuId> gpu;
    Observed<std::vector<DisplayMode>> modes;
    SupportFact hdr;
    Observed<ColorGamut> gamut;
    Observed<std::uint8_t> bits_per_channel;

    friend bool operator==(const DisplayCapability&, const DisplayCapability&) = default;
};

// ---------------------------------------------------------------------------------------------
// Encoders

enum class Codec {
    h264,
    hevc,
    av1,
};

enum class CodecProfile {
    h264_baseline,
    h264_main,
    h264_high,
    hevc_main,
    hevc_main10,
    av1_main,
};

enum class EncoderBackend {
    media_foundation,
    video_toolbox,
};

enum class ImplementationClass {
    hardware,
    software,
};

enum class PixelFormat {
    bgra8,
    rgba16f,
    nv12,
    p010,
};

enum class ChromaSubsampling {
    yuv420,
    yuv422,
    yuv444,
};

enum class ColorRange {
    limited,
    full,
};

enum class TransferFunction {
    srgb,
    bt709,
    pq,
    hlg,
};

enum class HdrMode {
    sdr,
    hdr10,
    hlg,
};

// One concrete advertised mode. Codec support never implies that every mode works, and unknown
// limits stay unknown rather than defaulting to a permissive range.
struct EncoderModeCapability {
    Codec codec = Codec::h264;
    Observed<CodecProfile> profile;
    Observed<DimensionRange> dimensions;
    Observed<RationalRange> frame_rates;
    PixelFormat input_format = PixelFormat::nv12;
    ChromaSubsampling chroma = ChromaSubsampling::yuv420;
    Observed<std::uint8_t> bit_depth;
    Observed<ColorRange> color_range;
    Observed<TransferFunction> transfer_function;
    Observed<HdrMode> hdr;
    SupportFact low_latency;
    SupportFact support;

    friend bool operator==(const EncoderModeCapability&, const EncoderModeCapability&) = default;
};

struct EncoderCapability {
    EncoderId id;
    Codec codec = Codec::h264;
    EncoderBackend backend = EncoderBackend::media_foundation;
    ImplementationClass implementation = ImplementationClass::hardware;
    // Absent for software encoders and for hardware whose adapter cannot be proven.
    Observed<GpuId> gpu;
    Observed<std::string> name;
    SupportFact support;
    std::vector<EncoderModeCapability> modes;

    friend bool operator==(const EncoderCapability&, const EncoderCapability&) = default;
};

// ---------------------------------------------------------------------------------------------
// Capture paths and capture-to-encoder transfers

enum class CaptureApi {
    windows_graphics_capture,
    desktop_duplication,
    screen_capture_kit,
};

enum class SourceKind {
    display,
    window,
    application,
};

// Capture capability is source-specific: one path per API and source kind.
struct CapturePathCapability {
    CapturePathId id;
    CaptureApi api = CaptureApi::windows_graphics_capture;
    SourceKind source = SourceKind::display;
    SupportFact support;
    Observed<GpuId> gpu;
    Observed<std::vector<PixelFormat>> output_formats;
    SupportFact hdr_output;
    Observed<RationalRange> frame_rates;

    friend bool operator==(const CapturePathCapability&, const CapturePathCapability&) = default;
};

enum class TransferKind {
    same_resource,
    same_adapter_copy,
    cross_adapter_copy,
    cpu_staging,
    unknown,
};

enum class Conversion {
    pixel_format,
    color_space,
    bit_depth,
    color_range,
    scaling,
    hdr_to_sdr,
};

// Zero-copy is a property of a capture-to-encoder relationship, never of an encoder alone.
struct TransferPathCapability {
    CapturePathId source;
    EncoderId destination;
    std::optional<GpuId> source_gpu;
    std::optional<GpuId> destination_gpu;
    TransferKind transfer = TransferKind::unknown;
    Observed<std::vector<Conversion>> conversions;
    SupportFact evidence;

    friend bool operator==(const TransferPathCapability&, const TransferPathCapability&) = default;
};

// ---------------------------------------------------------------------------------------------
// Audio endpoints (enumerated only; never opened)

enum class AudioDirection {
    input,
    output,
};

enum class SampleFormat {
    pcm_s16,
    pcm_s24,
    pcm_s32,
    pcm_f32,
};

struct AudioEndpointCapability {
    AudioEndpointId id;
    AudioDirection direction = AudioDirection::input;
    Observed<std::string> name;
    Observed<std::uint32_t> channels;
    Observed<std::uint32_t> sample_rate_hz;
    Observed<std::vector<SampleFormat>> sample_formats;

    friend bool operator==(const AudioEndpointCapability&, const AudioEndpointCapability&) = default;
};

struct DeviceInventory {
    std::vector<GpuCapability> gpus;
    std::vector<EncoderCapability> encoders;
    std::vector<CapturePathCapability> capture_paths;
    std::vector<DisplayCapability> displays;
    std::vector<AudioEndpointCapability> audio_endpoints;
    std::vector<TransferPathCapability> transfer_paths;

    friend bool operator==(const DeviceInventory&, const DeviceInventory&) = default;
};

// ---------------------------------------------------------------------------------------------
// Dynamic runtime state, kept apart from hardware facts and refreshed independently

enum class PowerSource {
    ac,
    battery,
};

enum class ThermalPressure {
    nominal,
    fair,
    serious,
    critical,
};

enum class MemoryPressure {
    normal,
    warning,
    critical,
};

enum class CapturePermission {
    granted,
    denied,
    not_determined,
};

enum class AudioRole {
    console,
    multimedia,
    communications,
};

struct DisplayState {
    DisplayId display;
    Observed<DisplayMode> active_mode;
    Observed<Rational> scale;
    Observed<bool> primary;
    Observed<bool> hdr_enabled;

    friend bool operator==(const DisplayState&, const DisplayState&) = default;
};

struct AudioEndpointState {
    AudioEndpointId endpoint;
    Observed<bool> active;
    Observed<std::vector<AudioRole>> default_roles;

    friend bool operator==(const AudioEndpointState&, const AudioEndpointState&) = default;
};

struct CapturePermissionState {
    CapturePathId path;
    Observed<CapturePermission> permission;

    friend bool operator==(const CapturePermissionState&, const CapturePermissionState&) = default;
};

struct RuntimeState {
    Observed<PowerSource> power_source;
    Observed<bool> battery_present;
    Observed<bool> low_power_mode;
    Observed<ThermalPressure> thermal;
    Observed<MemoryPressure> memory_pressure;
    Observed<bool> remote_session;
    Observed<bool> headless;
    std::vector<DisplayState> displays;
    std::vector<AudioEndpointState> audio_endpoints;
    std::vector<CapturePermissionState> capture_permissions;

    friend bool operator==(const RuntimeState&, const RuntimeState&) = default;
};

// ---------------------------------------------------------------------------------------------
// Probe records

enum class ProbeFamily {
    system,
    runtime,
    gpu_display,
    encoders,
    audio,
};

enum class ProbeOutcome {
    success,
    partial,
    api_unavailable,
    permission_unavailable,
    timeout,
    os_failure,
    malformed_output,
    helper_terminated,
};

// Every scheduled probe produces one record, synthesized by the coordinator when a helper
// times out or terminates before returning output.
struct ProbeRecord {
    std::string probe_id;
    ProbeFamily family = ProbeFamily::system;
    std::uint32_t revision = 0;
    std::chrono::microseconds duration{0};
    ProbeOutcome outcome = ProbeOutcome::success;
    // Raw native code, included only when safe to expose.
    std::optional<std::int64_t> native_error;
    std::uint32_t fact_count = 0;

    friend bool operator==(const ProbeRecord&, const ProbeRecord&) = default;
};

struct ProbeIssue {
    std::string probe_id;
    IssueCode code = IssueCode::os_failure;

    friend bool operator==(const ProbeIssue&, const ProbeIssue&) = default;
};

// ---------------------------------------------------------------------------------------------
// Snapshot

struct SnapshotHeader {
    std::string schema_id{kSchemaId};
    SchemaVersion schema_version = kSchemaVersion;
    // Positive and monotonic only within one capability-service lifetime.
    std::uint64_t generation = 0;
    UtcTimestamp captured_at{};
    std::uint32_t probe_revision = 0;

    friend bool operator==(const SnapshotHeader&, const SnapshotHeader&) = default;
};

struct CapabilitySnapshot {
    SnapshotHeader header;
    PlatformIdentity platform;
    HardwareCapabilities hardware;
    DeviceInventory devices;
    RuntimeState runtime;
    std::vector<ProbeRecord> probes;
    std::vector<ProbeIssue> issues;

    friend bool operator==(const CapabilitySnapshot&, const CapabilitySnapshot&) = default;
};

} // namespace catro::capabilities
