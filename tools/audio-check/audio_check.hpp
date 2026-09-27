#pragma once

#include <catro/audio/engine.hpp>

#include <chrono>
#include <functional>
#include <optional>
#include <ostream>
#include <span>
#include <string_view>

// The platform-independent half of catro-audio-check: arguments, the timed session, and the
// summary. The platform entry point supplies only its AudioPlatform.
namespace catro::tools {

struct AudioCheckOptions {
    audio::SessionConfig session;
    std::chrono::seconds duration{5};
};

enum AudioCheckExit : int {
    audio_check_ok = 0,
    audio_check_invalid_arguments = 2,
    audio_check_audio_failed = 5,
};

inline constexpr std::string_view kAudioCheckUsage =
    "usage: catro-audio-check [--mode meter|tone|monitor] [--seconds 0-60] [--input <id>] [--output <id>]\n"
    "  meter    capture only and report input levels (default)\n"
    "  tone     play a 440 Hz test tone\n"
    "  monitor  play the microphone back live; use headphones\n"
    "  ids are capability endpoint ids, e.g. mmdevice:{...}; the default is the communications device\n";

// UTF-8 arguments without the program name. Absent on unknown, repeated, or invalid options.
[[nodiscard]] std::optional<AudioCheckOptions> parse_audio_check_arguments(std::span<const std::string_view> arguments);

// Waits between progress lines; injected so tests do not sleep.
using AudioCheckWait = std::function<void(std::chrono::milliseconds)>;

[[nodiscard]] int run_audio_check(std::span<const std::string_view> arguments, audio::AudioPlatform& platform,
                                  const AudioCheckWait& wait, std::ostream& out, std::ostream& error);

} // namespace catro::tools
