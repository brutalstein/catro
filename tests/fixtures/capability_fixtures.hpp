#pragma once

#include <catro/capabilities/model.hpp>
#include <catro/capabilities/policy.hpp>

#include <cstdint>
#include <string_view>
#include <utility>

// Deterministic synthetic machines for core, policy, and reporting tests. Every fact carries
// explicit evidence and every identifier is stable, so fixtures never depend on real hardware.
namespace catro::fixtures {

namespace caps = catro::capabilities;

inline constexpr std::string_view kSystemProbe = "windows.system.v1";
inline constexpr std::string_view kRuntimeProbe = "windows.runtime.v1";
inline constexpr std::string_view kGpuDisplayProbe = "windows.gpu_display.v1";
inline constexpr std::string_view kEncoderProbe = "windows.encoders.v1";
inline constexpr std::string_view kAudioProbe = "windows.audio.v1";
inline constexpr std::string_view kMacSystemProbe = "macos.system.v1";
inline constexpr std::string_view kMacRuntimeProbe = "macos.runtime.v1";
inline constexpr std::string_view kMacGpuDisplayProbe = "macos.gpu_display.v1";
inline constexpr std::string_view kMacEncoderProbe = "macos.encoders.v1";
inline constexpr std::string_view kMacAudioProbe = "macos.audio.v1";

caps::Provenance measured(std::string_view probe);
caps::Provenance advertised(std::string_view probe);
caps::Provenance absent(std::string_view probe, caps::IssueCode code);

template <class T>
caps::Observed<T> known(T value, caps::Provenance provenance) {
    return caps::Observed<T>::known(std::move(value), std::move(provenance));
}

template <class T>
caps::Observed<T> unknown(std::string_view probe, caps::IssueCode code) {
    return caps::Observed<T>::unknown(absent(probe, code));
}

template <class T>
caps::Observed<T> unavailable(std::string_view probe, caps::IssueCode code) {
    return caps::Observed<T>::unavailable(absent(probe, code));
}

caps::SupportFact supported(caps::Provenance provenance);
caps::SupportFact unsupported(caps::Provenance provenance);
caps::SupportFact support_unknown(std::string_view probe, caps::IssueCode code);

caps::ProbeRecord probe_record(std::string_view probe, caps::ProbeFamily family, std::uint64_t generation = 1);

// A coherent Windows desktop: one discrete GPU driving one 144 Hz display, a hardware and a
// software H.264 encoder, source-specific display capture, one microphone, and one output.
caps::CapabilitySnapshot valid_snapshot();

// valid_snapshot() plus HDR-capable 10-bit HEVC and AV1 hardware encoders on the same GPU.
caps::CapabilitySnapshot high_end_desktop();
// One integrated GPU, 1920x1200 at 60 Hz, on battery with low-power mode enabled.
caps::CapabilitySnapshot intel_laptop_on_battery();
// The integrated GPU drives a 165 Hz panel; the discrete GPU carries more encoders but is only
// reachable from display capture through a cross-adapter copy.
caps::CapabilitySnapshot hybrid_laptop();
// Unified-memory Apple Silicon laptop with a 120 Hz Retina panel at scale 2.
caps::CapabilitySnapshot apple_silicon_macbook();
// apple_silicon_macbook() under serious thermal pressure.
caps::CapabilitySnapshot hot_apple_silicon();
// Integrated plus discrete GPU Intel Mac where display, capture, and encoder affinity is unprovable.
caps::CapabilitySnapshot older_intel_mac();
// valid_snapshot() in a headless session with no attached display.
caps::CapabilitySnapshot headless_session();
// valid_snapshot() whose encoder family timed out: encoders and transfers are absent.
caps::CapabilitySnapshot partial_probe_failure();
// valid_snapshot() without the hardware encoder: software H.264 is the only encoder.
caps::CapabilitySnapshot software_only();
// valid_snapshot() on the basic display adapter: the hardware encoder is still enumerated but
// its activation probe fails.
caps::CapabilitySnapshot missing_gpu_driver();
// valid_snapshot() whose hardware encoder reports no size or frame-rate limits.
caps::CapabilitySnapshot unknown_codec_limits();
// valid_snapshot() without an audio input endpoint.
caps::CapabilitySnapshot no_microphone();
// valid_snapshot() inside a remote desktop session.
caps::CapabilitySnapshot remote_session();
// valid_snapshot() plus secondary 59.94 Hz, 60 Hz, and 120 Hz displays (extra_display 2, 3, 4)
// on the same GPU; the 144 Hz display stays primary.
caps::CapabilitySnapshot mixed_refresh_desktop();
// high_end_desktop() with HDR enabled on its display.
caps::CapabilitySnapshot hdr_desktop();
// valid_snapshot() plus `extra` identical hardware H.264 encoders on the same GPU, with
// identifiers that sort after hardware_h264().
caps::CapabilitySnapshot crowded_desktop(std::uint32_t extra);

// Stable identifiers used by the fixtures.
caps::GpuId desktop_gpu();
caps::DisplayId desktop_display();
caps::DisplayId extra_display(std::uint32_t index);
caps::EncoderId hardware_h264();
caps::EncoderId software_h264();
caps::EncoderId hardware_hevc();
caps::EncoderId hardware_av1();
caps::CapturePathId display_capture();
caps::AudioEndpointId microphone();
caps::AudioEndpointId speakers();

caps::GpuId hybrid_integrated_gpu();
caps::GpuId hybrid_discrete_gpu();
caps::EncoderId hybrid_integrated_h264();
caps::EncoderId hybrid_discrete_h264();
caps::EncoderId hybrid_discrete_av1();
caps::DisplayId laptop_display();

caps::DisplayId mac_display();
caps::CapturePathId mac_display_capture();
caps::EncoderId mac_h264();
caps::EncoderId mac_hevc();

// A display-capture request for the fixture's primary display.
caps::MediaDecisionRequest display_request();

// The same snapshot with every collection whose order carries no meaning reordered.
caps::CapabilitySnapshot shuffled(caps::CapabilitySnapshot snapshot, std::uint32_t seed);

} // namespace catro::fixtures
