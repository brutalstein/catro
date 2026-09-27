#include "voice_peer.hpp"

#include <atomic>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#include <catro/platform/windows/audio_platform.hpp>

#include <Windows.h>

#include <algorithm>

namespace {

using Platform = catro::platform::windows::WasapiAudioPlatform;
catro::tools::VoicePeerControl g_control{};

BOOL WINAPI console_control_handler(DWORD event) {
    if (event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT) {
        g_control.stop_requested.store(true, std::memory_order_relaxed);
        return TRUE;
    }
    return FALSE;
}

std::string utf8(std::wstring_view text) {
    if (text.empty()) {
        return {};
    }
    const auto length = static_cast<int>(text.size());
    const auto size = WideCharToMultiByte(CP_UTF8, 0, text.data(), length, nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(std::max(size, 0)), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), length, result.data(), size, nullptr, nullptr);
    return result;
}

} // namespace
#else
#include <catro/platform/macos/audio_platform.hpp>

#include <csignal>

namespace {

using Platform = catro::platform::macos::CoreAudioPlatform;
static_assert(std::atomic_bool::is_always_lock_free);
std::atomic_bool g_stop_requested{false};

void stop_signal_handler(int) {
    g_control.stop_requested.store(true, std::memory_order_relaxed);
}

} // namespace
#endif

namespace {

int run(const std::vector<std::string>& storage) {
    const std::vector<std::string_view> arguments(storage.begin(), storage.end());
    Platform platform;
    return catro::tools::run_voice_peer(arguments, platform, std::cout, std::cerr,
                                        &g_control);
}

} // namespace

#if defined(_WIN32)
int wmain(int argc, wchar_t** argv) {
    std::vector<std::string> storage;
    storage.reserve(static_cast<std::size_t>(std::max(argc - 1, 0)));
    for (int index = 1; index < argc; ++index) {
        storage.push_back(utf8(argv[index]));
    }

    g_control.stop_requested.store(false, std::memory_order_relaxed);
    const auto installed = SetConsoleCtrlHandler(console_control_handler, TRUE) != FALSE;
    const auto result = run(storage);
    if (installed) {
        (void)SetConsoleCtrlHandler(console_control_handler, FALSE);
    }
    return result;
}
#else
int main(int argc, char** argv) {
    g_control.stop_requested.store(false, std::memory_order_relaxed);
    const auto previous_int = std::signal(SIGINT, stop_signal_handler);
    const auto previous_term = std::signal(SIGTERM, stop_signal_handler);
    const auto result = run(std::vector<std::string>(argv + 1, argv + argc));
    if (previous_int != SIG_ERR) {
        (void)std::signal(SIGINT, previous_int);
    }
    if (previous_term != SIG_ERR) {
        (void)std::signal(SIGTERM, previous_term);
    }
    return result;
}
#endif
