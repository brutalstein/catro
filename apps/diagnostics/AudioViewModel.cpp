#include "AudioViewModel.hpp"

#include <catro/audio/realtime.hpp>

#include <algorithm>
#include <cstdio>

namespace catro::app {
namespace {

using capabilities::AudioRole;

std::string fixed(double value) {
    char text[32]{};
    std::snprintf(text, sizeof(text), "%.1f", value);
    return text;
}

std::string milliseconds(std::uint32_t frames) {
    return fixed(static_cast<double>(frames) * 1000.0 / audio::kSampleRate) + " ms";
}

void add(std::vector<DiagnosticsRow>& rows, std::string label, std::string value) {
    rows.push_back({.depth = 0, .label = std::move(label), .value = std::move(value)});
}

void add_stream(std::vector<DiagnosticsRow>& rows, const std::string& prefix, const audio::StreamInfo& info) {
    add(rows, prefix + " device format",
        std::to_string(info.device_sample_rate) + " Hz, " + std::to_string(info.device_channels) + " ch");
    add(rows, prefix + " period", milliseconds(info.period_frames));
    add(rows, prefix + " device latency", milliseconds(info.device_latency_frames));
}

} // namespace

std::vector<AudioDeviceChoice> audio_choices(const capabilities::CapabilitySnapshot& snapshot,
                                             capabilities::AudioDirection direction) {
    std::vector<AudioDeviceChoice> choices{{std::nullopt, "System default (communications)"}};
    for (const auto& endpoint : snapshot.devices.audio_endpoints) {
        if (endpoint.direction != direction) {
            continue;
        }
        const auto state = std::ranges::find(snapshot.runtime.audio_endpoints, endpoint.id,
                                             &capabilities::AudioEndpointState::endpoint);
        const bool found = state != snapshot.runtime.audio_endpoints.end();
        // Only an endpoint known to be inactive is left out; unknown activity stays selectable.
        if (found && state->active.value() == false) {
            continue;
        }
        auto label = endpoint.name.value().value_or(endpoint.id.value);
        if (endpoint.sample_rate_hz.value()) {
            label += " — " + std::to_string(*endpoint.sample_rate_hz.value() / 1000) + " kHz";
            if (direction == capabilities::AudioDirection::input && *endpoint.sample_rate_hz.value() < 32000) {
                label += " (narrowband)";
            }
        }
        if (found && state->default_roles.value() &&
            std::ranges::find(*state->default_roles.value(), AudioRole::communications) !=
                state->default_roles.value()->end()) {
            label += " (default communications)";
        }
        choices.push_back({endpoint.id, std::move(label)});
    }
    return choices;
}

AudioLevel audio_level(float linear) {
    const auto dbfs = audio::LevelMeter::to_dbfs(linear);
    return {std::clamp((static_cast<double>(dbfs) + 60.0) / 60.0, 0.0, 1.0), fixed(dbfs) + " dBFS"};
}

AudioSessionView describe_audio(const audio::AudioStatistics& statistics) {
    AudioSessionView view;
    switch (statistics.state) {
    case audio::EngineState::idle:
        view.status = "Stopped";
        break;
    case audio::EngineState::running:
        view.status = "Running " + std::string(name(statistics.mode.value_or(audio::SessionMode::meter)));
        view.tone = Tone::positive;
        view.running = true;
        break;
    case audio::EngineState::failed:
        view.status = "Stopped: " + std::string(statistics.error ? name(statistics.error->code) : "failure");
        view.tone = Tone::critical;
        break;
    }
    if (statistics.input) {
        view.input = audio_level(statistics.input_peak);
        add_stream(view.rows, "Input", *statistics.input);
    }
    if (statistics.output) {
        view.output = audio_level(statistics.output_peak);
        add_stream(view.rows, "Output", *statistics.output);
    }
    if (statistics.estimated_latency) {
        add(view.rows, "Estimated latency",
            fixed(static_cast<double>(statistics.estimated_latency->count()) / 1000.0) +
                " ms (from device and buffer sizes, not measured)");
    }
    if (statistics.input || statistics.output) {
        add(view.rows, "Underruns", std::to_string(statistics.underruns));
        add(view.rows, "Overruns", std::to_string(statistics.overruns));
        add(view.rows, "Drift corrections", std::to_string(statistics.drift_corrections));
        add(view.rows, "Glitches", std::to_string(statistics.glitches));
    }
    // One initial discontinuity is common on some shared-mode endpoints. Repeated discontinuities
    // are not healthy for voice and must be visible instead of looking like a successful session.
    if (view.running && (statistics.underruns + statistics.overruns > 0 || statistics.glitches > 3)) {
        view.tone = Tone::caution;
    }
    if (statistics.glitches > 3) {
        add(view.rows, "Audio health", "Repeated device discontinuities; try another active endpoint");
    }
    return view;
}

} // namespace catro::app
