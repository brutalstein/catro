#include <catro/capabilities/validation.hpp>

#include <algorithm>
#include <iterator>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace catro::capabilities {
namespace {

bool is_identifier(std::string_view value) {
    return !value.empty() && value.size() <= kMaxIdentifierBytes &&
           std::ranges::all_of(value, [](char c) { return c > ' ' && c <= '~'; });
}

template <class Tag>
std::string at(std::string_view base, const ScopedId<Tag>& id) {
    return std::string(base) + "[" + id.value + "]";
}

Codec codec_of(CodecProfile profile) {
    switch (profile) {
    case CodecProfile::h264_baseline:
    case CodecProfile::h264_main:
    case CodecProfile::h264_high:
        return Codec::h264;
    case CodecProfile::hevc_main:
    case CodecProfile::hevc_main10:
        return Codec::hevc;
    case CodecProfile::av1_main:
        return Codec::av1;
    }
    return Codec::h264;
}

OperatingSystem platform_of(EncoderBackend backend) {
    return backend == EncoderBackend::video_toolbox ? OperatingSystem::macos : OperatingSystem::windows;
}

OperatingSystem platform_of(CaptureApi api) {
    return api == CaptureApi::screen_capture_kit ? OperatingSystem::macos : OperatingSystem::windows;
}

// Unknown transfer functions or bit depths cannot contradict a declared HDR mode.
bool color_consistent(HdrMode hdr, const TransferFunction* transfer, const std::uint8_t* bit_depth) {
    const bool high_bit_depth = bit_depth == nullptr || *bit_depth >= 10;
    switch (hdr) {
    case HdrMode::sdr:
        return transfer == nullptr || (*transfer != TransferFunction::pq && *transfer != TransferFunction::hlg);
    case HdrMode::hdr10:
        return (transfer == nullptr || *transfer == TransferFunction::pq) && high_bit_depth;
    case HdrMode::hlg:
        return (transfer == nullptr || *transfer == TransferFunction::hlg) && high_bit_depth;
    }
    return false;
}

class Validator {
public:
    explicit Validator(const CapabilitySnapshot& snapshot) : snapshot_(snapshot) {}

    ValidationReport run() {
        check_header();
        check_probes();
        gpu_ids_ = collect(snapshot_.devices.gpus, "devices.gpus", &GpuCapability::id);
        display_ids_ = collect(snapshot_.devices.displays, "devices.displays", &DisplayCapability::id);
        encoder_ids_ = collect(snapshot_.devices.encoders, "devices.encoders", &EncoderCapability::id);
        capture_path_ids_ = collect(snapshot_.devices.capture_paths, "devices.capture_paths", &CapturePathCapability::id);
        audio_ids_ = collect(snapshot_.devices.audio_endpoints, "devices.audio_endpoints", &AudioEndpointCapability::id);

        fact(snapshot_.platform.version, "platform.version");
        check_hardware();
        check_gpus();
        check_displays();
        check_encoders();
        check_capture_paths();
        check_transfers();
        check_audio();
        check_runtime();

        std::ranges::sort(errors_);
        const auto duplicates = std::ranges::unique(errors_);
        errors_.erase(duplicates.begin(), duplicates.end());
        return ValidationReport{std::move(errors_)};
    }

private:
    void error(ValidationCode code, std::string path) { errors_.push_back({code, std::move(path)}); }

    // --- identifiers and references ---------------------------------------------------------

    template <class Item, class Tag>
    std::set<ScopedId<Tag>> collect(const std::vector<Item>& items, std::string_view base, ScopedId<Tag> Item::* member) {
        std::set<ScopedId<Tag>> ids;
        std::set<std::string_view> values;
        for (const auto& item : items) {
            const auto& id = item.*member;
            if (!is_identifier(id.value)) {
                error(ValidationCode::invalid_identifier, at(base, id));
            }
            if (!values.insert(id.value).second) {
                error(ValidationCode::duplicate_id, at(base, id));
            }
            ids.insert(id);
        }
        return ids;
    }

    template <class Tag>
    void reference(const std::set<ScopedId<Tag>>& ids, const ScopedId<Tag>& id, ValidationCode missing, std::string path) {
        if (!ids.contains(id)) {
            error(missing, std::move(path));
        }
    }

    // --- evidence ----------------------------------------------------------------------------

