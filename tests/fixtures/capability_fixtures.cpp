#include "capability_fixtures.hpp"

#include <algorithm>
#include <chrono>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace catro::fixtures {

using namespace catro::capabilities;

namespace {

constexpr std::uint64_t kGiB = 1024ULL * 1024ULL * 1024ULL;

struct ProbeNames {
    std::string_view system;
    std::string_view runtime;
    std::string_view gpu_display;
    std::string_view encoders;
    std::string_view audio;
};

constexpr ProbeNames kWindowsProbes{kSystemProbe, kRuntimeProbe, kGpuDisplayProbe, kEncoderProbe, kAudioProbe};
constexpr ProbeNames kMacProbes{kMacSystemProbe, kMacRuntimeProbe, kMacGpuDisplayProbe, kMacEncoderProbe, kMacAudioProbe};

Provenance make_provenance(std::string_view probe, EvidenceMethod method) {
    return Provenance{.probe_id = std::string(probe), .method = method};
}

DisplayMode mode(Dimensions pixels, Dimensions logical, Rational refresh_rate) {
    return DisplayMode{.pixels = pixels, .logical = logical, .refresh_rate = refresh_rate};
}

DisplayMode mode_1440p(Rational refresh_rate) {
    return mode({2560, 1440}, {2560, 1440}, refresh_rate);
}

CapabilitySnapshot base_snapshot(OperatingSystem os, OsVersion version, const ProbeNames& probes) {
    CapabilitySnapshot snapshot;
    snapshot.header.generation = 1;
    snapshot.header.captured_at = UtcTimestamp{std::chrono::microseconds{1'790'000'000'000'000}};
    snapshot.header.probe_revision = 1;
    snapshot.probes = {
        probe_record(probes.system, ProbeFamily::system),
        probe_record(probes.runtime, ProbeFamily::runtime),
        probe_record(probes.gpu_display, ProbeFamily::gpu_display),
        probe_record(probes.encoders, ProbeFamily::encoders),
        probe_record(probes.audio, ProbeFamily::audio),
    };
    snapshot.platform = {.os = os, .version = known(version, measured(probes.system))};
    return snapshot;
}

CpuCapability cpu(CpuArchitecture architecture, std::uint32_t physical, std::uint32_t logical,
                  Observed<std::uint32_t> performance, Observed<std::uint32_t> efficiency,
                  std::vector<SimdFeature> simd, std::string_view probe) {
    return CpuCapability{
        .native_architecture = known(architecture, measured(probe)),
        .process_architecture = known(architecture, measured(probe)),
        .translation = known(TranslationState::native, measured(probe)),
        .physical_cores = known(physical, measured(probe)),
        .logical_cores = known(logical, measured(probe)),
        .performance_cores = std::move(performance),
        .efficiency_cores = std::move(efficiency),
        .simd = known(std::move(simd), measured(probe)),
    };
}

struct GpuShape {
    GpuId id;
    GpuKind kind = GpuKind::discrete;
    std::string name;
    std::uint64_t dedicated_gib = 0;
    std::uint64_t shared_gib = 0;
    bool unified = false;
    bool minimum_power = true;
    bool high_performance = true;
};

GpuCapability windows_gpu(const GpuShape& shape) {
    const auto probe = kGpuDisplayProbe;
    return GpuCapability{
        .id = shape.id,
        .vendor_id = known(0x1234U, advertised(probe)),
        .device_id = known(0x5678U, advertised(probe)),
        .name = known(shape.name, advertised(probe)),
        .kind = known(shape.kind, advertised(probe)),
        .dedicated_memory = known(Bytes{shape.dedicated_gib * kGiB}, advertised(probe)),
        .shared_memory = known(Bytes{shape.shared_gib * kGiB}, advertised(probe)),
        .unified_memory = known(shape.unified, advertised(probe)),
        .graphics_apis = known(std::vector{GraphicsApi::direct3d11, GraphicsApi::direct3d12}, advertised(probe)),
        .preferred_for_minimum_power = known(shape.minimum_power, advertised(probe)),
        .preferred_for_high_performance = known(shape.high_performance, advertised(probe)),
    };
}

// Metal exposes no PCI identity or adapter preference ranking.
GpuCapability mac_gpu(const GpuShape& shape) {
    const auto probe = kMacGpuDisplayProbe;
    return GpuCapability{
        .id = shape.id,
        .vendor_id = unavailable<std::uint32_t>(probe, IssueCode::not_reported),
        .device_id = unavailable<std::uint32_t>(probe, IssueCode::not_reported),
        .name = known(shape.name, advertised(probe)),
        .kind = known(shape.kind, advertised(probe)),
        .dedicated_memory = shape.unified ? unavailable<Bytes>(probe, IssueCode::not_applicable)
                                          : known(Bytes{shape.dedicated_gib * kGiB}, advertised(probe)),
        .shared_memory = known(Bytes{shape.shared_gib * kGiB}, advertised(probe)),
        .unified_memory = known(shape.unified, advertised(probe)),
        .graphics_apis = known(std::vector{GraphicsApi::metal}, advertised(probe)),
        .preferred_for_minimum_power = unavailable<bool>(probe, IssueCode::not_applicable),
        .preferred_for_high_performance = unavailable<bool>(probe, IssueCode::not_applicable),
    };
}

DisplayCapability display(DisplayId id, Observed<GpuId> gpu, std::vector<DisplayMode> modes, ColorGamut gamut,
                          std::uint8_t bits_per_channel, std::string_view probe) {
    return DisplayCapability{
        .id = std::move(id),
        .gpu = std::move(gpu),
        .modes = known(std::move(modes), measured(probe)),
        .hdr = supported(advertised(probe)),
        .gamut = known(gamut, advertised(probe)),
        .bits_per_channel = known(bits_per_channel, advertised(probe)),
    };
}

DisplayState display_state(DisplayId id, DisplayMode active, Rational scale, std::string_view probe) {
    return DisplayState{
        .display = std::move(id),
        .active_mode = known(active, measured(probe)),
        .scale = known(scale, measured(probe)),
        .primary = known(true, measured(probe)),
        .hdr_enabled = known(false, measured(probe)),
    };
}

CodecProfile profile_for(Codec codec, bool hdr10) {
    switch (codec) {
    case Codec::h264:
        return CodecProfile::h264_high;
    case Codec::hevc:
        return hdr10 ? CodecProfile::hevc_main10 : CodecProfile::hevc_main;
    case Codec::av1:
        return CodecProfile::av1_main;
    }
    return CodecProfile::h264_high;
}

EncoderModeCapability hardware_mode(Codec codec, bool hdr10, std::string_view probe) {
    return EncoderModeCapability{
        .codec = codec,
        .profile = known(profile_for(codec, hdr10), advertised(probe)),
        .dimensions = known(DimensionRange{{128, 128}, {4096, 4096}}, advertised(probe)),
        .frame_rates = known(RationalRange{Rational{1, 1}, Rational{240, 1}}, advertised(probe)),
        .input_format = hdr10 ? PixelFormat::p010 : PixelFormat::nv12,
        .chroma = ChromaSubsampling::yuv420,
        .bit_depth = known(std::uint8_t{hdr10 ? std::uint8_t{10} : std::uint8_t{8}}, advertised(probe)),
        .color_range = known(ColorRange::limited, advertised(probe)),
        .transfer_function = known(hdr10 ? TransferFunction::pq : TransferFunction::bt709, advertised(probe)),
        .hdr = known(hdr10 ? HdrMode::hdr10 : HdrMode::sdr, advertised(probe)),
        .low_latency = supported(advertised(probe)),
        .support = supported(advertised(probe)),
    };
}

EncoderCapability hardware_encoder(EncoderId id, Codec codec, Observed<GpuId> gpu, EncoderBackend backend,
                                   std::string_view probe, std::vector<EncoderModeCapability> modes) {
    return EncoderCapability{
        .id = std::move(id),
        .codec = codec,
        .backend = backend,
        .implementation = ImplementationClass::hardware,
        .gpu = std::move(gpu),
        .name = known(std::string("Hardware Encoder"), advertised(probe)),
        .support = supported(advertised(probe)),
        .modes = std::move(modes),
    };
}

EncoderCapability software_encoder(EncoderId id) {
    const auto probe = kEncoderProbe;
    return EncoderCapability{
        .id = std::move(id),
        .codec = Codec::h264,
        .backend = EncoderBackend::media_foundation,
        .implementation = ImplementationClass::software,
        .gpu = unavailable<GpuId>(probe, IssueCode::not_applicable),
        .name = known(std::string("Software H.264 Encoder"), advertised(probe)),
        .support = supported(advertised(probe)),
        .modes = {EncoderModeCapability{
            .codec = Codec::h264,
            .profile = known(CodecProfile::h264_main, advertised(probe)),
            .dimensions = unknown<DimensionRange>(probe, IssueCode::not_reported),
            .frame_rates = unknown<RationalRange>(probe, IssueCode::not_reported),
            .input_format = PixelFormat::nv12,
            .chroma = ChromaSubsampling::yuv420,
            .bit_depth = known(std::uint8_t{8}, advertised(probe)),
            .color_range = known(ColorRange::limited, advertised(probe)),
            .transfer_function = known(TransferFunction::bt709, advertised(probe)),
            .hdr = known(HdrMode::sdr, advertised(probe)),
            .low_latency = support_unknown(probe, IssueCode::not_reported),
            .support = supported(advertised(probe)),
        }},
    };
}

CapturePathCapability display_capture_path(CapturePathId id, CaptureApi api, std::string_view probe) {
    return CapturePathCapability{
        .id = std::move(id),
        .api = api,
        .source = SourceKind::display,
        .support = supported(advertised(probe)),
        .gpu = unknown<GpuId>(probe, IssueCode::relationship_unprovable),
        .output_formats = known(std::vector{PixelFormat::bgra8, PixelFormat::rgba16f}, advertised(probe)),
        .hdr_output = supported(advertised(probe)),
        .frame_rates = unknown<RationalRange>(probe, IssueCode::not_reported),
    };
}

TransferPathCapability transfer(CapturePathId source, EncoderId destination, std::optional<GpuId> source_gpu,
                                std::optional<GpuId> destination_gpu, TransferKind kind, std::string_view probe) {
    const bool provable = kind != TransferKind::unknown;
    return TransferPathCapability{
        .source = std::move(source),
        .destination = std::move(destination),
        .source_gpu = std::move(source_gpu),
        .destination_gpu = std::move(destination_gpu),
        .transfer = kind,
        .conversions = provable ? known(std::vector{Conversion::pixel_format}, advertised(probe))
                                : unknown<std::vector<Conversion>>(probe, IssueCode::relationship_unprovable),
        .evidence = provable ? supported(advertised(probe)) : support_unknown(probe, IssueCode::relationship_unprovable),
    };
}

std::vector<AudioEndpointCapability> audio_endpoints(AudioEndpointId input, AudioEndpointId output, std::string_view probe) {
    const auto endpoint = [probe](AudioEndpointId id, AudioDirection direction, std::string name) {
        return AudioEndpointCapability{
            .id = std::move(id),
            .direction = direction,
            .name = known(std::move(name), advertised(probe)),
            .channels = known(2U, advertised(probe)),
            .sample_rate_hz = known(48'000U, advertised(probe)),
            .sample_formats = known(std::vector{SampleFormat::pcm_f32}, advertised(probe)),
        };
    };
    return {endpoint(std::move(input), AudioDirection::input, "Microphone"),
            endpoint(std::move(output), AudioDirection::output, "Speakers")};
}

std::vector<AudioEndpointState> audio_states(const std::vector<AudioEndpointCapability>& endpoints, std::string_view probe) {
    std::vector<AudioEndpointState> states;
    for (const auto& endpoint : endpoints) {
        states.push_back(AudioEndpointState{
            .endpoint = endpoint.id,
            .active = known(true, measured(probe)),
            .default_roles = known(std::vector{AudioRole::console, AudioRole::multimedia, AudioRole::communications},
                                   measured(probe)),
        });
    }
    return states;
}

struct RuntimeShape {
    PowerSource power = PowerSource::ac;
    bool battery = false;
    bool low_power = false;
    std::optional<ThermalPressure> thermal = ThermalPressure::nominal;
};

void set_runtime(RuntimeState& runtime, const RuntimeShape& shape, std::string_view probe) {
    runtime.power_source = known(shape.power, measured(probe));
    runtime.battery_present = known(shape.battery, measured(probe));
    runtime.low_power_mode = known(shape.low_power, measured(probe));
    // Windows has no public thermal-pressure API; that absence stays explicit.
    runtime.thermal = shape.thermal ? known(*shape.thermal, measured(probe))
                                    : unavailable<ThermalPressure>(probe, IssueCode::api_unavailable);
    runtime.memory_pressure = known(MemoryPressure::normal, measured(probe));
    runtime.remote_session = known(false, measured(probe));
    runtime.headless = known(false, measured(probe));
}

CapturePermissionState permission(CapturePathId path, std::string_view probe) {
    return CapturePermissionState{.path = std::move(path),
                                  .permission = known(CapturePermission::granted, advertised(probe))};
}

} // namespace

Provenance measured(std::string_view probe) {
    return make_provenance(probe, EvidenceMethod::measured);
}

Provenance advertised(std::string_view probe) {
    return make_provenance(probe, EvidenceMethod::advertised);
}

Provenance absent(std::string_view probe, IssueCode code) {
    return Provenance{
        .probe_id = std::string(probe),
        .method = EvidenceMethod::advertised,
        .confidence = Confidence::degraded,
        .issue = code,
    };
}

SupportFact supported(Provenance provenance) {
    return SupportFact{.status = Support::supported, .provenance = std::move(provenance)};
}

SupportFact unsupported(Provenance provenance) {
    return SupportFact{.status = Support::unsupported, .provenance = std::move(provenance)};
}

SupportFact support_unknown(std::string_view probe, IssueCode code) {
    return SupportFact{.status = Support::unknown, .provenance = absent(probe, code)};
}

ProbeRecord probe_record(std::string_view probe, ProbeFamily family, std::uint64_t generation) {
    return ProbeRecord{
        .probe_id = std::string(probe),
        .family = family,
        .revision = 1,
        .generation = generation,
        .duration = std::chrono::microseconds{12'000},
        .outcome = ProbeOutcome::success,
        .fact_count = 8,
    };
}

GpuId desktop_gpu() { return {"luid:00000000:0000a001", IdentityScope::os_session}; }
DisplayId desktop_display() { return {"display:00000000:0000a001:1", IdentityScope::os_session}; }
DisplayId extra_display(std::uint32_t index) {
    return {"display:00000000:0000a001:" + std::to_string(index), IdentityScope::os_session};
}
EncoderId hardware_h264() { return {"mft:h264:hardware:0", IdentityScope::service_lifetime}; }
EncoderId software_h264() { return {"mft:h264:software:0", IdentityScope::service_lifetime}; }
EncoderId hardware_hevc() { return {"mft:hevc:hardware:0", IdentityScope::service_lifetime}; }
EncoderId hardware_av1() { return {"mft:av1:hardware:0", IdentityScope::service_lifetime}; }
CapturePathId display_capture() { return {"wgc:display", IdentityScope::persistent}; }
AudioEndpointId microphone() { return {"mmdevice:input:0", IdentityScope::persistent}; }
AudioEndpointId speakers() { return {"mmdevice:output:0", IdentityScope::persistent}; }

GpuId hybrid_integrated_gpu() { return {"luid:00000000:0000b001", IdentityScope::os_session}; }
GpuId hybrid_discrete_gpu() { return {"luid:00000000:0000b002", IdentityScope::os_session}; }
EncoderId hybrid_integrated_h264() { return {"mft:h264:hardware:igpu", IdentityScope::service_lifetime}; }
EncoderId hybrid_discrete_h264() { return {"mft:h264:hardware:dgpu", IdentityScope::service_lifetime}; }
EncoderId hybrid_discrete_av1() { return {"mft:av1:hardware:dgpu", IdentityScope::service_lifetime}; }
DisplayId laptop_display() { return {"display:00000000:0000b001:1", IdentityScope::os_session}; }

DisplayId mac_display() { return {"cg:display:1", IdentityScope::os_session}; }
CapturePathId mac_display_capture() { return {"sck:display", IdentityScope::persistent}; }
EncoderId mac_h264() { return {"vt:h264:hardware", IdentityScope::service_lifetime}; }
EncoderId mac_hevc() { return {"vt:hevc:hardware", IdentityScope::service_lifetime}; }

MediaDecisionRequest display_request() {
    return MediaDecisionRequest{};
}

CapabilitySnapshot shuffled(CapabilitySnapshot snapshot, std::uint32_t seed) {
    std::mt19937 random(seed);
    const auto shuffle = [&random](auto& items) { std::ranges::shuffle(items, random); };
    const auto shuffle_set = [&random]<class T>(Observed<std::vector<T>>& set) {
        if (auto values = set.value()) {
            std::ranges::shuffle(*values, random);
            set = Observed<std::vector<T>>(set.knowledge(), std::move(values), set.provenance());
        }
    };
    auto& devices = snapshot.devices;
    shuffle(devices.gpus);
    for (auto& gpu : devices.gpus) {
        shuffle_set(gpu.graphics_apis);
    }
    shuffle(devices.encoders);
    for (auto& encoder : devices.encoders) {
        shuffle(encoder.modes);
    }
    shuffle(devices.capture_paths);
    for (auto& capture : devices.capture_paths) {
        shuffle_set(capture.output_formats);
    }
    shuffle(devices.displays);
    for (auto& display : devices.displays) {
        shuffle_set(display.modes);
    }
    shuffle(devices.audio_endpoints);
    for (auto& endpoint : devices.audio_endpoints) {
        shuffle_set(endpoint.sample_formats);
    }
    shuffle(devices.transfer_paths);
    for (auto& transfer : devices.transfer_paths) {
        shuffle_set(transfer.conversions);
    }
    shuffle_set(snapshot.hardware.cpu.simd);
    auto& runtime = snapshot.runtime;
    shuffle(runtime.displays);
    shuffle(runtime.audio_endpoints);
    for (auto& state : runtime.audio_endpoints) {
        shuffle_set(state.default_roles);
    }
    shuffle(runtime.capture_permissions);
    shuffle(snapshot.probes);
    shuffle(snapshot.issues);
    return snapshot;
}

CapabilitySnapshot valid_snapshot() {
    auto snapshot = base_snapshot(OperatingSystem::windows, {10, 0, 26100}, kWindowsProbes);
    snapshot.hardware.cpu = cpu(CpuArchitecture::x86_64, 16, 32, known(16U, measured(kSystemProbe)),
                                known(0U, measured(kSystemProbe)),
                                {SimdFeature::sse4_2, SimdFeature::avx, SimdFeature::avx2}, kSystemProbe);
    snapshot.hardware.installed_memory = known(Bytes{32 * kGiB}, measured(kSystemProbe));
    snapshot.hardware.platform_role = known(PlatformRole::desktop, advertised(kSystemProbe));

    snapshot.devices.gpus.push_back(windows_gpu({.id = desktop_gpu(), .name = "Discrete GPU", .dedicated_gib = 12, .shared_gib = 16}));
    snapshot.devices.displays.push_back(display(desktop_display(), known(desktop_gpu(), advertised(kGpuDisplayProbe)),
                                                {mode_1440p(Rational{144, 1}), mode_1440p(Rational{60, 1})},
                                                ColorGamut::srgb, 8, kGpuDisplayProbe));

    auto h264 = hardware_encoder(hardware_h264(), Codec::h264, known(desktop_gpu(), advertised(kEncoderProbe)),
                                 EncoderBackend::media_foundation, kEncoderProbe,
                                 {hardware_mode(Codec::h264, false, kEncoderProbe)});
    h264.name = known(std::string("Hardware H.264 Encoder"), advertised(kEncoderProbe));
    snapshot.devices.encoders = {std::move(h264), software_encoder(software_h264())};

    snapshot.devices.capture_paths.push_back(
        display_capture_path(display_capture(), CaptureApi::windows_graphics_capture, kGpuDisplayProbe));
    snapshot.devices.transfer_paths = {
        transfer(display_capture(), hardware_h264(), desktop_gpu(), desktop_gpu(), TransferKind::same_resource, kEncoderProbe),
        transfer(display_capture(), software_h264(), std::nullopt, std::nullopt, TransferKind::cpu_staging, kEncoderProbe),
    };
    snapshot.devices.audio_endpoints = audio_endpoints(microphone(), speakers(), kAudioProbe);

    set_runtime(snapshot.runtime, {}, kRuntimeProbe);
    snapshot.runtime.displays.push_back(
        display_state(desktop_display(), mode_1440p(Rational{144, 1}), Rational{1, 1}, kGpuDisplayProbe));
    snapshot.runtime.audio_endpoints = audio_states(snapshot.devices.audio_endpoints, kAudioProbe);
    snapshot.runtime.capture_permissions.push_back(permission(display_capture(), kGpuDisplayProbe));
    return snapshot;
}

CapabilitySnapshot high_end_desktop() {
    auto snapshot = valid_snapshot();
    const auto gpu = known(desktop_gpu(), advertised(kEncoderProbe));
    snapshot.devices.encoders.push_back(hardware_encoder(
        hardware_hevc(), Codec::hevc, gpu, EncoderBackend::media_foundation, kEncoderProbe,
        {hardware_mode(Codec::hevc, false, kEncoderProbe), hardware_mode(Codec::hevc, true, kEncoderProbe)}));
    snapshot.devices.encoders.push_back(hardware_encoder(hardware_av1(), Codec::av1, gpu, EncoderBackend::media_foundation,
                                                         kEncoderProbe, {hardware_mode(Codec::av1, false, kEncoderProbe)}));
    for (const auto& encoder : {hardware_hevc(), hardware_av1()}) {
        snapshot.devices.transfer_paths.push_back(
            transfer(display_capture(), encoder, desktop_gpu(), desktop_gpu(), TransferKind::same_resource, kEncoderProbe));
    }
    return snapshot;
}

CapabilitySnapshot intel_laptop_on_battery() {
    auto snapshot = base_snapshot(OperatingSystem::windows, {10, 0, 22631}, kWindowsProbes);
    snapshot.hardware.cpu = cpu(CpuArchitecture::x86_64, 12, 16, known(4U, measured(kSystemProbe)),
                                known(8U, measured(kSystemProbe)),
                                {SimdFeature::sse4_2, SimdFeature::avx, SimdFeature::avx2}, kSystemProbe);
    snapshot.hardware.installed_memory = known(Bytes{16 * kGiB}, measured(kSystemProbe));
    snapshot.hardware.platform_role = known(PlatformRole::mobile, advertised(kSystemProbe));

    const GpuId gpu{"luid:00000000:0000c001", IdentityScope::os_session};
    const DisplayId panel{"display:00000000:0000c001:1", IdentityScope::os_session};
    const EncoderId h264{"mft:h264:hardware:igpu", IdentityScope::service_lifetime};
    const EncoderId hevc{"mft:hevc:hardware:igpu", IdentityScope::service_lifetime};
    const auto panel_mode = mode({1920, 1200}, {1280, 800}, Rational{60, 1});

    snapshot.devices.gpus.push_back(windows_gpu({.id = gpu, .kind = GpuKind::integrated, .name = "Integrated GPU", .shared_gib = 8}));
    snapshot.devices.displays.push_back(display(panel, known(gpu, advertised(kGpuDisplayProbe)), {panel_mode},
                                                ColorGamut::srgb, 8, kGpuDisplayProbe));
    const auto encoder_gpu = known(gpu, advertised(kEncoderProbe));
    snapshot.devices.encoders = {
        hardware_encoder(h264, Codec::h264, encoder_gpu, EncoderBackend::media_foundation, kEncoderProbe,
                         {hardware_mode(Codec::h264, false, kEncoderProbe)}),
        hardware_encoder(hevc, Codec::hevc, encoder_gpu, EncoderBackend::media_foundation, kEncoderProbe,
                         {hardware_mode(Codec::hevc, false, kEncoderProbe)}),
        software_encoder(software_h264()),
    };
    snapshot.devices.capture_paths.push_back(
        display_capture_path(display_capture(), CaptureApi::windows_graphics_capture, kGpuDisplayProbe));
    snapshot.devices.transfer_paths = {
        transfer(display_capture(), h264, gpu, gpu, TransferKind::same_resource, kEncoderProbe),
        transfer(display_capture(), hevc, gpu, gpu, TransferKind::same_resource, kEncoderProbe),
        transfer(display_capture(), software_h264(), std::nullopt, std::nullopt, TransferKind::cpu_staging, kEncoderProbe),
    };
    snapshot.devices.audio_endpoints = audio_endpoints(microphone(), speakers(), kAudioProbe);

    set_runtime(snapshot.runtime, {.power = PowerSource::battery, .battery = true, .low_power = true, .thermal = std::nullopt},
                kRuntimeProbe);
    snapshot.runtime.displays.push_back(display_state(panel, panel_mode, Rational{3, 2}, kGpuDisplayProbe));
    snapshot.runtime.audio_endpoints = audio_states(snapshot.devices.audio_endpoints, kAudioProbe);
    snapshot.runtime.capture_permissions.push_back(permission(display_capture(), kGpuDisplayProbe));
    return snapshot;
}

CapabilitySnapshot hybrid_laptop() {
    auto snapshot = base_snapshot(OperatingSystem::windows, {10, 0, 26100}, kWindowsProbes);
    snapshot.hardware.cpu = cpu(CpuArchitecture::x86_64, 16, 24, known(8U, measured(kSystemProbe)),
                                known(8U, measured(kSystemProbe)),
                                {SimdFeature::sse4_2, SimdFeature::avx, SimdFeature::avx2}, kSystemProbe);
    snapshot.hardware.installed_memory = known(Bytes{32 * kGiB}, measured(kSystemProbe));
    snapshot.hardware.platform_role = known(PlatformRole::mobile, advertised(kSystemProbe));

    const auto integrated = hybrid_integrated_gpu();
    const auto discrete = hybrid_discrete_gpu();
    const auto panel_mode = mode({2560, 1600}, {1706, 1066}, Rational{165, 1});
    snapshot.devices.gpus = {
        windows_gpu({.id = integrated, .kind = GpuKind::integrated, .name = "Integrated GPU", .shared_gib = 16,
                     .minimum_power = true, .high_performance = false}),
        windows_gpu({.id = discrete, .kind = GpuKind::discrete, .name = "Discrete GPU", .dedicated_gib = 8,
                     .shared_gib = 16, .minimum_power = false, .high_performance = true}),
    };
    snapshot.devices.displays.push_back(display(laptop_display(), known(integrated, advertised(kGpuDisplayProbe)),
                                                {panel_mode, mode({2560, 1600}, {1706, 1066}, Rational{60, 1})},
                                                ColorGamut::srgb, 8, kGpuDisplayProbe));
    snapshot.devices.encoders = {
        hardware_encoder(hybrid_integrated_h264(), Codec::h264, known(integrated, advertised(kEncoderProbe)),
                         EncoderBackend::media_foundation, kEncoderProbe, {hardware_mode(Codec::h264, false, kEncoderProbe)}),
        hardware_encoder(hybrid_discrete_h264(), Codec::h264, known(discrete, advertised(kEncoderProbe)),
                         EncoderBackend::media_foundation, kEncoderProbe, {hardware_mode(Codec::h264, false, kEncoderProbe)}),
        hardware_encoder(hybrid_discrete_av1(), Codec::av1, known(discrete, advertised(kEncoderProbe)),
                         EncoderBackend::media_foundation, kEncoderProbe, {hardware_mode(Codec::av1, false, kEncoderProbe)}),
    };
    snapshot.devices.capture_paths.push_back(
        display_capture_path(display_capture(), CaptureApi::windows_graphics_capture, kGpuDisplayProbe));
    snapshot.devices.transfer_paths = {
        transfer(display_capture(), hybrid_integrated_h264(), integrated, integrated, TransferKind::same_resource, kEncoderProbe),
        transfer(display_capture(), hybrid_discrete_h264(), integrated, discrete, TransferKind::cross_adapter_copy, kEncoderProbe),
        transfer(display_capture(), hybrid_discrete_av1(), integrated, discrete, TransferKind::cross_adapter_copy, kEncoderProbe),
    };
    snapshot.devices.audio_endpoints = audio_endpoints(microphone(), speakers(), kAudioProbe);

    set_runtime(snapshot.runtime, {.power = PowerSource::ac, .battery = true, .thermal = std::nullopt}, kRuntimeProbe);
    snapshot.runtime.displays.push_back(display_state(laptop_display(), panel_mode, Rational{3, 2}, kGpuDisplayProbe));
    snapshot.runtime.audio_endpoints = audio_states(snapshot.devices.audio_endpoints, kAudioProbe);
    snapshot.runtime.capture_permissions.push_back(permission(display_capture(), kGpuDisplayProbe));
    return snapshot;
}

CapabilitySnapshot apple_silicon_macbook() {
    auto snapshot = base_snapshot(OperatingSystem::macos, {14, 5, 0}, kMacProbes);
    snapshot.hardware.cpu = cpu(CpuArchitecture::arm64, 12, 12, known(8U, measured(kMacSystemProbe)),
                                known(4U, measured(kMacSystemProbe)), {SimdFeature::neon}, kMacSystemProbe);
    snapshot.hardware.installed_memory = known(Bytes{36 * kGiB}, measured(kMacSystemProbe));
    snapshot.hardware.platform_role = known(PlatformRole::mobile, advertised(kMacSystemProbe));

    const GpuId gpu{"metal:registry:1000", IdentityScope::os_session};
    const auto panel_mode = mode({3024, 1964}, {1512, 982}, Rational{120, 1});
    snapshot.devices.gpus.push_back(
        mac_gpu({.id = gpu, .kind = GpuKind::integrated, .name = "Apple GPU", .shared_gib = 36, .unified = true}));
    snapshot.devices.displays.push_back(display(mac_display(), known(gpu, advertised(kMacGpuDisplayProbe)),
                                                {panel_mode, mode({3024, 1964}, {1512, 982}, Rational{60, 1})},
                                                ColorGamut::display_p3, 10, kMacGpuDisplayProbe));

    // The media engine is not a Metal device, so encoder adapter affinity stays unknown; the
    // IOSurface-backed path from ScreenCaptureKit to VideoToolbox is still documented as shared.
    const auto no_adapter = unknown<GpuId>(kMacEncoderProbe, IssueCode::relationship_unprovable);
    snapshot.devices.encoders = {
        hardware_encoder(mac_h264(), Codec::h264, no_adapter, EncoderBackend::video_toolbox, kMacEncoderProbe,
                         {hardware_mode(Codec::h264, false, kMacEncoderProbe)}),
        hardware_encoder(mac_hevc(), Codec::hevc, no_adapter, EncoderBackend::video_toolbox, kMacEncoderProbe,
                         {hardware_mode(Codec::hevc, false, kMacEncoderProbe), hardware_mode(Codec::hevc, true, kMacEncoderProbe)}),
    };
    snapshot.devices.capture_paths.push_back(
        display_capture_path(mac_display_capture(), CaptureApi::screen_capture_kit, kMacGpuDisplayProbe));
    snapshot.devices.transfer_paths = {
        transfer(mac_display_capture(), mac_h264(), std::nullopt, std::nullopt, TransferKind::same_resource, kMacEncoderProbe),
        transfer(mac_display_capture(), mac_hevc(), std::nullopt, std::nullopt, TransferKind::same_resource, kMacEncoderProbe),
    };
    snapshot.devices.audio_endpoints = audio_endpoints({"coreaudio:BuiltInMicrophoneDevice", IdentityScope::persistent},
                                                       {"coreaudio:BuiltInSpeakerDevice", IdentityScope::persistent},
                                                       kMacAudioProbe);

    set_runtime(snapshot.runtime, {.power = PowerSource::ac, .battery = true}, kMacRuntimeProbe);
    snapshot.runtime.displays.push_back(display_state(mac_display(), panel_mode, Rational{2, 1}, kMacGpuDisplayProbe));
    snapshot.runtime.audio_endpoints = audio_states(snapshot.devices.audio_endpoints, kMacAudioProbe);
    snapshot.runtime.capture_permissions.push_back(permission(mac_display_capture(), kMacGpuDisplayProbe));
    return snapshot;
}

CapabilitySnapshot hot_apple_silicon() {
    auto snapshot = apple_silicon_macbook();
    snapshot.runtime.thermal = known(ThermalPressure::serious, measured(kMacRuntimeProbe));
    return snapshot;
}

CapabilitySnapshot older_intel_mac() {
    auto snapshot = base_snapshot(OperatingSystem::macos, {13, 6, 0}, kMacProbes);
    snapshot.hardware.cpu = cpu(CpuArchitecture::x86_64, 6, 12,
                                unavailable<std::uint32_t>(kMacSystemProbe, IssueCode::not_applicable),
                                unavailable<std::uint32_t>(kMacSystemProbe, IssueCode::not_applicable),
                                {SimdFeature::sse4_2, SimdFeature::avx, SimdFeature::avx2}, kMacSystemProbe);
    snapshot.hardware.installed_memory = known(Bytes{16 * kGiB}, measured(kMacSystemProbe));
    snapshot.hardware.platform_role = known(PlatformRole::mobile, advertised(kMacSystemProbe));

    const GpuId integrated{"metal:registry:2000", IdentityScope::os_session};
    const GpuId discrete{"metal:registry:3000", IdentityScope::os_session};
    const auto panel_mode = mode({2880, 1800}, {1440, 900}, Rational{60, 1});
    snapshot.devices.gpus = {
        mac_gpu({.id = integrated, .kind = GpuKind::integrated, .name = "Integrated GPU", .shared_gib = 2}),
        mac_gpu({.id = discrete, .kind = GpuKind::discrete, .name = "Discrete GPU", .dedicated_gib = 4}),
    };
    snapshot.devices.displays.push_back(display(mac_display(),
                                                unknown<GpuId>(kMacGpuDisplayProbe, IssueCode::relationship_unprovable),
                                                {panel_mode}, ColorGamut::display_p3, 8, kMacGpuDisplayProbe));

    const auto no_adapter = unknown<GpuId>(kMacEncoderProbe, IssueCode::relationship_unprovable);
    snapshot.devices.encoders = {
        hardware_encoder(mac_h264(), Codec::h264, no_adapter, EncoderBackend::video_toolbox, kMacEncoderProbe,
                         {hardware_mode(Codec::h264, false, kMacEncoderProbe)}),
        hardware_encoder(mac_hevc(), Codec::hevc, no_adapter, EncoderBackend::video_toolbox, kMacEncoderProbe,
                         {hardware_mode(Codec::hevc, false, kMacEncoderProbe)}),
    };
    snapshot.devices.capture_paths.push_back(
        display_capture_path(mac_display_capture(), CaptureApi::screen_capture_kit, kMacGpuDisplayProbe));
    snapshot.devices.transfer_paths = {
        transfer(mac_display_capture(), mac_h264(), std::nullopt, std::nullopt, TransferKind::unknown, kMacEncoderProbe),
        transfer(mac_display_capture(), mac_hevc(), std::nullopt, std::nullopt, TransferKind::unknown, kMacEncoderProbe),
    };
    snapshot.devices.audio_endpoints = audio_endpoints({"coreaudio:BuiltInMicrophoneDevice", IdentityScope::persistent},
                                                       {"coreaudio:BuiltInSpeakerDevice", IdentityScope::persistent},
                                                       kMacAudioProbe);

    set_runtime(snapshot.runtime, {.power = PowerSource::ac, .battery = true}, kMacRuntimeProbe);
    snapshot.runtime.displays.push_back(display_state(mac_display(), panel_mode, Rational{2, 1}, kMacGpuDisplayProbe));
    snapshot.runtime.audio_endpoints = audio_states(snapshot.devices.audio_endpoints, kMacAudioProbe);
    snapshot.runtime.capture_permissions.push_back(permission(mac_display_capture(), kMacGpuDisplayProbe));
    return snapshot;
}

CapabilitySnapshot headless_session() {
    auto snapshot = valid_snapshot();
    snapshot.devices.displays.clear();
    snapshot.runtime.displays.clear();
    snapshot.runtime.headless = known(true, measured(kRuntimeProbe));
    return snapshot;
}

CapabilitySnapshot partial_probe_failure() {
    auto snapshot = valid_snapshot();
    auto& encoders = *std::ranges::find(snapshot.probes, std::string(kEncoderProbe), &ProbeRecord::probe_id);
    encoders.outcome = ProbeOutcome::timeout;
    encoders.duration = std::chrono::microseconds{1'500'000};
    encoders.fact_count = 0;
    snapshot.issues.push_back(ProbeIssue{.probe_id = std::string(kEncoderProbe), .code = IssueCode::timeout});
    snapshot.devices.encoders.clear();
    snapshot.devices.transfer_paths.clear();
    return snapshot;
}

CapabilitySnapshot software_only() {
    auto snapshot = valid_snapshot();
    std::erase_if(snapshot.devices.encoders, [](const EncoderCapability& encoder) { return encoder.id == hardware_h264(); });
    std::erase_if(snapshot.devices.transfer_paths,
                  [](const TransferPathCapability& transfer) { return transfer.destination == hardware_h264(); });
    return snapshot;
}

CapabilitySnapshot missing_gpu_driver() {
    auto snapshot = valid_snapshot();
    auto& gpu = snapshot.devices.gpus.front();
    gpu.kind = known(GpuKind::software, advertised(kGpuDisplayProbe));
    gpu.name = known(std::string("Basic Display Adapter"), advertised(kGpuDisplayProbe));
    const auto failed = unsupported(make_provenance(kEncoderProbe, EvidenceMethod::probe_validated));
    auto& encoder = *std::ranges::find(snapshot.devices.encoders, hardware_h264(), &EncoderCapability::id);
    encoder.support = failed;
    for (auto& encoder_mode : encoder.modes) {
        encoder_mode.support = failed;
    }
    return snapshot;
}

CapabilitySnapshot unknown_codec_limits() {
    auto snapshot = valid_snapshot();
    auto& encoder_mode = std::ranges::find(snapshot.devices.encoders, hardware_h264(), &EncoderCapability::id)->modes.front();
    encoder_mode.dimensions = unknown<DimensionRange>(kEncoderProbe, IssueCode::not_reported);
    encoder_mode.frame_rates = unknown<RationalRange>(kEncoderProbe, IssueCode::not_reported);
    return snapshot;
}

CapabilitySnapshot no_microphone() {
    auto snapshot = valid_snapshot();
    std::erase_if(snapshot.devices.audio_endpoints,
                  [](const AudioEndpointCapability& endpoint) { return endpoint.id == microphone(); });
    std::erase_if(snapshot.runtime.audio_endpoints,
                  [](const AudioEndpointState& state) { return state.endpoint == microphone(); });
    return snapshot;
}

CapabilitySnapshot remote_session() {
    auto snapshot = valid_snapshot();
    snapshot.runtime.remote_session = known(true, measured(kRuntimeProbe));
    return snapshot;
}

CapabilitySnapshot mixed_refresh_desktop() {
    auto snapshot = valid_snapshot();
    const struct {
        std::uint32_t index;
        DisplayMode active;
    } extras[] = {
        {2, mode({1920, 1080}, {1920, 1080}, Rational{60'000, 1'001})},
        {3, mode({1920, 1080}, {1920, 1080}, Rational{60, 1})},
        {4, mode_1440p(Rational{120, 1})},
    };
    for (const auto& extra : extras) {
        snapshot.devices.displays.push_back(display(extra_display(extra.index), known(desktop_gpu(), advertised(kGpuDisplayProbe)),
                                                    {extra.active}, ColorGamut::srgb, 8, kGpuDisplayProbe));
        auto state = display_state(extra_display(extra.index), extra.active, Rational{1, 1}, kGpuDisplayProbe);
        state.primary = known(false, measured(kGpuDisplayProbe));
        snapshot.runtime.displays.push_back(std::move(state));
    }
    return snapshot;
}

CapabilitySnapshot hdr_desktop() {
    auto snapshot = high_end_desktop();
    snapshot.runtime.displays.front().hdr_enabled = known(true, measured(kGpuDisplayProbe));
    return snapshot;
}

CapabilitySnapshot crowded_desktop(std::uint32_t extra) {
    auto snapshot = valid_snapshot();
    for (std::uint32_t index = 0; index < extra; ++index) {
        const EncoderId id{"mft:h264:hardware:x" + std::string(index < 10 ? "0" : "") + std::to_string(index),
                           IdentityScope::service_lifetime};
        snapshot.devices.encoders.push_back(hardware_encoder(id, Codec::h264, known(desktop_gpu(), advertised(kEncoderProbe)),
                                                             EncoderBackend::media_foundation, kEncoderProbe,
                                                             {hardware_mode(Codec::h264, false, kEncoderProbe)}));
        snapshot.devices.transfer_paths.push_back(
            transfer(display_capture(), id, desktop_gpu(), desktop_gpu(), TransferKind::same_resource, kEncoderProbe));
    }
    return snapshot;
}

} // namespace catro::fixtures
