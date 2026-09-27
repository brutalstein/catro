#include "video_peer.hpp"

#include <Windows.h>

#include <algorithm>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

catro::tools::VideoPeerControl g_control{};

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
    const auto size =
        WideCharToMultiByte(
            CP_UTF8, 0, text.data(), length, nullptr, 0, nullptr, nullptr);
    std::string result(
        static_cast<std::size_t>(std::max(size, 0)), '\0');
    if (size > 0) {
        (void)WideCharToMultiByte(
            CP_UTF8, 0, text.data(), length,
            result.data(), size, nullptr, nullptr);
    }
    return result;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    std::vector<std::string> storage;
    storage.reserve(
        static_cast<std::size_t>(std::max(argc - 1, 0)));
    for (int index = 1; index < argc; ++index) {
        storage.push_back(utf8(argv[index]));
    }
    const std::vector<std::string_view> arguments(
        storage.begin(), storage.end());

    const auto options =
        catro::tools::parse_video_peer_arguments(arguments);
    if (!options) {
        std::cerr << catro::tools::kVideoPeerUsage;
        return catro::tools::video_peer_invalid_arguments;
    }

    g_control.stop_requested.store(false, std::memory_order_relaxed);
    const auto installed =
        SetConsoleCtrlHandler(console_control_handler, TRUE) != FALSE;
    const auto result =
        catro::tools::run_video_peer(
            *options, std::cout, std::cerr, &g_control);
    if (installed) {
        (void)SetConsoleCtrlHandler(console_control_handler, FALSE);
    }
    return result;
}
