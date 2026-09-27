#include "windows_translation.hpp"

#include <catro/capabilities/validation.hpp>

#include <algorithm>
#include <iterator>
#include <cstdio>
#include <set>
#include <utility>

namespace catro::platform::windows {
namespace {

std::string hex8(std::uint32_t value) {
    char text[9]{};
    std::snprintf(text, sizeof(text), "%08x", value);
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

std::uint32_t logical(std::uint32_t pixels, std::uint32_t dpi) {
    const auto scaled = (std::uint64_t{pixels} * 96U + dpi / 2U) / dpi;
    return static_cast<std::uint32_t>(std::max<std::uint64_t>(scaled, 1U));
}

class Translator {
public:
    Translator(std::string_view probe_id, std::vector<caps::ProbeIssue>& issues) : probe_id_(probe_id), issues_(issues) {}

    caps::GpuDisplayProbeFacts run(const NativeGpuDisplay& native) {
        caps::GpuDisplayProbeFacts facts;
        std::set<NativeLuid> adapters;
        if (native.adapters) {
            for (const auto& adapter : *native.adapters) {
                // One entry per LUID; a repeated adapter would make the snapshot invalid.
                if (adapters.insert(adapter.luid).second) {
                    facts.gpus.push_back(gpu(adapter));
                }
            }
        }
        if (native.displays) {
            const auto primaries = std::ranges::count_if(*native.displays, &NativeDisplay::primary);
            for (const auto& display : *native.displays) {
                facts.displays.push_back(capability(display, native.adapters ? &adapters : nullptr));
                facts.display_states.push_back(state(display, primaries <= 1));
            }
        }

        const auto& apis = native.capture;
        const std::vector<caps::PixelFormat> graphics_capture_formats{caps::PixelFormat::bgra8,
                                                                      caps::PixelFormat::rgba16f};
        auto duplication_formats = std::vector{caps::PixelFormat::bgra8};
        if (apis.desktop_duplication_hdr) {
            duplication_formats.push_back(caps::PixelFormat::rgba16f);
        }
        facts.capture_paths = {
            capture(std::string(kGraphicsCaptureDisplay), caps::CaptureApi::windows_graphics_capture, caps::SourceKind::display,
                    apis.graphics_capture, caps::IssueCode::api_unavailable, graphics_capture_formats, true),
            capture(std::string(kGraphicsCaptureWindow), caps::CaptureApi::windows_graphics_capture, caps::SourceKind::window,
                    apis.graphics_capture, caps::IssueCode::api_unavailable, graphics_capture_formats, true),
            capture(std::string(kDesktopDuplicationDisplay), caps::CaptureApi::desktop_duplication, caps::SourceKind::display,
                    apis.desktop_duplication, caps::IssueCode::not_applicable, duplication_formats,
                    apis.desktop_duplication_hdr),
        };
        // Desktop applications need no user consent for these APIs, so a usable path is granted.
        for (const auto& path : facts.capture_paths) {
            if (path.support.status == caps::Support::supported) {
                facts.capture_permissions.push_back(caps::CapturePermissionState{
                    .path = path.id,
                    .permission = caps::Observed<caps::CapturePermission>::known(caps::CapturePermission::granted,
                                                                                advertised()),
                });
            }
        }
        return facts;
    }

    caps::EncoderProbeFacts encoders(const std::vector<NativeEncoder>& native,
                                     const std::vector<NativeLuid>& output_adapters) {
        caps::EncoderProbeFacts facts;
        std::set<std::string> seen;
        for (const auto& encoder : native) {
            auto id = encoder_id(encoder);
            if (!identifier(encoder.clsid) || !identifier(id.value)) {
                absent(caps::IssueCode::not_reported);
                continue;
            }
            // A transform registered for several matching types is listed more than once.
            if (!seen.insert(id.value).second) {
                continue;
            }
            facts.transfer_paths.push_back(transfer(kGraphicsCaptureDisplay, encoder, id));
            facts.transfer_paths.push_back(transfer(kGraphicsCaptureWindow, encoder, id));
            const std::set<NativeLuid> sources(output_adapters.begin(), output_adapters.end());
            if (!encoder.hardware || !encoder.adapter || sources.empty()) {
                facts.transfer_paths.push_back(transfer(kDesktopDuplicationDisplay, encoder, id));
            } else {
                for (const auto source : sources) {
                    facts.transfer_paths.push_back(duplication(source, *encoder.adapter, id));
                }
            }
            facts.encoders.push_back(capability(encoder, std::move(id)));
        }
        return facts;
    }

    caps::AudioProbeFacts audio(const std::vector<NativeAudioEndpoint>& native) {
        caps::AudioProbeFacts facts;
        std::set<std::string> seen;
        for (const auto& endpoint : native) {
            caps::AudioEndpointId id{"mmdevice:" + endpoint.id, caps::IdentityScope::persistent};
            if (!identifier(endpoint.id) || !identifier(id.value)) {
                absent(caps::IssueCode::not_reported);
                continue;
            }
            if (!seen.insert(id.value).second) {
                continue;
            }
            auto name = bounded_text(endpoint.name);
            std::optional<std::uint32_t> channels;
            if (endpoint.channels.value_or(0) > 0) {
                channels = endpoint.channels;
            }
            std::optional<std::uint32_t> sample_rate;
            if (endpoint.sample_rate_hz.value_or(0) > 0) {
                sample_rate = endpoint.sample_rate_hz;
            }
            std::optional<std::vector<caps::SampleFormat>> formats;
            if (endpoint.sample_format) {
                formats = std::vector{*endpoint.sample_format};
            }
            facts.endpoints.push_back(caps::AudioEndpointCapability{
                .id = id,
                .direction = endpoint.direction,
                .name = name.empty() ? unknown<std::string>(caps::IssueCode::not_reported)
                                     : caps::Observed<std::string>::known(std::move(name), advertised()),
                .channels = reported(channels, advertised(), caps::IssueCode::not_reported),
                .sample_rate_hz = reported(sample_rate, advertised(), caps::IssueCode::not_reported),
                .sample_formats = reported(formats, advertised(), caps::IssueCode::not_reported),
            });
            auto roles = endpoint.default_roles;
            if (roles) {
                std::ranges::sort(*roles);
                roles->erase(std::ranges::unique(*roles).begin(), roles->end());
            }
            facts.states.push_back(caps::AudioEndpointState{
                .endpoint = std::move(id),
                .active = caps::Observed<bool>::known(endpoint.state == NativeEndpointState::active, measured()),
                .default_roles = reported(roles, measured(), caps::IssueCode::os_failure),
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

    [[nodiscard]] caps::Provenance validated() const {
        return {.probe_id = std::string(probe_id_), .method = caps::EvidenceMethod::probe_validated};
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

    caps::GpuCapability gpu(const NativeAdapter& adapter) {
        std::optional<caps::GpuKind> kind;
        if (adapter.software) {
            kind = caps::GpuKind::software;
        } else if (adapter.detachable.value_or(false)) {
            kind = caps::GpuKind::external;
        } else if (adapter.integrated) {
            kind = *adapter.integrated ? caps::GpuKind::integrated : caps::GpuKind::discrete;
        }
        std::vector<caps::GraphicsApi> apis;
        if (adapter.direct3d11) {
            apis.push_back(caps::GraphicsApi::direct3d11);
        }
        if (adapter.direct3d12) {
            apis.push_back(caps::GraphicsApi::direct3d12);
        }
        auto name = bounded_text(adapter.description);
        return caps::GpuCapability{
            .id = gpu_id(adapter.luid),
            .vendor_id = caps::Observed<std::uint32_t>::known(adapter.vendor_id, advertised()),
            .device_id = caps::Observed<std::uint32_t>::known(adapter.device_id, advertised()),
            .name = name.empty() ? unknown<std::string>(caps::IssueCode::not_reported)
                                 : caps::Observed<std::string>::known(std::move(name), advertised()),
            .kind = reported(kind, advertised(), caps::IssueCode::not_reported),
            .dedicated_memory = caps::Observed<caps::Bytes>::known(caps::Bytes{adapter.dedicated_memory}, advertised()),
            .shared_memory = caps::Observed<caps::Bytes>::known(caps::Bytes{adapter.shared_memory}, advertised()),
            // Unified memory needs a device-level architecture query; discovery does not create one.
            .unified_memory = unknown<bool>(caps::IssueCode::not_reported),
            .graphics_apis = caps::Observed<std::vector<caps::GraphicsApi>>::known(std::move(apis), validated()),
            .preferred_for_minimum_power =
                reported(adapter.preferred_for_minimum_power, advertised(), caps::IssueCode::api_unavailable),
            .preferred_for_high_performance =
                reported(adapter.preferred_for_high_performance, advertised(), caps::IssueCode::api_unavailable),
        };
    }

    caps::DisplayCapability capability(const NativeDisplay& display, const std::set<NativeLuid>* adapters) {
        const bool proven = adapters != nullptr && adapters->contains(display.adapter);
        std::optional<std::uint8_t> bits;
        if (display.bits_per_channel.value_or(0) > 0) {
            bits = display.bits_per_channel;
        }
        return caps::DisplayCapability{
            .id = display_id(display.adapter, display.target_id),
            .gpu = proven ? caps::Observed<caps::GpuId>::known(gpu_id(display.adapter), advertised())
                          : unknown<caps::GpuId>(caps::IssueCode::relationship_unprovable),
            // The display configuration reports only the active mode exactly; integer-Hz mode
            // lists would misstate fractional rates, so the full list stays unreported.
            .modes = unknown<std::vector<caps::DisplayMode>>(caps::IssueCode::not_reported),
            .hdr = support(display.hdr_supported, caps::IssueCode::os_failure),
            .gamut = unknown<caps::ColorGamut>(caps::IssueCode::not_reported),
            .bits_per_channel = reported(bits, advertised(), caps::IssueCode::not_reported),
        };
    }

    caps::DisplayState state(const NativeDisplay& display, bool single_primary) {
        const caps::Rational refresh{display.refresh_numerator, display.refresh_denominator};
        auto active = unknown<caps::DisplayMode>(caps::IssueCode::not_reported);
        if (display.width > 0 && display.height > 0 && refresh.valid() && refresh.numerator() > 0) {
            caps::DisplayMode mode{
                .pixels = {display.width, display.height},
                .logical = {display.width, display.height},
                .refresh_rate = refresh,
            };
            if (display.dpi.value_or(0) > 0) {
                mode.logical = {logical(display.width, *display.dpi), logical(display.height, *display.dpi)};
                active = caps::Observed<caps::DisplayMode>::known(mode, measured());
            } else {
                // Exact pixels and rate, but the logical size assumes no scaling.
                active = caps::Observed<caps::DisplayMode>::known(mode, absent(caps::IssueCode::not_reported));
            }
        }
        std::optional<caps::Rational> scale;
        if (display.dpi.value_or(0) > 0) {
            scale = caps::Rational{*display.dpi, 96};
        }
        return caps::DisplayState{
            .display = display_id(display.adapter, display.target_id),
            .active_mode = std::move(active),
            .scale = reported(scale, measured(), caps::IssueCode::not_reported),
            // Cloned targets share the origin source; which one is primary is then unprovable.
            .primary = single_primary || !display.primary
                           ? caps::Observed<bool>::known(display.primary, measured())
                           : unknown<bool>(caps::IssueCode::relationship_unprovable),
            .hdr_enabled = reported(display.hdr_enabled, measured(), caps::IssueCode::os_failure),
        };
    }

    caps::CapturePathCapability capture(std::string id, caps::CaptureApi api, caps::SourceKind source,
                                        std::optional<bool> supported, caps::IssueCode missing,
                                        const std::vector<caps::PixelFormat>& formats, bool hdr) {
        const bool usable = supported.value_or(false);
        return caps::CapturePathCapability{
            .id = {std::move(id), caps::IdentityScope::persistent},
            .api = api,
            .source = source,
            .support = support(supported, missing),
            // Capture may cross adapters; the producing adapter is never assumed.
            .gpu = unknown<caps::GpuId>(caps::IssueCode::relationship_unprovable),
            .output_formats = usable ? caps::Observed<std::vector<caps::PixelFormat>>::known(formats, advertised())
                                     : caps::Observed<std::vector<caps::PixelFormat>>::unavailable(
                                           absent(caps::IssueCode::not_applicable)),
            .hdr_output = usable ? support(hdr, caps::IssueCode::not_reported) : support(supported, missing),
            .frame_rates = unknown<caps::RationalRange>(caps::IssueCode::not_reported),
        };
    }

    caps::EncoderCapability capability(const NativeEncoder& encoder, caps::EncoderId id) {
        auto gpu = unknown<caps::GpuId>(caps::IssueCode::relationship_unprovable);
        if (!encoder.hardware) {
            gpu = caps::Observed<caps::GpuId>::unavailable(absent(caps::IssueCode::not_applicable));
        } else if (encoder.adapter) {
            gpu = caps::Observed<caps::GpuId>::known(gpu_id(*encoder.adapter), advertised());
        }
        auto name = bounded_text(encoder.name);
        caps::EncoderCapability result{
            .id = std::move(id),
            .codec = encoder.codec,
            .backend = caps::EncoderBackend::media_foundation,
            .implementation = encoder.hardware ? caps::ImplementationClass::hardware : caps::ImplementationClass::software,
            .gpu = std::move(gpu),
            .name = name.empty() ? unknown<std::string>(caps::IssueCode::not_reported)
                                 : caps::Observed<std::string>::known(std::move(name), advertised()),
            .support = {caps::Support::supported, advertised()},
        };
        std::set<caps::PixelFormat> described;
        for (const auto format : encoder.inputs) {
            // RGB inputs fix no encoded chroma layout, so only planar YUV inputs become modes.
            if ((format == caps::PixelFormat::nv12 || format == caps::PixelFormat::p010) &&
                described.insert(format).second) {
                result.modes.push_back(mode(encoder.codec, format));
            }
        }
        return result;
    }

    // Profiles, limits, color signalling, and low-latency support need an activated transform;
    // passive discovery leaves them unreported.
    caps::EncoderModeCapability mode(caps::Codec codec, caps::PixelFormat format) {
        const bool ten_bit = format == caps::PixelFormat::p010;
        return caps::EncoderModeCapability{
            .codec = codec,
            .profile = unknown<caps::CodecProfile>(caps::IssueCode::not_reported),
            .dimensions = unknown<caps::DimensionRange>(caps::IssueCode::not_reported),
            .frame_rates = unknown<caps::RationalRange>(caps::IssueCode::not_reported),
            .input_format = format,
            .chroma = caps::ChromaSubsampling::yuv420,
            .bit_depth = caps::Observed<std::uint8_t>::known(static_cast<std::uint8_t>(ten_bit ? 10 : 8), advertised()),
            .color_range = unknown<caps::ColorRange>(caps::IssueCode::not_reported),
            .transfer_function = unknown<caps::TransferFunction>(caps::IssueCode::not_reported),
            // Eight-bit input cannot carry HDR10 or HLG; ten-bit input may carry either.
            .hdr = ten_bit ? unknown<caps::HdrMode>(caps::IssueCode::not_reported)
                           : caps::Observed<caps::HdrMode>::known(caps::HdrMode::sdr, inferred()),
            .low_latency = {caps::Support::unknown, absent(caps::IssueCode::not_reported)},
            .support = {caps::Support::supported, advertised()},
        };
    }

    caps::TransferPathCapability transfer(std::string_view path, const NativeEncoder& encoder,
                                          const caps::EncoderId& id) {
        caps::TransferPathCapability result{
            .source = {std::string(path), caps::IdentityScope::persistent},
            .destination = id,
            .conversions = unknown<std::vector<caps::Conversion>>(caps::IssueCode::relationship_unprovable),
        };
        if (!encoder.hardware) {
            result.transfer = caps::TransferKind::cpu_staging;
            result.evidence = {caps::Support::supported, inferred()};
            return result;
        }
        if (encoder.adapter) {
            result.destination_gpu = gpu_id(*encoder.adapter);
        }
        result.transfer = caps::TransferKind::unknown;
        result.evidence = {caps::Support::unknown, absent(caps::IssueCode::relationship_unprovable)};
        return result;
    }

    // The duplicated frame is a texture on the output's adapter; the encoder needs its own input
    // surface on its adapter, so the frame is always copied at least once.
    caps::TransferPathCapability duplication(NativeLuid source, NativeLuid encoder, const caps::EncoderId& id) {
        return caps::TransferPathCapability{
            .source = {std::string(kDesktopDuplicationDisplay), caps::IdentityScope::persistent},
            .destination = id,
            .source_gpu = gpu_id(source),
            .destination_gpu = gpu_id(encoder),
            .transfer = source == encoder ? caps::TransferKind::same_adapter_copy
                                          : caps::TransferKind::cross_adapter_copy,
            .conversions = unknown<std::vector<caps::Conversion>>(caps::IssueCode::relationship_unprovable),
            .evidence = {caps::Support::supported, inferred()},
        };
    }

    static bool identifier(std::string_view value) {
        return !value.empty() && value.size() <= caps::kMaxIdentifierBytes &&
               std::ranges::all_of(value, [](char c) { return c > ' ' && c <= '~'; });
    }

    std::string_view probe_id_;
    std::vector<caps::ProbeIssue>& issues_;
};

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

} // namespace

caps::GpuId gpu_id(NativeLuid luid) {
    return {"luid:" + hex8(luid.high) + ":" + hex8(luid.low), caps::IdentityScope::os_session};
}

caps::DisplayId display_id(NativeLuid adapter, std::uint32_t target_id) {
    return {"display:" + hex8(adapter.high) + ":" + hex8(adapter.low) + ":" + std::to_string(target_id),
            caps::IdentityScope::os_session};
}

caps::EncoderId encoder_id(const NativeEncoder& encoder) {
    std::string value = "mft:" + std::string(codec_name(encoder.codec)) +
                        (encoder.hardware ? ":hardware:" : ":software:");
    std::ranges::transform(encoder.clsid, std::back_inserter(value),
                           [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; });
    if (encoder.adapter) {
        value += ":" + hex8(encoder.adapter->high) + ":" + hex8(encoder.adapter->low);
    }
    // Hardware transforms are tied to adapters whose LUIDs last one OS session.
    return {std::move(value), encoder.hardware ? caps::IdentityScope::os_session : caps::IdentityScope::persistent};
}

caps::EncoderProbeFacts translate_encoders(const std::vector<NativeEncoder>& encoders,
                                           const std::vector<NativeLuid>& output_adapters, std::string_view probe_id,
                                           std::vector<caps::ProbeIssue>& issues) {
    return Translator(probe_id, issues).encoders(encoders, output_adapters);
}

caps::AudioProbeFacts translate_audio(const std::vector<NativeAudioEndpoint>& endpoints, std::string_view probe_id,
                                      std::vector<caps::ProbeIssue>& issues) {
    return Translator(probe_id, issues).audio(endpoints);
}

caps::GpuDisplayProbeFacts translate_gpu_display(const NativeGpuDisplay& native, std::string_view probe_id,
                                                 std::vector<caps::ProbeIssue>& issues) {
    return Translator(probe_id, issues).run(native);
}

} // namespace catro::platform::windows
