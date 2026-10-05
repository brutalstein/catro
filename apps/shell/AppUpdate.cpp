#include "AppUpdate.hpp"

#include <array>
#include <charconv>

namespace catro::app {

namespace {

using Version = std::array<unsigned, 3>;

std::optional<Version> parse_version(std::string_view text) noexcept {
    if (text.starts_with('v')) {
        text.remove_prefix(1);
    }
    Version version{};
    for (std::size_t part = 0; part < version.size(); ++part) {
        const auto* const end = text.data() + text.size();
        const auto [next, error] = std::from_chars(text.data(), end, version[part]);
        if (error != std::errc{} || next == text.data()) {
            return std::nullopt;
        }
        text.remove_prefix(static_cast<std::size_t>(next - text.data()));
        if (part + 1 < version.size()) {
            if (!text.starts_with('.')) {
                return std::nullopt;
            }
            text.remove_prefix(1);
        }
    }
    if (!text.empty()) {
        return std::nullopt;
    }
    return version;
}

} // namespace

std::string_view app_version() noexcept {
    return CATRO_APP_VERSION;
}

std::optional<std::string> release_tag(std::string_view final_url) {
    constexpr std::string_view prefix = "https://github.com/brutalstein/catro/releases/tag/";
    if (!final_url.starts_with(prefix)) {
        return std::nullopt;
    }
    auto tag = final_url.substr(prefix.size());
    if (tag.ends_with('/')) {
        tag.remove_suffix(1);
    }
    if (tag.empty() ||
        tag.find_first_not_of("0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ.-_") !=
            std::string_view::npos) {
        return std::nullopt;
    }
    return std::string{tag};
}

bool is_newer_release(std::string_view tag, std::string_view current) noexcept {
    const auto offered = parse_version(tag);
    const auto running = parse_version(current);
    return offered && running && *offered > *running;
}

} // namespace catro::app