    void provenance(const Provenance& provenance, bool absent, const std::string& path) {
        if (!probe_ids_.contains(provenance.probe_id)) {
            error(ValidationCode::missing_probe_reference, path);
        }
        const bool degraded = provenance.confidence == Confidence::degraded;
        if ((absent || degraded) && !provenance.issue) {
            error(ValidationCode::missing_issue_code, path);
        }
        if (!absent && !degraded && provenance.issue) {
            error(ValidationCode::contradictory_evidence, path);
        }
    }

    // Checks knowledge/value consistency and returns the value only when it is validly known.
    template <class T>
    const T* fact(const Observed<T>& observed, const std::string& path) {
        const bool known = observed.knowledge() == Knowledge::known;
        provenance(observed.provenance(), !known, path);
        if (known && !observed.value()) {
            error(ValidationCode::missing_value, path);
        } else if (!known && observed.value()) {
            error(ValidationCode::unexpected_value, path);
        } else if (known) {
            return &*observed.value();
        }
        return nullptr;
    }

    void support(const SupportFact& fact, const std::string& path) {
        provenance(fact.provenance, fact.status == Support::unknown, path);
    }

    // --- values ------------------------------------------------------------------------------

    void positive(std::uint64_t value, const std::string& path) {
        if (value == 0) {
            error(ValidationCode::invalid_quantity, path);
        }
    }

    void dimensions(const Dimensions& dimensions, const std::string& path) {
        if (dimensions.width == 0 || dimensions.height == 0) {
            error(ValidationCode::invalid_quantity, path);
        }
    }

    void rate(const Rational& rate, const std::string& path) {
        if (!rate.valid()) {
            error(ValidationCode::invalid_rational, path);
        } else if (rate.numerator() == 0) {
            error(ValidationCode::invalid_quantity, path);
        }
    }

    void range(const DimensionRange& range, const std::string& path) {
        dimensions(range.minimum, path);
        dimensions(range.maximum, path);
        if (range.minimum.width > range.maximum.width || range.minimum.height > range.maximum.height) {
            error(ValidationCode::invalid_range, path);
        }
    }

    void range(const RationalRange& range, const std::string& path) {
        rate(range.minimum, path);
        rate(range.maximum, path);
        if (range.minimum.valid() && range.maximum.valid() && range.minimum > range.maximum) {
            error(ValidationCode::invalid_range, path);
        }
    }

    void mode(const DisplayMode& mode, const std::string& path) {
        dimensions(mode.pixels, path);
        dimensions(mode.logical, path);
        rate(mode.refresh_rate, path);
    }

    // ponytail: quadratic scan; these sets hold at most a few hundred entries.
    template <class T>
    void distinct(const std::vector<T>& values, const std::string& path) {
        for (auto it = values.begin(); it != values.end(); ++it) {
            if (std::find(std::next(it), values.end(), *it) != values.end()) {
                error(ValidationCode::duplicate_entry, path);
                return;
            }
        }
    }

    void text(const std::string& value, const std::string& path) {
        if (value.empty() || value.size() > kMaxTextBytes) {
            error(ValidationCode::invalid_text, path);
        }
    }

    // --- sections ----------------------------------------------------------------------------

    void check_header() {
        const auto& header = snapshot_.header;
        if (header.schema_id != kSchemaId || !is_supported(header.schema_version)) {
            error(ValidationCode::unsupported_schema, "header.schema");
        }
        if (header.generation == 0) {
            error(ValidationCode::invalid_generation, "header.generation");
        }
    }

    void check_probes() {
        for (const auto& record : snapshot_.probes) {
            const auto path = "probes[" + record.probe_id + "]";
            if (!is_identifier(record.probe_id)) {
                error(ValidationCode::invalid_identifier, path);
            }
            if (!probe_ids_.insert(record.probe_id).second) {
                error(ValidationCode::duplicate_id, path);
            }
            if (record.generation == 0 || record.generation > snapshot_.header.generation) {
                error(ValidationCode::invalid_generation, path + ".generation");
            }
            if (record.duration.count() < 0) {
                error(ValidationCode::invalid_quantity, path + ".duration");
            }
        }
        for (const auto& issue : snapshot_.issues) {
            if (!probe_ids_.contains(issue.probe_id)) {
                error(ValidationCode::missing_probe_reference, "issues[" + issue.probe_id + "]");
            }
        }
    }

