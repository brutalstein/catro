#include "ShellModel.hpp"

#include <algorithm>

namespace catro::app {

const ShellSectionSpec& shell_section_spec(ShellSection section) noexcept {
    const auto found = std::ranges::find(kShellSections, section, &ShellSectionSpec::section);
    return found != kShellSections.end() ? *found : kShellSections.front();
}

std::optional<ShellSection> shell_section_from_id(std::string_view id) noexcept {
    const auto found = std::ranges::find(kShellSections, id, &ShellSectionSpec::id);
    return found != kShellSections.end() ? std::optional{found->section} : std::nullopt;
}

bool ShellState::activate(ShellSection section) noexcept {
    if (section == active_) {
        return false;
    }
    active_ = shell_section_spec(section).section;
    return true;
}

bool ShellState::activate(std::string_view id) noexcept {
    const auto section = shell_section_from_id(id);
    return section ? activate(*section) : false;
}

} // namespace catro::app
