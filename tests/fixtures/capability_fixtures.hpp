#pragma once

#include <catro/capabilities/model.hpp>

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

// Stable identifiers used by valid_snapshot().
caps::GpuId desktop_gpu();
caps::DisplayId desktop_display();
caps::EncoderId hardware_h264();
caps::EncoderId software_h264();
caps::CapturePathId display_capture();
caps::AudioEndpointId microphone();
caps::AudioEndpointId speakers();

} // namespace catro::fixtures
