#pragma once

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>

namespace catro::shell {

// The directory accepts at most 64 UTF-8 bytes. Windows text input supplies valid UTF-8.
inline std::optional<std::string> profile_name(std::string_view input) {
    const auto first = input.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return std::nullopt;
    }
    const auto name = input.substr(first, input.find_last_not_of(" \t\r\n") - first + 1);
    if (name.size() > 64 || std::ranges::any_of(name, [](unsigned char ch) {
            return ch < 0x20 || ch == 0x7f;
        })) {
        return std::nullopt;
    }
    return std::string{name};
}

} // namespace catro::shell
