#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace catro::app {

enum class ShellSection : std::uint8_t {
    home,
    voice,
    share,
    diagnostics,
    settings,
};

struct ShellSectionSpec {
    ShellSection section = ShellSection::home;
    std::string_view id;
    std::string_view title;
    std::string_view eyebrow;
    std::string_view summary;
    std::string_view empty_title;
    std::string_view empty_detail;

    friend constexpr bool operator==(const ShellSectionSpec&, const ShellSectionSpec&) = default;
};

inline constexpr std::array<ShellSectionSpec, 5> kShellSections{{
    {ShellSection::home, "home", "Home", "LOCAL WORKSPACE",
     "A quiet launch surface for rooms, voice, and sharing.",
     "Nothing pinned yet", "Recent rooms and people will appear here without changing the shell layout."},
    {ShellSection::voice, "voice", "Voice", "VOICE",
     "Low-latency rooms backed by the native media core.",
     "No voice rooms yet", "Room discovery and membership will plug into this pane; media stays in the native core."},
    {ShellSection::share, "share", "Share", "SCREEN SHARE",
     "Native capture and hardware encode will live behind this surface.",
     "No share session", "Window, display, quality, and system-audio sources will connect here later."},
    {ShellSection::diagnostics, "diagnostics", "System", "DIAGNOSTICS",
     "Inspect capabilities, devices, and native media health.",
     "Diagnostics are local", "Capability and audio evidence stays available without crowding the primary workspace."},
    {ShellSection::settings, "settings", "Settings", "PREFERENCES",
     "User-facing defaults without exposing implementation detail.",
     "Defaults first", "Only settings that materially change voice, sharing, or resource use belong here."},
}};

[[nodiscard]] const ShellSectionSpec& shell_section_spec(ShellSection section) noexcept;
[[nodiscard]] std::optional<ShellSection> shell_section_from_id(std::string_view id) noexcept;

class ShellState {
public:
    [[nodiscard]] ShellSection active() const noexcept { return active_; }
    [[nodiscard]] const ShellSectionSpec& active_spec() const noexcept {
        return shell_section_spec(active_);
    }

    bool activate(ShellSection section) noexcept;
    bool activate(std::string_view id) noexcept;

private:
    ShellSection active_ = ShellSection::home;
};

} // namespace catro::app
