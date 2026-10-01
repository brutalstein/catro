#pragma once

#include <catro/platform/windows/local_state.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <variant>

// The user's appearance choice, stored as one word next to the local profile. Ivory is the
// default; "system" follows the Windows light/dark setting.
namespace catro::shell {

enum class Appearance { ivory, dark, system };

inline std::filesystem::path appearance_path() {
    const auto state = catro::platform::windows::default_local_state_path();
    const auto* path = std::get_if<std::filesystem::path>(&state);
    return path != nullptr ? path->parent_path() / L"appearance" : std::filesystem::path{};
}

inline Appearance load_appearance() {
    std::ifstream input(appearance_path());
    std::string value;
    input >> value;
    if (value == "dark") {
        return Appearance::dark;
    }
    return value == "system" ? Appearance::system : Appearance::ivory;
}

inline void save_appearance(Appearance appearance) {
    const auto path = appearance_path();
    if (path.empty()) {
        return;
    }
    std::error_code ignored;
    std::filesystem::create_directories(path.parent_path(), ignored);
    std::ofstream output(path, std::ios::trunc);
    output << (appearance == Appearance::dark     ? "dark"
               : appearance == Appearance::system ? "system"
                                                  : "ivory");
}

inline winrt::Microsoft::UI::Xaml::ElementTheme element_theme(Appearance appearance) {
    using winrt::Microsoft::UI::Xaml::ElementTheme;
    switch (appearance) {
    case Appearance::dark:
        return ElementTheme::Dark;
    case Appearance::system:
        return ElementTheme::Default;
    case Appearance::ivory:
        break;
    }
    return ElementTheme::Light;
}

} // namespace catro::shell
