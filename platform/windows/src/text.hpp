#pragma once

#include <Windows.h>

#include <string>
#include <string_view>

// UTF-16 and UTF-8 conversion at the Windows API boundary. Invalid input yields an empty string.
namespace catro::platform::windows {

inline std::string utf8(std::wstring_view text) {
    if (text.empty()) {
        return {};
    }
    const auto length = static_cast<int>(text.size());
    const auto size = WideCharToMultiByte(CP_UTF8, 0, text.data(), length, nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return {};
    }
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), length, result.data(), size, nullptr, nullptr);
    return result;
}

inline std::wstring wide(std::string_view text) {
    if (text.empty()) {
        return {};
    }
    const auto length = static_cast<int>(text.size());
    const auto size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), length, nullptr, 0);
    if (size <= 0) {
        return {};
    }
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), length, result.data(), size);
    return result;
}

} // namespace catro::platform::windows