    void check_hardware() {
        const auto& cpu = snapshot_.hardware.cpu;
        const std::string base = "hardware.cpu.";

        const auto* native = fact(cpu.native_architecture, base + "native_architecture");
        const auto* process = fact(cpu.process_architecture, base + "process_architecture");
        const auto* translation = fact(cpu.translation, base + "translation");
        if (native && process && translation &&
            (*native != *process) != (*translation == TranslationState::translated)) {
            error(ValidationCode::contradictory_evidence, base + "translation");
        }

        const auto* physical = fact(cpu.physical_cores, base + "physical_cores");
        const auto* logical = fact(cpu.logical_cores, base + "logical_cores");
        const auto* performance = fact(cpu.performance_cores, base + "performance_cores");
        const auto* efficiency = fact(cpu.efficiency_cores, base + "efficiency_cores");
        if (physical) {
            positive(*physical, base + "physical_cores");
        }
        if (logical) {
            positive(*logical, base + "logical_cores");
        }
        if (physical && logical && *logical < *physical) {
            error(ValidationCode::invalid_range, base + "logical_cores");
        }
        if (physical && performance && efficiency && std::uint64_t{*performance} + *efficiency > *physical) {
            error(ValidationCode::invalid_range, base + "performance_cores");
        }
        if (const auto* simd = fact(cpu.simd, base + "simd")) {
            distinct(*simd, base + "simd");
        }

        if (const auto* memory = fact(snapshot_.hardware.installed_memory, "hardware.installed_memory")) {
            positive(memory->value, "hardware.installed_memory");
        }
        fact(snapshot_.hardware.platform_role, "hardware.platform_role");
    }

    void check_gpus() {
        for (const auto& gpu : snapshot_.devices.gpus) {
            const auto base = at("devices.gpus", gpu.id);
            fact(gpu.vendor_id, base + ".vendor_id");
            fact(gpu.device_id, base + ".device_id");
            if (const auto* name = fact(gpu.name, base + ".name")) {
                text(*name, base + ".name");
            }
            fact(gpu.kind, base + ".kind");
            fact(gpu.dedicated_memory, base + ".dedicated_memory");
            fact(gpu.shared_memory, base + ".shared_memory");
            fact(gpu.unified_memory, base + ".unified_memory");
            if (const auto* apis = fact(gpu.graphics_apis, base + ".graphics_apis")) {
                distinct(*apis, base + ".graphics_apis");
            }
            fact(gpu.preferred_for_minimum_power, base + ".preferred_for_minimum_power");
            fact(gpu.preferred_for_high_performance, base + ".preferred_for_high_performance");
        }
    }

    void check_displays() {
        for (const auto& display : snapshot_.devices.displays) {
            const auto base = at("devices.displays", display.id);
            if (const auto* gpu = fact(display.gpu, base + ".gpu")) {
                reference(gpu_ids_, *gpu, ValidationCode::missing_gpu_reference, base + ".gpu");
            }
            if (const auto* modes = fact(display.modes, base + ".modes")) {
                for (const auto& display_mode : *modes) {
                    mode(display_mode, base + ".modes");
                }
                distinct(*modes, base + ".modes");
            }
            support(display.hdr, base + ".hdr");
            fact(display.gamut, base + ".gamut");
            if (const auto* bits = fact(display.bits_per_channel, base + ".bits_per_channel")) {
                positive(*bits, base + ".bits_per_channel");
            }
        }
    }

    void check_encoders() {
        for (const auto& encoder : snapshot_.devices.encoders) {
            const auto base = at("devices.encoders", encoder.id);
            if (platform_of(encoder.backend) != snapshot_.platform.os) {
                error(ValidationCode::platform_mismatch, base + ".backend");
            }
            if (const auto* gpu = fact(encoder.gpu, base + ".gpu")) {
                if (encoder.implementation == ImplementationClass::software) {
                    error(ValidationCode::unexpected_gpu_reference, base + ".gpu");
                } else {
                    reference(gpu_ids_, *gpu, ValidationCode::missing_gpu_reference, base + ".gpu");
                }
            }
            if (const auto* name = fact(encoder.name, base + ".name")) {
                text(*name, base + ".name");
            }
            support(encoder.support, base + ".support");
            std::vector<EncoderModeKey> keys;
            std::ranges::transform(encoder.modes, std::back_inserter(keys), mode_key);
            distinct(keys, base + ".modes");
            for (std::size_t index = 0; index < encoder.modes.size(); ++index) {
                check_encoder_mode(encoder, encoder.modes[index], base + ".modes[" + std::to_string(index) + "]");
            }
        }
    }

