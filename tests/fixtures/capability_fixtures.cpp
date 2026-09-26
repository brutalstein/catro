#include "capability_fixtures.hpp"

#include <chrono>
#include <string>
#include <vector>

namespace catro::fixtures {

using namespace catro::capabilities;

namespace {

constexpr std::uint64_t kGiB = 1024ULL * 1024ULL * 1024ULL;

Provenance make_provenance(std::string_view probe, EvidenceMethod method) {
    return Provenance{.probe_id = std::string(probe), .method = method};
}

DisplayMode mode_1440p(Rational refresh_rate) {
    return DisplayMode{.pixels = {2560, 1440}, .logical = {2560, 1440}, .refresh_rate = refresh_rate};
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

GpuId desktop_gpu() {
    return {"luid:00000000:0000a001", IdentityScope::os_session};
}

DisplayId desktop_display() {
    return {"display:00000000:0000a001:1", IdentityScope::os_session};
}

EncoderId hardware_h264() {
    return {"mft:h264:hardware:0", IdentityScope::service_lifetime};
}

EncoderId software_h264() {
    return {"mft:h264:software:0", IdentityScope::service_lifetime};
}

CapturePathId display_capture() {
    return {"wgc:display", IdentityScope::persistent};
}

AudioEndpointId microphone() {
    return {"mmdevice:input:0", IdentityScope::persistent};
}

AudioEndpointId speakers() {
    return {"mmdevice:output:0", IdentityScope::persistent};
}

CapabilitySnapshot valid_snapshot() {
    CapabilitySnapshot snapshot;
    snapshot.header.generation = 1;
    snapshot.header.captured_at = UtcTimestamp{std::chrono::microseconds{1'790'000'000'000'000}};
    snapshot.header.probe_revision = 1;
    snapshot.probes = {
        probe_record(kSystemProbe, ProbeFamily::system),
        probe_record(kRuntimeProbe, ProbeFamily::runtime),
        probe_record(kGpuDisplayProbe, ProbeFamily::gpu_display),
        probe_record(kEncoderProbe, ProbeFamily::encoders),
        probe_record(kAudioProbe, ProbeFamily::audio),
    };

    snapshot.platform = {
        .os = OperatingSystem::windows,
        .version = known(OsVersion{10, 0, 26100}, measured(kSystemProbe)),
    };

    auto& cpu = snapshot.hardware.cpu;
    cpu.native_architecture = known(CpuArchitecture::x86_64, measured(kSystemProbe));
    cpu.process_architecture = known(CpuArchitecture::x86_64, measured(kSystemProbe));
    cpu.translation = known(TranslationState::native, measured(kSystemProbe));
    cpu.physical_cores = known(16U, measured(kSystemProbe));
    cpu.logical_cores = known(32U, measured(kSystemProbe));
    cpu.performance_cores = known(16U, measured(kSystemProbe));
    cpu.efficiency_cores = known(0U, measured(kSystemProbe));
    cpu.simd = known(std::vector{SimdFeature::sse4_2, SimdFeature::avx, SimdFeature::avx2}, measured(kSystemProbe));
    snapshot.hardware.installed_memory = known(Bytes{32 * kGiB}, measured(kSystemProbe));
    snapshot.hardware.platform_role = known(PlatformRole::desktop, advertised(kSystemProbe));

    snapshot.devices.gpus.push_back(GpuCapability{
        .id = desktop_gpu(),
        .vendor_id = known(0x1234U, advertised(kGpuDisplayProbe)),
        .device_id = known(0x5678U, advertised(kGpuDisplayProbe)),
        .name = known(std::string("Discrete GPU"), advertised(kGpuDisplayProbe)),
        .kind = known(GpuKind::discrete, advertised(kGpuDisplayProbe)),
        .dedicated_memory = known(Bytes{12 * kGiB}, advertised(kGpuDisplayProbe)),
        .shared_memory = known(Bytes{16 * kGiB}, advertised(kGpuDisplayProbe)),
        .unified_memory = known(false, advertised(kGpuDisplayProbe)),
        .graphics_apis = known(std::vector{GraphicsApi::direct3d11, GraphicsApi::direct3d12}, advertised(kGpuDisplayProbe)),
        .preferred_for_minimum_power = known(true, advertised(kGpuDisplayProbe)),
        .preferred_for_high_performance = known(true, advertised(kGpuDisplayProbe)),
    });

    snapshot.devices.displays.push_back(DisplayCapability{
        .id = desktop_display(),
        .gpu = known(desktop_gpu(), advertised(kGpuDisplayProbe)),
        .modes = known(std::vector{mode_1440p(Rational{144, 1}), mode_1440p(Rational{60, 1})}, measured(kGpuDisplayProbe)),
        .hdr = supported(advertised(kGpuDisplayProbe)),
        .gamut = known(ColorGamut::srgb, advertised(kGpuDisplayProbe)),
        .bits_per_channel = known(std::uint8_t{8}, advertised(kGpuDisplayProbe)),
    });

    snapshot.devices.encoders.push_back(EncoderCapability{
        .id = hardware_h264(),
        .codec = Codec::h264,
        .backend = EncoderBackend::media_foundation,
        .implementation = ImplementationClass::hardware,
        .gpu = known(desktop_gpu(), advertised(kEncoderProbe)),
        .name = known(std::string("Hardware H.264 Encoder"), advertised(kEncoderProbe)),
        .support = supported(advertised(kEncoderProbe)),
        .modes = {EncoderModeCapability{
            .codec = Codec::h264,
            .profile = known(CodecProfile::h264_high, advertised(kEncoderProbe)),
            .dimensions = known(DimensionRange{{128, 128}, {4096, 4096}}, advertised(kEncoderProbe)),
            .frame_rates = known(RationalRange{Rational{1, 1}, Rational{240, 1}}, advertised(kEncoderProbe)),
            .input_format = PixelFormat::nv12,
            .chroma = ChromaSubsampling::yuv420,
            .bit_depth = known(std::uint8_t{8}, advertised(kEncoderProbe)),
            .color_range = known(ColorRange::limited, advertised(kEncoderProbe)),
            .transfer_function = known(TransferFunction::bt709, advertised(kEncoderProbe)),
            .hdr = known(HdrMode::sdr, advertised(kEncoderProbe)),
            .low_latency = supported(advertised(kEncoderProbe)),
            .support = supported(advertised(kEncoderProbe)),
        }},
    });

    snapshot.devices.encoders.push_back(EncoderCapability{
        .id = software_h264(),
        .codec = Codec::h264,
        .backend = EncoderBackend::media_foundation,
        .implementation = ImplementationClass::software,
        .gpu = unavailable<GpuId>(kEncoderProbe, IssueCode::not_applicable),
        .name = known(std::string("Software H.264 Encoder"), advertised(kEncoderProbe)),
        .support = supported(advertised(kEncoderProbe)),
        .modes = {EncoderModeCapability{
            .codec = Codec::h264,
            .profile = known(CodecProfile::h264_main, advertised(kEncoderProbe)),
            .dimensions = unknown<DimensionRange>(kEncoderProbe, IssueCode::not_reported),
            .frame_rates = unknown<RationalRange>(kEncoderProbe, IssueCode::not_reported),
            .input_format = PixelFormat::nv12,
            .chroma = ChromaSubsampling::yuv420,
            .bit_depth = known(std::uint8_t{8}, advertised(kEncoderProbe)),
            .color_range = known(ColorRange::limited, advertised(kEncoderProbe)),
            .transfer_function = known(TransferFunction::bt709, advertised(kEncoderProbe)),
            .hdr = known(HdrMode::sdr, advertised(kEncoderProbe)),
            .low_latency = support_unknown(kEncoderProbe, IssueCode::not_reported),
            .support = supported(advertised(kEncoderProbe)),
        }},
    });

    snapshot.devices.capture_paths.push_back(CapturePathCapability{
        .id = display_capture(),
        .api = CaptureApi::windows_graphics_capture,
        .source = SourceKind::display,
        .support = supported(advertised(kGpuDisplayProbe)),
        .gpu = unknown<GpuId>(kGpuDisplayProbe, IssueCode::relationship_unprovable),
        .output_formats = known(std::vector{PixelFormat::bgra8, PixelFormat::rgba16f}, advertised(kGpuDisplayProbe)),
        .hdr_output = supported(advertised(kGpuDisplayProbe)),
        .frame_rates = unknown<RationalRange>(kGpuDisplayProbe, IssueCode::not_reported),
    });

    snapshot.devices.transfer_paths = {
        TransferPathCapability{
            .source = display_capture(),
            .destination = hardware_h264(),
            .source_gpu = desktop_gpu(),
            .destination_gpu = desktop_gpu(),
            .transfer = TransferKind::same_resource,
            .conversions = known(std::vector{Conversion::pixel_format}, advertised(kEncoderProbe)),
            .evidence = supported(advertised(kEncoderProbe)),
        },
        TransferPathCapability{
            .source = display_capture(),
            .destination = software_h264(),
            .transfer = TransferKind::cpu_staging,
            .conversions = known(std::vector{Conversion::pixel_format}, advertised(kEncoderProbe)),
            .evidence = supported(advertised(kEncoderProbe)),
        },
    };

    snapshot.devices.audio_endpoints = {
        AudioEndpointCapability{
            .id = microphone(),
            .direction = AudioDirection::input,
            .name = known(std::string("Microphone"), advertised(kAudioProbe)),
            .channels = known(2U, advertised(kAudioProbe)),
            .sample_rate_hz = known(48'000U, advertised(kAudioProbe)),
            .sample_formats = known(std::vector{SampleFormat::pcm_f32}, advertised(kAudioProbe)),
        },
        AudioEndpointCapability{
            .id = speakers(),
            .direction = AudioDirection::output,
            .name = known(std::string("Speakers"), advertised(kAudioProbe)),
            .channels = known(2U, advertised(kAudioProbe)),
            .sample_rate_hz = known(48'000U, advertised(kAudioProbe)),
            .sample_formats = known(std::vector{SampleFormat::pcm_f32}, advertised(kAudioProbe)),
        },
    };

    auto& runtime = snapshot.runtime;
    runtime.power_source = known(PowerSource::ac, measured(kRuntimeProbe));
    runtime.battery_present = known(false, measured(kRuntimeProbe));
    runtime.low_power_mode = known(false, measured(kRuntimeProbe));
    runtime.thermal = known(ThermalPressure::nominal, measured(kRuntimeProbe));
    runtime.memory_pressure = known(MemoryPressure::normal, measured(kRuntimeProbe));
    runtime.remote_session = known(false, measured(kRuntimeProbe));
    runtime.headless = known(false, measured(kRuntimeProbe));
    runtime.displays.push_back(DisplayState{
        .display = desktop_display(),
        .active_mode = known(mode_1440p(Rational{144, 1}), measured(kGpuDisplayProbe)),
        .scale = known(Rational{1, 1}, measured(kGpuDisplayProbe)),
        .primary = known(true, measured(kGpuDisplayProbe)),
        .hdr_enabled = known(false, measured(kGpuDisplayProbe)),
    });
    const auto all_roles = std::vector{AudioRole::console, AudioRole::multimedia, AudioRole::communications};
    runtime.audio_endpoints = {
        AudioEndpointState{
            .endpoint = microphone(),
            .active = known(true, measured(kAudioProbe)),
            .default_roles = known(all_roles, measured(kAudioProbe)),
        },
        AudioEndpointState{
            .endpoint = speakers(),
            .active = known(true, measured(kAudioProbe)),
            .default_roles = known(all_roles, measured(kAudioProbe)),
        },
    };
    runtime.capture_permissions.push_back(CapturePermissionState{
        .path = display_capture(),
        .permission = known(CapturePermission::granted, advertised(kGpuDisplayProbe)),
    });

    return snapshot;
}

} // namespace catro::fixtures
