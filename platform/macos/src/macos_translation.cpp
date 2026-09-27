#include "macos_translation.hpp"

#include <catro/capabilities/validation.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <set>
#include <utility>

namespace catro::platform::macos {
namespace {

std::string hex16(std::uint64_t value) {
    char text[17]{};
    std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(value));
    return text;
}

// Longest prefix within the text bound that ends on a UTF-8 code point boundary.
std::string bounded_text(std::string_view text) {
    if (text.size() <= caps::kMaxTextBytes) {
        return std::string(text);
    }
    auto size = caps::kMaxTextBytes;
    while (size > 0 && (static_cast<unsigned char>(text[size]) & 0xC0U) == 0x80U) {
        --size;
    }
    return std::string(text.substr(0, size));
}

bool identifier(std::string_view value) {
    return !value.empty() && value.size() <= caps::kMaxIdentifierBytes &&
           std::ranges::all_of(value, [](char c) { return c > ' ' && c <= '~'; });
}

std::string lowercase(std::string_view text) {
    std::string result;
    std::ranges::transform(text, std::back_inserter(result),
                           [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; });
    return result;
}

std::string_view codec_name(caps::Codec codec) {
    switch (codec) {
    case caps::Codec::h264:
        return "h264";
    case caps::Codec::hevc:
        return "hevc";
    case caps::Codec::av1:
        return "av1";
    }
    return "h264";
}

constexpr std::array kScreenCapturePaths{
    std::pair{kScreenCaptureDisplay, caps::SourceKind::display},
    std::pair{kScreenCaptureWindow, caps::SourceKind::window},
    std::pair{kScreenCaptureApplication, caps::SourceKind::application},
};

class Translator {
public:
    Translator(std::string_view probe_id, std::vector<caps::ProbeIssue>& issues) : probe_id_(probe_id), issues_(issues) {}

    caps::GpuDisplayProbeFacts gpu_display(const NativeGpuDisplay& native) {
        caps::GpuDisplayProbeFacts facts;
        std::set<std::uint64_t> gpus;
        if (native.gpus) {
            for (const auto& gpu : *native.gpus) {
                if (gpus.insert(gpu.registry_id).second) {
                    facts.gpus.push_back(capability(gpu));
                }
            }
        }
        if (native.displays) {
            std::set<std::uint32_t> seen;
            for (const auto& display : *native.displays) {
                if (!seen.insert(display.id).second) {
                    continue;
                }
                facts.displays.push_back(capability(display, native.gpus ? &gpus : nullptr));
                facts.display_states.push_back(state(display));
            }
        }

        for (const auto& [path, source] : kScreenCapturePaths) {
            facts.capture_paths.push_back(capture(path, source, native.capture.screen_capture_kit));
        }
        if (native.capture.screen_capture_kit) {
            for (const auto& path : facts.capture_paths) {
                facts.capture_permissions.push_back(caps::CapturePermissionState{
                    .path = path.id,
                    .permission = native.capture.access_granted
                                      ? caps::Observed<caps::CapturePermission>::known(
                                            caps::CapturePermission::granted, measured())
                                      : unknown<caps::CapturePermission>(caps::IssueCode::not_reported),
                });
            }
        }
        return facts;
    }

    caps::EncoderProbeFacts encoders(const std::vector<NativeEncoder>& native,
                                     const std::optional<std::vector<NativeGpu>>& gpus) {
        std::set<std::uint64_t> registry;
        std::optional<bool> unified;
        if (gpus && !gpus->empty()) {
            unified = std::ranges::all_of(*gpus, &NativeGpu::unified_memory);
            for (const auto& gpu : *gpus) {
                registry.insert(gpu.registry_id);
            }
        }

        caps::EncoderProbeFacts facts;
        std::set<std::string> seen;
        for (const auto& encoder : native) {
            auto id = encoder_id(encoder);
            if (!identifier(encoder.encoder_id) || !identifier(id.value)) {
                absent(caps::IssueCode::not_reported);
                continue;
            }
            // Repeated list entries describe the same encoder.
            if (!seen.insert(id.value).second) {
                continue;
            }
            std::optional<caps::GpuId> gpu;
            if (encoder.hardware && encoder.gpu && registry.contains(*encoder.gpu)) {
                gpu = gpu_id(*encoder.gpu);
            }
            for (const auto& [path, source] : kScreenCapturePaths) {
                facts.transfer_paths.push_back(transfer(path, encoder, id, gpu, unified));
            }
            facts.encoders.push_back(capability(encoder, std::move(id), gpu));
        }
        return facts;
    }

