#pragma once

#include "Settings/Appearance.hpp"

#include <algorithm>
#include <cstdint>
#include <string>

namespace catro::shell {

// Voice & video preferences, saved next to the appearance file as key=value lines.
struct VoicePreferences {
    bool push_to_talk = false;
    // Windows virtual-key code, mouse side buttons included; 0 until the user records one.
    std::uint32_t push_to_talk_key = 0;
    bool automatic_sensitivity = true;
    float sensitivity_db = -50.0F;
    bool echo_cancellation = true;
    bool noise_suppression = true;
    bool automatic_gain = true;
};

inline std::filesystem::path voice_preferences_path() {
    const auto appearance = appearance_path();
    return appearance.empty() ? std::filesystem::path{} : appearance.parent_path() / L"voice-settings";
}

inline VoicePreferences& voice_preferences() {
    static VoicePreferences value = [] {
        VoicePreferences loaded;
        std::ifstream input(voice_preferences_path());
        std::string line;
        while (std::getline(input, line)) {
            const auto split = line.find('=');
            if (split == std::string::npos) {
                continue;
            }
            const auto key = line.substr(0, split);
            const auto text = line.substr(split + 1);
            const bool on = text == "on";
            try {
                if (key == "push-to-talk") {
                    loaded.push_to_talk = on;
                } else if (key == "push-to-talk-key") {
                    loaded.push_to_talk_key = static_cast<std::uint32_t>(std::stoul(text)) & 0xFFU;
                } else if (key == "automatic-sensitivity") {
                    loaded.automatic_sensitivity = on;
                } else if (key == "sensitivity-db") {
                    loaded.sensitivity_db = std::clamp(std::stof(text), -100.0F, 0.0F);
                } else if (key == "echo-cancellation") {
                    loaded.echo_cancellation = on;
                } else if (key == "noise-suppression") {
                    loaded.noise_suppression = on;
                } else if (key == "automatic-gain") {
                    loaded.automatic_gain = on;
                }
            } catch (...) {
                // A damaged value keeps its default.
            }
        }
        return loaded;
    }();
    return value;
}

// Bumped by every save so a running call re-applies preferences without reading the file.
inline std::uint32_t& voice_preferences_version() {
    static std::uint32_t version = 0;
    return version;
}

// Applies for this session even when the file cannot be written.
inline bool save_voice_preferences(const VoicePreferences& preferences) {
    voice_preferences() = preferences;
    ++voice_preferences_version();
    const auto path = voice_preferences_path();
    if (path.empty()) {
        return false;
    }
    const auto flag = [](bool value) { return value ? "on" : "off"; };
    std::ofstream output(path, std::ios::trunc);
    output << "push-to-talk=" << flag(preferences.push_to_talk) << '\n'
           << "push-to-talk-key=" << preferences.push_to_talk_key << '\n'
           << "automatic-sensitivity=" << flag(preferences.automatic_sensitivity) << '\n'
           << "sensitivity-db=" << preferences.sensitivity_db << '\n'
           << "echo-cancellation=" << flag(preferences.echo_cancellation) << '\n'
           << "noise-suppression=" << flag(preferences.noise_suppression) << '\n'
           << "automatic-gain=" << flag(preferences.automatic_gain) << '\n';
    output.close();
    return static_cast<bool>(output);
}

} // namespace catro::shell
