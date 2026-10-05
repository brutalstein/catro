#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace catro::app {

// Where the app checks for updates. GitHub redirects this page to /releases/tag/<tag> of the newest
// published release, so no API call (or API rate limit) is involved.
inline constexpr std::string_view kLatestReleaseUrl =
    "https://github.com/brutalstein/catro/releases/latest";

// The x.y.z version this build was made from (CMake's project version).
[[nodiscard]] std::string_view app_version() noexcept;

// The release tag from the URL the latest-release page redirected to, or nothing.
[[nodiscard]] std::optional<std::string> release_tag(std::string_view final_url);

// True only when the tag ("v0.3.8" or "0.3.8") is a newer plain x.y.z than the current version.
[[nodiscard]] bool is_newer_release(std::string_view tag, std::string_view current) noexcept;

} // namespace catro::app