    caps::AudioProbeFacts audio(const std::vector<NativeAudioDevice>& native) {
        caps::AudioProbeFacts facts;
        std::set<std::string> seen;
        for (const auto& device : native) {
            caps::AudioEndpointId id{"coreaudio:" + device.uid +
                                         (device.direction == caps::AudioDirection::input ? ":input" : ":output"),
                                     caps::IdentityScope::persistent};
            if (!identifier(device.uid) || !identifier(id.value)) {
                absent(caps::IssueCode::not_reported);
                continue;
            }
            if (!seen.insert(id.value).second) {
                continue;
            }
            auto name = bounded_text(device.name);
            std::optional<std::uint32_t> channels;
            if (device.channels.value_or(0) > 0) {
                channels = device.channels;
            }
            std::optional<std::uint32_t> sample_rate;
            if (device.sample_rate_hz.value_or(0) > 0) {
                sample_rate = device.sample_rate_hz;
            }
            std::optional<std::vector<caps::SampleFormat>> formats;
            if (device.sample_format) {
                formats = std::vector{*device.sample_format};
            }
            facts.endpoints.push_back(caps::AudioEndpointCapability{
                .id = id,
                .direction = device.direction,
                .name = name.empty() ? unknown<std::string>(caps::IssueCode::not_reported)
                                     : caps::Observed<std::string>::known(std::move(name), advertised()),
                .channels = reported(channels, advertised(), caps::IssueCode::not_reported),
                .sample_rate_hz = reported(sample_rate, advertised(), caps::IssueCode::not_reported),
                .sample_formats = reported(formats, advertised(), caps::IssueCode::not_reported),
            });
            // macOS keeps one default device per direction and uses it for every purpose.
            auto roles = unknown<std::vector<caps::AudioRole>>(caps::IssueCode::os_failure);
            if (device.is_default) {
                roles = caps::Observed<std::vector<caps::AudioRole>>::known(
                    *device.is_default ? std::vector{caps::AudioRole::console, caps::AudioRole::multimedia,
                                                     caps::AudioRole::communications}
                                       : std::vector<caps::AudioRole>{},
                    inferred());
            }
            facts.states.push_back(caps::AudioEndpointState{
                .endpoint = std::move(id),
                .active = caps::Observed<bool>::known(device.alive, measured()),
                .default_roles = std::move(roles),
            });
        }
        return facts;
    }

private:
    [[nodiscard]] caps::Provenance inferred() const {
        return {.probe_id = std::string(probe_id_), .method = caps::EvidenceMethod::inferred};
    }

    [[nodiscard]] caps::Provenance advertised() const {
        return {.probe_id = std::string(probe_id_), .method = caps::EvidenceMethod::advertised};
    }

    [[nodiscard]] caps::Provenance measured() const {
        return {.probe_id = std::string(probe_id_), .method = caps::EvidenceMethod::measured};
    }

    caps::Provenance absent(caps::IssueCode issue) {
        const caps::ProbeIssue record{std::string(probe_id_), issue};
        if (std::ranges::find(issues_, record) == issues_.end()) {
            issues_.push_back(record);
        }
        return {std::string(probe_id_), caps::EvidenceMethod::measured, caps::Confidence::degraded, issue};
    }

    template <class T>
    caps::Observed<T> unknown(caps::IssueCode issue) {
        return caps::Observed<T>::unknown(absent(issue));
    }