    void check_encoder_mode(const EncoderCapability& encoder, const EncoderModeCapability& mode, const std::string& base) {
        if (mode.codec != encoder.codec) {
            error(ValidationCode::codec_mismatch, base + ".codec");
        }
        if (const auto* profile = fact(mode.profile, base + ".profile"); profile && codec_of(*profile) != encoder.codec) {
            error(ValidationCode::codec_mismatch, base + ".profile");
        }
        if (const auto* dimension_range = fact(mode.dimensions, base + ".dimensions")) {
            range(*dimension_range, base + ".dimensions");
        }
        if (const auto* frame_rates = fact(mode.frame_rates, base + ".frame_rates")) {
            range(*frame_rates, base + ".frame_rates");
        }
        const auto* bit_depth = fact(mode.bit_depth, base + ".bit_depth");
        if (bit_depth) {
            positive(*bit_depth, base + ".bit_depth");
        }
        fact(mode.color_range, base + ".color_range");
        const auto* transfer = fact(mode.transfer_function, base + ".transfer_function");
        if (const auto* hdr = fact(mode.hdr, base + ".hdr"); hdr && !color_consistent(*hdr, transfer, bit_depth)) {
            error(ValidationCode::inconsistent_color_format, base + ".hdr");
        }
        support(mode.low_latency, base + ".low_latency");
        support(mode.support, base + ".support");
    }

    void check_capture_paths() {
        for (const auto& capture : snapshot_.devices.capture_paths) {
            const auto base = at("devices.capture_paths", capture.id);
            if (platform_of(capture.api) != snapshot_.platform.os) {
                error(ValidationCode::platform_mismatch, base + ".api");
            }
            support(capture.support, base + ".support");
            if (const auto* gpu = fact(capture.gpu, base + ".gpu")) {
                reference(gpu_ids_, *gpu, ValidationCode::missing_gpu_reference, base + ".gpu");
            }
            if (const auto* formats = fact(capture.output_formats, base + ".output_formats")) {
                distinct(*formats, base + ".output_formats");
            }
            support(capture.hdr_output, base + ".hdr_output");
            if (const auto* frame_rates = fact(capture.frame_rates, base + ".frame_rates")) {
                range(*frame_rates, base + ".frame_rates");
            }
        }
    }

    void check_transfers() {
        std::set<std::tuple<CapturePathId, EncoderId, std::optional<GpuId>>> seen;
        for (const auto& transfer : snapshot_.devices.transfer_paths) {
            const auto base = "devices.transfer_paths[" + transfer.source.value + "->" + transfer.destination.value + "]";
            if (!seen.emplace(transfer.source, transfer.destination, transfer.source_gpu).second) {
                error(ValidationCode::duplicate_entry, base);
            }
            reference(capture_path_ids_, transfer.source, ValidationCode::missing_capture_path_reference, base + ".source");

            const auto& encoders = snapshot_.devices.encoders;
            const auto encoder = std::ranges::find(encoders, transfer.destination, &EncoderCapability::id);
            if (encoder == encoders.end()) {
                error(ValidationCode::missing_encoder_reference, base + ".destination");
            }
            if (transfer.source_gpu) {
                reference(gpu_ids_, *transfer.source_gpu, ValidationCode::missing_gpu_reference, base + ".source_gpu");
            }
            if (transfer.destination_gpu) {
                reference(gpu_ids_, *transfer.destination_gpu, ValidationCode::missing_gpu_reference, base + ".destination_gpu");
                // The destination adapter must be the encoder's proven adapter, never fabricated.
                if (encoder != encoders.end() && encoder->gpu.value() != transfer.destination_gpu) {
                    error(ValidationCode::invalid_transfer, base + ".destination_gpu");
                }
            }
            if (encoder != encoders.end() && encoder->implementation == ImplementationClass::software &&
                transfer.transfer != TransferKind::cpu_staging && transfer.transfer != TransferKind::unknown) {
                error(ValidationCode::invalid_transfer, base + ".transfer");
            }
            if (transfer.source_gpu && transfer.destination_gpu) {
                const bool same_adapter = *transfer.source_gpu == *transfer.destination_gpu;
                const bool claims_same_adapter = transfer.transfer == TransferKind::same_resource ||
                                                 transfer.transfer == TransferKind::same_adapter_copy;
                if ((claims_same_adapter && !same_adapter) ||
                    (transfer.transfer == TransferKind::cross_adapter_copy && same_adapter)) {
                    error(ValidationCode::invalid_transfer, base + ".transfer");
                }
            }
            if (const auto* conversions = fact(transfer.conversions, base + ".conversions")) {
                distinct(*conversions, base + ".conversions");
            }
            support(transfer.evidence, base + ".evidence");
        }
    }

