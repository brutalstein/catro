#include "capture_check.hpp"

#include <charconv>

namespace catro::tools {

std::optional<CaptureCheckOptions> parse_capture_check_arguments(
    std::span<const std::string_view> arguments) {
    CaptureCheckOptions options;
    bool seconds_seen = false;

    for (std::size_t index = 0; index < arguments.size(); ++index) {
        if (index + 1 >= arguments.size()) {
            return std::nullopt;
        }

        const auto option = arguments[index];
        const auto value = arguments[++index];
        if (option != "--seconds" || seconds_seen) {
            return std::nullopt;
        }

        seconds_seen = true;
        int seconds = 0;
        const auto [end, error] =
            std::from_chars(value.data(), value.data() + value.size(), seconds);
        if (error != std::errc{} || end != value.data() + value.size() ||
            seconds < 1 || seconds > 60) {
            return std::nullopt;
        }
        options.duration = std::chrono::seconds(seconds);
    }

    return options;
}

} // namespace catro::tools