    template <class T>
    caps::Observed<T> reported(const std::optional<T>& value, caps::Provenance provenance, caps::IssueCode missing) {
        return value ? caps::Observed<T>::known(*value, std::move(provenance)) : unknown<T>(missing);
    }

    caps::SupportFact support(std::optional<bool> supported, caps::IssueCode missing) {
        if (!supported) {
            return {caps::Support::unknown, absent(missing)};
        }
        return {*supported ? caps::Support::supported : caps::Support::unsupported, advertised()};
    }

    caps::GpuCapability capability(const NativeGpu& gpu) {
        // Metal reports device traits, not a kind: a removable GPU is external, a GPU sharing
        // system memory or ranked low power is integrated, and any other has its own memory.
        const auto kind = gpu.removable                          ? caps::GpuKind::external
                          : gpu.unified_memory || gpu.low_power ? caps::GpuKind::integrated
                                                                : caps::GpuKind::discrete;
        auto name = bounded_text(gpu.name);
        return caps::GpuCapability{
            .id = gpu_id(gpu.registry_id),
            .vendor_id = reported(gpu.vendor_id, advertised(), caps::IssueCode::not_reported),
            .device_id = reported(gpu.device_id, advertised(), caps::IssueCode::not_reported),
            .name = name.empty() ? unknown<std::string>(caps::IssueCode::not_reported)
                                 : caps::Observed<std::string>::known(std::move(name), advertised()),
            .kind = caps::Observed<caps::GpuKind>::known(kind, inferred()),
            // Metal reports a recommended working set, which is neither dedicated nor shared memory.
            .dedicated_memory = unknown<caps::Bytes>(caps::IssueCode::not_reported),
            .shared_memory = unknown<caps::Bytes>(caps::IssueCode::not_reported),
            .unified_memory = caps::Observed<bool>::known(gpu.unified_memory, advertised()),
            .graphics_apis = caps::Observed<std::vector<caps::GraphicsApi>>::known({caps::GraphicsApi::metal},
                                                                                  advertised()),
            .preferred_for_minimum_power = unknown<bool>(caps::IssueCode::not_reported),
            .preferred_for_high_performance = unknown<bool>(caps::IssueCode::not_reported),
        };
    }

    static std::optional<caps::DisplayMode> mode(const NativeDisplayMode& native) {
        const auto refresh = exact_refresh(native.refresh_hz);
        if (!refresh || native.pixel_width == 0 || native.pixel_height == 0 || native.point_width == 0 ||
            native.point_height == 0) {
            return std::nullopt;
        }
        return caps::DisplayMode{
            .pixels = {native.pixel_width, native.pixel_height},
            .logical = {native.point_width, native.point_height},
            .refresh_rate = *refresh,
        };
    }

    caps::DisplayCapability capability(const NativeDisplay& display, const std::set<std::uint64_t>* gpus) {
        const bool proven = gpus != nullptr && display.gpu && gpus->contains(*display.gpu);
        auto modes = unknown<std::vector<caps::DisplayMode>>(caps::IssueCode::not_reported);
        if (display.modes) {
            std::vector<caps::DisplayMode> described;
            for (const auto& native : *display.modes) {
                // Modes without a fixed rate state nothing exact; duplicates add nothing.
                const auto translated = mode(native);
                if (translated && std::ranges::find(described, *translated) == described.end()) {
                    described.push_back(*translated);
                }
            }
            if (!described.empty()) {
                modes = caps::Observed<std::vector<caps::DisplayMode>>::known(std::move(described), advertised());
            }
        }
        std::optional<std::uint8_t> bits;
        if (display.bits_per_channel.value_or(0) > 0) {
            bits = display.bits_per_channel;
        }
        return caps::DisplayCapability{
            .id = display_id(display.id),
            .gpu = proven ? caps::Observed<caps::GpuId>::known(gpu_id(*display.gpu), advertised())
                          : unknown<caps::GpuId>(caps::IssueCode::relationship_unprovable),
            .modes = std::move(modes),
            .hdr = support(display.hdr_supported, caps::IssueCode::not_reported),
            .gamut = reported(display.gamut, advertised(), caps::IssueCode::not_reported),
            .bits_per_channel = reported(bits, advertised(), caps::IssueCode::not_reported),
        };
    }

