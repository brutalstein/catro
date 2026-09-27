#pragma once

#include <catro/capabilities/probe.hpp>

#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Plain records of what the native GPU and display APIs reported, and their translation into
// domain facts. No Windows headers: tests build synthetic records, the probe fills real ones.
namespace catro::platform::windows {

namespace caps = catro::capabilities;

struct NativeLuid {
    std::uint32_t high = 0;
    std::uint32_t low = 0;

    friend auto operator<=>(const NativeLuid&, const NativeLuid&) = default;
};

struct NativeAdapter {
    NativeLuid luid;
    std::uint32_t vendor_id = 0;
    std::uint32_t device_id = 0;
    // UTF-8 driver description.
    std::string description;
    // DXGI_ADAPTER_FLAG_SOFTWARE: WARP or the basic render driver.
    bool software = false;
    // DXCore IsIntegrated; absent when DXCore cannot answer for this adapter.
    std::optional<bool> integrated;
    // DXCore IsDetachable: an external enclosure; absent when DXCore cannot answer.
    std::optional<bool> detachable;
    std::uint64_t dedicated_memory = 0;
    std::uint64_t shared_memory = 0;
    bool direct3d11 = false;
    bool direct3d12 = false;
    // Whether the OS ranks this adapter first for the preference; absent without IDXGIFactory6.
    std::optional<bool> preferred_for_minimum_power;
    std::optional<bool> preferred_for_high_performance;
};

struct NativeDisplay {
    // Adapter and target of the active display path.
    NativeLuid adapter;
    std::uint32_t target_id = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    // Exact target refresh rate from the display configuration.
    std::uint32_t refresh_numerator = 0;
    std::uint32_t refresh_denominator = 0;
    // Effective DPI of the matching monitor; absent when it cannot be matched.
    std::optional<std::uint32_t> dpi;
    bool primary = false;
    // Advanced-color query results; absent when the query failed.
    std::optional<bool> hdr_supported;
    std::optional<bool> hdr_enabled;
    std::optional<std::uint8_t> bits_per_channel;
};

struct NativeCaptureApis {
    // GraphicsCaptureSession::IsSupported; absent when the API cannot be activated.
    std::optional<bool> graphics_capture;
    // IDXGIOutput1 on an attached output; absent when no output is attached.
    std::optional<bool> desktop_duplication;
    // IDXGIOutput5 (DuplicateOutput1 with FP16 formats) on an attached output.
    bool desktop_duplication_hdr = false;
};

struct NativeGpuDisplay {
    // Absent when the enumeration API itself failed.
    std::optional<std::vector<NativeAdapter>> adapters;
    std::optional<std::vector<NativeDisplay>> displays;
    NativeCaptureApis capture;
};

struct NativeEncoder {
    caps::Codec codec = caps::Codec::h264;
    // Registered transform CLSID in registry form, e.g. "{6ca50344-051a-4ded-9779-a43305165e35}".
    std::string clsid;
    bool hardware = false;
    // Adapter the transform was enumerated for; absent when enumeration could not filter by adapter.
    std::optional<NativeLuid> adapter;
    // UTF-8 friendly name.
    std::string name;
    // Registered input subtypes Catro can describe, in registration order.
    std::vector<caps::PixelFormat> inputs;
};

enum class NativeEndpointState {
    active,
    disabled,
    unplugged,
};

struct NativeAudioEndpoint {
    // MMDevice endpoint identifier, printable ASCII.
    std::string id;
    caps::AudioDirection direction = caps::AudioDirection::output;
    std::string name;
    NativeEndpointState state = NativeEndpointState::active;
    // Engine device format from the property store; absent when not reported.
    std::optional<std::uint32_t> channels;
    std::optional<std::uint32_t> sample_rate_hz;
    std::optional<caps::SampleFormat> sample_format;
    // Roles for which this endpoint is the current default; absent when a default query failed.
    std::optional<std::vector<caps::AudioRole>> default_roles;
};

// Capture paths every gpu_display fragment publishes; encoder transfers start from these.
inline constexpr std::string_view kGraphicsCaptureDisplay = "wgc:display";
inline constexpr std::string_view kGraphicsCaptureWindow = "wgc:window";
inline constexpr std::string_view kDesktopDuplicationDisplay = "dxgi-duplication:display";

// Stable identifiers: adapter LUIDs are valid for the OS session; capture paths are constant.
[[nodiscard]] caps::GpuId gpu_id(NativeLuid luid);
[[nodiscard]] caps::DisplayId display_id(NativeLuid adapter, std::uint32_t target_id);

// Relationships are claimed only when native data proves them: a display's adapter is known
// only when its path LUID matches an enumerated adapter, and capture paths never claim one.
// Every reason for absent or degraded evidence is also appended to `issues` once.
[[nodiscard]] caps::GpuDisplayProbeFacts translate_gpu_display(const NativeGpuDisplay& native,
                                                               std::string_view probe_id,
                                                               std::vector<caps::ProbeIssue>& issues);

[[nodiscard]] caps::EncoderId encoder_id(const NativeEncoder& encoder);

// Encoders are advertised, never runtime-validated. Transfers claim only what is certain: a
// software encoder always takes CPU-staged frames. Desktop Duplication produces frames only on
// the adapter an output is attached to, so each such adapter gets its own duplication transfer
// to a hardware encoder: a same-adapter copy or a cross-adapter copy. Graphics Capture delivers
// to whichever device the caller supplies, so its cost to a hardware encoder stays unknown.
[[nodiscard]] caps::EncoderProbeFacts translate_encoders(const std::vector<NativeEncoder>& encoders,
                                                         const std::vector<NativeLuid>& output_adapters,
                                                         std::string_view probe_id,
                                                         std::vector<caps::ProbeIssue>& issues);

[[nodiscard]] caps::AudioProbeFacts translate_audio(const std::vector<NativeAudioEndpoint>& endpoints,
                                                    std::string_view probe_id,
                                                    std::vector<caps::ProbeIssue>& issues);

} // namespace catro::platform::windows
