#include "audio_check.hpp"

#include <catro/platform/windows/audio_platform.hpp>

#include <Windows.h>

#include <algorithm>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

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

int wmain(int argc, wchar_t** argv) {
    std::vector<std::string> storage;
    for (int index = 1; index < argc; ++index) {
        storage.push_back(utf8(argv[index]));
    }
    const std::vector<std::string_view> arguments(storage.begin(), storage.end());
    catro::platform::windows::WasapiAudioPlatform platform;
    return catro::tools::run_audio_check(
        arguments, platform, [](std::chrono::milliseconds duration) { std::this_thread::sleep_for(duration); },
        std::cout, std::cerr);
}