    void check_audio() {
        for (const auto& endpoint : snapshot_.devices.audio_endpoints) {
            const auto base = at("devices.audio_endpoints", endpoint.id);
            if (const auto* name = fact(endpoint.name, base + ".name")) {
                text(*name, base + ".name");
            }
            if (const auto* channels = fact(endpoint.channels, base + ".channels")) {
                positive(*channels, base + ".channels");
            }
            if (const auto* sample_rate = fact(endpoint.sample_rate_hz, base + ".sample_rate_hz")) {
                positive(*sample_rate, base + ".sample_rate_hz");
            }
            if (const auto* formats = fact(endpoint.sample_formats, base + ".sample_formats")) {
                distinct(*formats, base + ".sample_formats");
            }
        }
    }

    void check_runtime() {
        const auto& runtime = snapshot_.runtime;
        const auto* source = fact(runtime.power_source, "runtime.power_source");
        const auto* battery = fact(runtime.battery_present, "runtime.battery_present");
        if (source && battery && *source == PowerSource::battery && !*battery) {
            error(ValidationCode::contradictory_evidence, "runtime.power_source");
        }
        fact(runtime.low_power_mode, "runtime.low_power_mode");
        fact(runtime.thermal, "runtime.thermal");
        fact(runtime.memory_pressure, "runtime.memory_pressure");
        fact(runtime.remote_session, "runtime.remote_session");
        fact(runtime.headless, "runtime.headless");

        std::set<DisplayId> displays;
        std::size_t primaries = 0;
        for (const auto& state : runtime.displays) {
            const auto base = at("runtime.displays", state.display);
            reference(display_ids_, state.display, ValidationCode::missing_display_reference, base);
            if (!displays.insert(state.display).second) {
                error(ValidationCode::duplicate_id, base);
            }
            if (const auto* active = fact(state.active_mode, base + ".active_mode")) {
                mode(*active, base + ".active_mode");
            }
            if (const auto* scale = fact(state.scale, base + ".scale")) {
                rate(*scale, base + ".scale");
            }
            if (const auto* primary = fact(state.primary, base + ".primary"); primary && *primary) {
                ++primaries;
            }
            fact(state.hdr_enabled, base + ".hdr_enabled");
        }
        if (primaries > 1) {
            error(ValidationCode::contradictory_evidence, "runtime.displays.primary");
        }

        std::set<AudioEndpointId> endpoints;
        for (const auto& state : runtime.audio_endpoints) {
            const auto base = at("runtime.audio_endpoints", state.endpoint);
            reference(audio_ids_, state.endpoint, ValidationCode::missing_audio_endpoint_reference, base);
            if (!endpoints.insert(state.endpoint).second) {
                error(ValidationCode::duplicate_id, base);
            }
            fact(state.active, base + ".active");
            if (const auto* roles = fact(state.default_roles, base + ".default_roles")) {
                distinct(*roles, base + ".default_roles");
            }
        }

        std::set<CapturePathId> paths;
        for (const auto& state : runtime.capture_permissions) {
            const auto base = at("runtime.capture_permissions", state.path);
            reference(capture_path_ids_, state.path, ValidationCode::missing_capture_path_reference, base);
            if (!paths.insert(state.path).second) {
                error(ValidationCode::duplicate_id, base);
            }
            fact(state.permission, base + ".permission");
        }
    }

    const CapabilitySnapshot& snapshot_;
    std::vector<ValidationError> errors_;
    std::set<std::string> probe_ids_;
    std::set<GpuId> gpu_ids_;
    std::set<DisplayId> display_ids_;
    std::set<EncoderId> encoder_ids_;
    std::set<CapturePathId> capture_path_ids_;
    std::set<AudioEndpointId> audio_ids_;
};

} // namespace

ValidationReport validate(const CapabilitySnapshot& snapshot) {
    return Validator(snapshot).run();
}

} // namespace catro::capabilities