    caps::DisplayState state(const NativeDisplay& display) {
        std::optional<caps::DisplayMode> active;
        std::optional<caps::Rational> scale;
        if (display.active) {
            active = mode(*display.active);
            if (display.active->point_width > 0 && display.active->pixel_width > 0) {
                scale = caps::Rational{display.active->pixel_width, display.active->point_width};
            }
        }
        return caps::DisplayState{
            .display = display_id(display.id),
            .active_mode = reported(active, measured(), caps::IssueCode::not_reported),
            .scale = reported(scale, measured(), caps::IssueCode::not_reported),
            // CoreGraphics names exactly one main display.
            .primary = caps::Observed<bool>::known(display.main, measured()),
            // EDR headroom is granted per window on request; a probe cannot see it in use.
            .hdr_enabled = unknown<bool>(caps::IssueCode::not_reported),
        };
    }

    caps::CapturePathCapability capture(std::string_view path, caps::SourceKind source, bool available) {
        return caps::CapturePathCapability{
            .id = {std::string(path), caps::IdentityScope::persistent},
            .api = caps::CaptureApi::screen_capture_kit,
            .source = source,
            .support = support(available, caps::IssueCode::api_unavailable),
            // Captured surfaces are shared memory; the producing GPU is never assumed.
            .gpu = unknown<caps::GpuId>(caps::IssueCode::relationship_unprovable),
            .output_formats = available
                                  ? caps::Observed<std::vector<caps::PixelFormat>>::known(
                                        {caps::PixelFormat::bgra8, caps::PixelFormat::nv12}, advertised())
                                  : caps::Observed<std::vector<caps::PixelFormat>>::unavailable(
                                        absent(caps::IssueCode::not_applicable)),
            .hdr_output = available ? caps::SupportFact{caps::Support::unknown, absent(caps::IssueCode::not_reported)}
                                    : support(false, caps::IssueCode::api_unavailable),
            .frame_rates = unknown<caps::RationalRange>(caps::IssueCode::not_reported),
        };
    }

    caps::EncoderCapability capability(const NativeEncoder& encoder, caps::EncoderId id,
                                       const std::optional<caps::GpuId>& gpu) {
        auto device = unknown<caps::GpuId>(caps::IssueCode::relationship_unprovable);
        if (!encoder.hardware) {
            device = caps::Observed<caps::GpuId>::unavailable(absent(caps::IssueCode::not_applicable));
        } else if (gpu) {
            device = caps::Observed<caps::GpuId>::known(*gpu, advertised());
        }
        auto name = bounded_text(encoder.name);
        return caps::EncoderCapability{
            .id = std::move(id),
            .codec = encoder.codec,
            .backend = caps::EncoderBackend::video_toolbox,
            .implementation = encoder.hardware ? caps::ImplementationClass::hardware
                                               : caps::ImplementationClass::software,
            .gpu = std::move(device),
            .name = name.empty() ? unknown<std::string>(caps::IssueCode::not_reported)
                                 : caps::Observed<std::string>::known(std::move(name), advertised()),
            .support = {caps::Support::supported, advertised()},
            .modes = {nv12_mode(encoder.codec)},
        };
    }

    // Profiles, limits, color signalling, and low-latency support need a compression session;
    // passive discovery leaves them unreported.
    caps::EncoderModeCapability nv12_mode(caps::Codec codec) {
        return caps::EncoderModeCapability{
            .codec = codec,
            .profile = unknown<caps::CodecProfile>(caps::IssueCode::not_reported),
            .dimensions = unknown<caps::DimensionRange>(caps::IssueCode::not_reported),
            .frame_rates = unknown<caps::RationalRange>(caps::IssueCode::not_reported),
            .input_format = caps::PixelFormat::nv12,
            .chroma = caps::ChromaSubsampling::yuv420,
            .bit_depth = caps::Observed<std::uint8_t>::known(8, inferred()),
            .color_range = unknown<caps::ColorRange>(caps::IssueCode::not_reported),
            .transfer_function = unknown<caps::TransferFunction>(caps::IssueCode::not_reported),
            .hdr = caps::Observed<caps::HdrMode>::known(caps::HdrMode::sdr, inferred()),
            .low_latency = {caps::Support::unknown, absent(caps::IssueCode::not_reported)},
            .support = {caps::Support::supported, inferred()},
        };
    }

