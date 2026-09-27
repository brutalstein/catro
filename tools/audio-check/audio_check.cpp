#include "audio_check.hpp"

#include <catro/audio/realtime.hpp>

#include <charconv>
#include <cstdio>
#include <string>

namespace catro::tools {
namespace {

using audio::LevelMeter;

std::string fixed(double value) {
    char text[32]{};
    std::snprintf(text, sizeof(text), "%.1f", value);
    return text;
}

std::string dbfs(float linear) {
    return fixed(LevelMeter::to_dbfs(linear)) + " dBFS";
}

std::string describe(const audio::StreamInfo& info) {
    return info.device.value + " (" + std::to_string(info.device_sample_rate) + " Hz, " +
           std::to_string(info.device_channels) + " ch, period " + std::to_string(info.period_frames) +
           " frames, device latency " + std::to_string(info.device_latency_frames) + " frames)";
}

void report_error(std::ostream& error, const audio::AudioError& failure) {
    error << "catro-audio-check: " << name(failure.code);
    if (failure.native_code) {
        char code[16]{};
        std::snprintf(code, sizeof(code), "0x%08x", static_cast<unsigned>(*failure.native_code));
        error << " (" << code << ")";
    }
    error << '\n';
}

} // namespace

std::optional<AudioCheckOptions> parse_audio_check_arguments(std::span<const std::string_view> arguments) {
    AudioCheckOptions options;
    bool mode_seen = false;
    bool seconds_seen = false;
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const auto option = arguments[index];
        if (index + 1 >= arguments.size()) {
            return std::nullopt;
        }
        const auto value = arguments[++index];
        if (option == "--mode" && !mode_seen) {
            mode_seen = true;
            if (value == "meter") {
                options.session.mode = audio::SessionMode::meter;
            } else if (value == "tone") {
                options.session.mode = audio::SessionMode::tone;
            } else if (value == "monitor") {
                options.session.mode = audio::SessionMode::monitor;
            } else {
                return std::nullopt;
            }
        } else if (option == "--seconds" && !seconds_seen) {
            seconds_seen = true;
            int seconds = -1;
            const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), seconds);
            if (error != std::errc{} || end != value.data() + value.size() || seconds < 0 || seconds > 60) {
                return std::nullopt;
            }
            options.duration = std::chrono::seconds(seconds);
        } else if (option == "--input" && !options.session.input && !value.empty()) {
            options.session.input = capabilities::AudioEndpointId{std::string(value), capabilities::IdentityScope::persistent};
        } else if (option == "--output" && !options.session.output && !value.empty()) {
            options.session.output = capabilities::AudioEndpointId{std::string(value), capabilities::IdentityScope::persistent};
        } else {
            return std::nullopt;
        }
    }
    return options;
}

int run_audio_check(std::span<const std::string_view> arguments, audio::AudioPlatform& platform,
                    const AudioCheckWait& wait, std::ostream& out, std::ostream& error) {
    const auto options = parse_audio_check_arguments(arguments);
    if (!options) {
        error << kAudioCheckUsage;
        return audio_check_invalid_arguments;
    }
    const auto mode = options->session.mode;
    if (mode == audio::SessionMode::monitor) {
        error << "catro-audio-check: monitor plays the microphone back; use headphones to avoid feedback\n";
    }

    audio::AudioEngine engine(platform);
    if (const auto failure = engine.start(options->session)) {
        report_error(error, *failure);
        return audio_check_audio_failed;
    }
    auto statistics = engine.statistics();
    out << "mode: " << name(mode) << '\n';
    if (statistics.input) {
        out << "input: " << describe(*statistics.input) << '\n';
    }
    if (statistics.output) {
        out << "output: " << describe(*statistics.output) << '\n';
    }

    for (std::int64_t second = 1; second <= options->duration.count(); ++second) {
        wait(std::chrono::seconds(1));
        statistics = engine.statistics();
        if (statistics.state == audio::EngineState::failed) {
            break;
        }
        out << second << "s:";
        if (statistics.input) {
            out << " input " << dbfs(statistics.input_peak);
        }
        if (statistics.output) {
            out << " output " << dbfs(statistics.output_peak);
        }
        out << " | glitches " << statistics.glitches;
        if (mode == audio::SessionMode::monitor) {
            out << ", underruns " << statistics.underruns << ", overruns " << statistics.overruns
                << ", drift " << statistics.drift_corrections;
        }
        out << '\n';
    }
    statistics = engine.statistics();
    engine.stop();

    if (statistics.input) {
        out << "input peak: " << dbfs(statistics.input_peak) << ", rms: " << dbfs(statistics.input_rms) << '\n';
    }
    if (statistics.output) {
        out << "output peak: " << dbfs(statistics.output_peak) << '\n';
    }
    if (statistics.estimated_latency) {
        out << "estimated latency: " << fixed(static_cast<double>(statistics.estimated_latency->count()) / 1000.0)
            << " ms (estimated from device and buffer sizes, not measured)\n";
    }
    out << "underruns: " << statistics.underruns << ", overruns: " << statistics.overruns
        << ", drift corrections: " << statistics.drift_corrections << ", glitches: " << statistics.glitches << '\n';
    if (statistics.state == audio::EngineState::failed && statistics.error) {
        report_error(error, *statistics.error);
        return audio_check_audio_failed;
    }
    return audio_check_ok;
}

} // namespace catro::tools
