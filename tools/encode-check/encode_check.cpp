#include "encode_check.hpp"

#include <charconv>
#include <cstdint>
#include <string_view>

namespace catro::tools {
namespace {

template <class Integer>
bool parse_integer(std::string_view text, Integer& output) {
    const auto [end, error] =
        std::from_chars(text.data(), text.data() + text.size(), output);
    return error == std::errc{} && end == text.data() + text.size();
}

} // namespace

std::optional<EncodeCheckOptions> parse_encode_check_arguments(
    std::span<const std::string_view> arguments) {
    EncodeCheckOptions options;
    bool seconds_seen = false;
    bool width_seen = false;
    bool height_seen = false;
    bool fps_seen = false;
    bool bitrate_seen = false;

    for (std::size_t index = 0; index < arguments.size(); ++index) {
        if (index + 1 >= arguments.size()) {
            return std::nullopt;
        }

        const auto option = arguments[index];
        const auto value = arguments[++index];

        if (option == "--seconds" && !seconds_seen) {
            seconds_seen = true;
            int seconds = 0;
            if (!parse_integer(value, seconds) || seconds < 1 || seconds > 60) {
                return std::nullopt;
            }
            options.duration = std::chrono::seconds(seconds);
        } else if (option == "--width" && !width_seen) {
            width_seen = true;
            std::uint32_t width = 0;
            if (!parse_integer(value, width) || width < 320 || width > 7680 || (width & 1U) != 0) {
                return std::nullopt;
            }
            options.encoder.width = width;
        } else if (option == "--height" && !height_seen) {
            height_seen = true;
            std::uint32_t height = 0;
            if (!parse_integer(value, height) || height < 180 || height > 4320 || (height & 1U) != 0) {
                return std::nullopt;
            }
            options.encoder.height = height;
        } else if (option == "--fps" && !fps_seen) {
            fps_seen = true;
            std::uint32_t fps = 0;
            if (!parse_integer(value, fps) || fps < 1 || fps > 120) {
                return std::nullopt;
            }
            options.encoder.frame_rate_numerator = fps;
            options.encoder.frame_rate_denominator = 1;
            options.encoder.gop_frames = fps * 2U;
        } else if (option == "--bitrate" && !bitrate_seen) {
            bitrate_seen = true;
            std::uint32_t bitrate = 0;
            if (!parse_integer(value, bitrate) || bitrate < 128'000 || bitrate > 50'000'000) {
                return std::nullopt;
            }
            options.encoder.bitrate = bitrate;
        } else {
            return std::nullopt;
        }
    }

    return options;
}

} // namespace catro::tools
