#pragma once

#include <chrono>

namespace catro::shell {

enum class WindowActivity { foreground, background, hidden };

struct UiRefreshPolicy {
    std::chrono::milliseconds messages;
    std::chrono::milliseconds roster;
    std::chrono::milliseconds voice;
    std::chrono::milliseconds screen;
    bool local_preview;
};

// Presentation only: these intervals never control voice, capture, encode or transport workers.
inline constexpr UiRefreshPolicy ui_refresh_policy(
    WindowActivity activity, bool page_loaded, bool voice_channel,
    bool stream_popout = false) noexcept {
    using namespace std::chrono_literals;
    if (!page_loaded || activity == WindowActivity::hidden) {
        return {0ms, 0ms, 0ms, stream_popout ? 250ms : 0ms, false};
    }
    if (activity == WindowActivity::background) {
        return {5s, 30s, 2s, 1s, voice_channel};
    }
    // 250 ms voice keeps speaking indicators in step with the 300 ms speech hangover.
    return {1s, 5s, 250ms, 250ms, voice_channel};
}

} // namespace catro::shell
