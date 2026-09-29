#pragma once

#include <string_view>

namespace catro::rtc {

[[nodiscard]] constexpr bool screen_media_allowed(
    std::string_view owner,
    std::string_view sender) noexcept {
    return !owner.empty() && owner == sender;
}

} // namespace catro::rtc
