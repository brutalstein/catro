#pragma once

#include <catro/capabilities/media_plan.hpp>
#include <catro/platform/macos/screen_capture.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace catro::product {

// Product-facing quality derived from the shared capability policy. The UI chooses a ceiling;
// the running session may narrow it later as power/thermal state changes.
struct AdaptiveShareQuality {
    std::string label;
    std::string detail;
    std::uint32_t max_width = 1280;
    std::uint32_t max_height = 720;
    std::uint32_t fps = 30;
    std::uint32_t bitrate = 3'000'000;
    bool recommended = false;
    capabilities::OperatingProfile profile = capabilities::OperatingProfile::safe_local_envelope;

    friend bool operator==(const AdaptiveShareQuality&, const AdaptiveShareQuality&) = default;
};

// Generates unique quality choices from the current CapabilitySnapshot. A null snapshot yields a
// conservative 720p30-safe choice while the passive capability scan is still starting.
[[nodiscard]] std::vector<AdaptiveShareQuality> adaptive_share_qualities(
    const capabilities::CapabilitySnapshot* snapshot,
    const platform::macos::CaptureSource& source);

// Re-evaluates one user ceiling against the latest runtime state. Used for active-share adaptation
// after battery, Low Power Mode, thermal, memory/display, or encoder-capability changes.
[[nodiscard]] AdaptiveShareQuality adaptive_share_quality(
    const capabilities::CapabilitySnapshot* snapshot,
    const platform::macos::CaptureSource& source,
    std::uint32_t max_width,
    std::uint32_t max_height,
    std::uint32_t fps);

} // namespace catro::product
