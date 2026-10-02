#pragma once

#include "Settings/Appearance.hpp"

namespace catro::shell {

// Read once, not in the presentation timer. New installations skip the redundant self-preview.
inline bool& local_preview_preference() {
    static bool enabled = [] {
        const auto appearance = appearance_path();
        std::ifstream input(appearance.empty() ? std::filesystem::path{}
                                             : appearance.parent_path() / L"local-preview");
        std::string value;
        input >> value;
        return value == "on";
    }();
    return enabled;
}

inline bool save_local_preview(bool enabled) {
    const auto appearance = appearance_path();
    if (appearance.empty()) {
        return false;
    }
    std::ofstream output(appearance.parent_path() / L"local-preview", std::ios::trunc);
    output << (enabled ? "on" : "off");
    output.close();
    if (!output) {
        return false;
    }
    local_preview_preference() = enabled;
    return true;
}

} // namespace catro::shell
