#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <string_view>

namespace catro::rtc {

[[nodiscard]] constexpr bool screen_media_allowed(
    std::string_view owner,
    std::string_view sender) noexcept {
    return !owner.empty() && owner == sender;
}

// A viewer asks the sharer for a keyframe (like an RTCP PLI) with this datagram on the video lane.
// RTP always starts with version bits 0b10, so a leading zero byte can never be media.
inline constexpr std::array<std::byte, 4> kKeyframeRequest{
    std::byte{0x00}, std::byte{'K'}, std::byte{'F'}, std::byte{'R'}};

[[nodiscard]] inline bool is_keyframe_request(std::span<const std::byte> datagram) noexcept {
    return datagram.size() == kKeyframeRequest.size() &&
           std::equal(datagram.begin(), datagram.end(), kKeyframeRequest.begin());
}

} // namespace catro::rtc