    caps::TransferPathCapability transfer(std::string_view path, const NativeEncoder& encoder,
                                          const caps::EncoderId& id, const std::optional<caps::GpuId>& gpu,
                                          std::optional<bool> unified) {
        caps::TransferPathCapability result{
            .source = {std::string(path), caps::IdentityScope::persistent},
            .destination = id,
            .destination_gpu = encoder.hardware ? gpu : std::nullopt,
            .conversions = unknown<std::vector<caps::Conversion>>(caps::IssueCode::relationship_unprovable),
        };
        if (!encoder.hardware) {
            result.transfer = caps::TransferKind::cpu_staging;
            result.evidence = {caps::Support::supported, inferred()};
        } else if (unified.value_or(false)) {
            result.transfer = caps::TransferKind::same_resource;
            result.evidence = {caps::Support::supported, inferred()};
        } else {
            result.transfer = caps::TransferKind::unknown;
            result.evidence = {caps::Support::unknown, absent(caps::IssueCode::relationship_unprovable)};
        }
        return result;
    }

    std::string_view probe_id_;
    std::vector<caps::ProbeIssue>& issues_;
};

} // namespace

caps::GpuId gpu_id(std::uint64_t registry_id) {
    return {"metal:" + hex16(registry_id), caps::IdentityScope::os_session};
}

caps::DisplayId display_id(std::uint32_t display) {
    return {"cgdisplay:" + std::to_string(display), caps::IdentityScope::os_session};
}

std::optional<caps::Rational> exact_refresh(double hertz) {
    constexpr double kTolerance = 1e-3;
    // The comparison also rejects NaN.
    if (!(hertz > 0.0) || hertz > 10000.0) {
        return std::nullopt;
    }
    const auto whole = std::round(hertz);
    if (whole >= 1.0 && std::abs(hertz - whole) < kTolerance) {
        return caps::Rational{static_cast<std::uint32_t>(whole), 1};
    }
    const auto broadcast = std::round(hertz * 1.001);
    if (broadcast >= 1.0 && std::abs(hertz - broadcast * 1000.0 / 1001.0) < kTolerance) {
        return caps::Rational{static_cast<std::uint32_t>(broadcast) * 1000U, 1001};
    }
    return caps::Rational{static_cast<std::uint32_t>(std::round(hertz * 1000.0)), 1000};
}

caps::EncoderId encoder_id(const NativeEncoder& encoder) {
    // Registered encoder names are stable across restarts.
    return {"vt:" + std::string(codec_name(encoder.codec)) + (encoder.hardware ? ":hardware:" : ":software:") +
                lowercase(encoder.encoder_id),
            caps::IdentityScope::persistent};
}

caps::GpuDisplayProbeFacts translate_gpu_display(const NativeGpuDisplay& native, std::string_view probe_id,
                                                 std::vector<caps::ProbeIssue>& issues) {
    return Translator(probe_id, issues).gpu_display(native);
}

caps::EncoderProbeFacts translate_encoders(const std::vector<NativeEncoder>& encoders,
                                           const std::optional<std::vector<NativeGpu>>& gpus,
                                           std::string_view probe_id, std::vector<caps::ProbeIssue>& issues) {
    return Translator(probe_id, issues).encoders(encoders, gpus);
}

caps::AudioProbeFacts translate_audio(const std::vector<NativeAudioDevice>& devices, std::string_view probe_id,
                                      std::vector<caps::ProbeIssue>& issues) {
    return Translator(probe_id, issues).audio(devices);
}

} // namespace catro::platform::macos
