#include "voice_peer.hpp"

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

namespace {

using Platform = catro::platform::macos::CoreAudioPlatform;

} // namespace
#endif

namespace {

int run(const std::vector<std::string>& storage) {
    const std::vector<std::string_view> arguments(storage.begin(), storage.end());
    Platform platform;
    return catro::tools::run_voice_peer(arguments, platform, std::cout, std::cerr);
}

} // namespace

#if defined(_WIN32)
int wmain(int argc, wchar_t** argv) {
    std::vector<std::string> storage;
    storage.reserve(static_cast<std::size_t>(std::max(argc - 1, 0)));
    for (int index = 1; index < argc; ++index) {
        storage.push_back(utf8(argv[index]));
    }
    return run(storage);
}
#else
int main(int argc, char** argv) {
    return run(std::vector<std::string>(argv + 1, argv + argc));
}
#endif
