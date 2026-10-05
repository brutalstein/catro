#pragma once

#include <algorithm>
#include <cmath>
#include <map>
#include <ostream>
#include <string>
#include <string_view>

namespace catro::shell {

inline float user_volume(const std::map<std::string, float>& volumes, const std::string& id) {
    const auto found = volumes.find(id);
    return found == volumes.end() ? 1.0F : found->second;
}

// Pushes only changed gains to the voice runtime; anyone who left (or whose setting was removed)
// goes back to 100%.
template <typename SetVolume>
void sync_user_volumes(std::map<std::string, float>& applied,
                       const std::map<std::string, float>& wanted, SetVolume set_volume) {
    for (const auto& [id, gain] : applied) {
        if (!wanted.contains(id)) {
            set_volume(id, 1.0F);
        }
    }
    for (const auto& [id, gain] : wanted) {
        const auto found = applied.find(id);
        if (found == applied.end() || found->second != gain) {
            set_volume(id, gain);
        }
    }
    applied = wanted;
}

inline void read_user_volume(std::map<std::string, float>& volumes, std::string_view key,
                             const std::string& text) {
    constexpr std::string_view prefix = "user-volume.";
    if (!key.starts_with(prefix)) {
        return;
    }
    const auto id = key.substr(prefix.size());
    if (id.size() != 64 || id.find_first_not_of("0123456789abcdef") != std::string_view::npos) {
        return;
    }
    try {
        std::size_t consumed = 0;
        const auto value = std::stof(text, &consumed);
        if (consumed == text.size() && std::isfinite(value)) {
            volumes[std::string{id}] = std::clamp(value, 0.0F, 2.0F);
        }
    } catch (...) {
        // A damaged entry leaves this user's default or previous value intact.
    }
}

inline void write_user_volumes(std::ostream& output, const std::map<std::string, float>& volumes) {
    for (const auto& [id, volume] : volumes) {
        output << "user-volume." << id << '=' << volume << '\n';
    }
}

} // namespace catro::shell
