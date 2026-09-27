#pragma once

#include "DiagnosticsViewModel.hpp"

#include <catro/audio/engine.hpp>
#include <catro/capabilities/model.hpp>

#include <optional>
#include <string>
#include <vector>

// Plain C++ presentation of the audio test page, shared by the native shells.
namespace catro::app {

struct AudioDeviceChoice {
    // Empty selects the system default communications endpoint.
    std::optional<capabilities::AudioEndpointId> id;
    std::string label;

    friend bool operator==(const AudioDeviceChoice&, const AudioDeviceChoice&) = default;
};

// The default first, then every active endpoint of the direction in snapshot order. Names are shown
// unredacted: the user picks by name, and the picker is never exported.
[[nodiscard]] std::vector<AudioDeviceChoice> audio_choices(const capabilities::CapabilitySnapshot& snapshot,
                                                           capabilities::AudioDirection direction);

struct AudioLevel {
    // 0 at -60 dBFS and below, 1 at full scale.
    double fraction = 0;
    std::string text;

    friend bool operator==(const AudioLevel&, const AudioLevel&) = default;
};

struct AudioSessionView {
    std::string status;
    Tone tone = Tone::neutral;
    bool running = false;
    std::optional<AudioLevel> input;
    std::optional<AudioLevel> output;
    std::vector<DiagnosticsRow> rows;

    friend bool operator==(const AudioSessionView&, const AudioSessionView&) = default;
};

[[nodiscard]] AudioLevel audio_level(float linear);

// Latency is labelled as an estimate: it is computed from buffer sizes, never measured.
[[nodiscard]] AudioSessionView describe_audio(const audio::AudioStatistics& statistics);

} // namespace catro::app
