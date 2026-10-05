#pragma once

#include <chrono>
#include <ctime>
#include <string_view>

namespace catro::shell {

inline std::chrono::sys_days message_day(const std::tm& local) {
    using namespace std::chrono;
    return sys_days{year{local.tm_year + 1900} / month{static_cast<unsigned>(local.tm_mon + 1)} /
                    day{static_cast<unsigned>(local.tm_mday)}};
}

inline int message_day_age(const std::tm& message, const std::tm& now) {
    return static_cast<int>((message_day(now) - message_day(message)).count());
}

inline bool voice_member_visible(bool self, bool joined, std::string_view member_channel,
                                 std::string_view channel) {
    return !channel.empty() && (self ? joined : member_channel == channel);
}

} // namespace catro::shell
