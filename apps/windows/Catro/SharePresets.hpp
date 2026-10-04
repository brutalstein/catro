#pragma once

#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <string>
#include <string_view>
#include <vector>

namespace catro::shell {

// Discord-style share quality: the user picks only a resolution. Frame rate and starting
// bitrate follow from the resolution and the GPU; the sender's adaptive bitrate takes it from
// there.
struct ShareQuality {
    std::wstring label;
    std::uint32_t max_width = 0;
    std::uint32_t max_height = 0;
    std::uint32_t fps = 0;
    std::uint32_t bitrate = 0;
};

struct ShareQualityChoice {
    std::vector<ShareQuality> options;
    std::size_t recommended = 0;
};

// strong_gpu: a GPU with its own video memory. Integrated GPUs stop at 1080p and keep 60 FPS
// only at 720p. Presets never exceed the source height, so nothing is upscaled; "Source" covers
// a source larger than every preset or smaller than the smallest one. A window can be resized
// while it is shared, so the caller passes the screen size for windows, not the window size.
inline ShareQualityChoice share_qualities(
    std::uint32_t source_width, std::uint32_t source_height, bool strong_gpu) {
    const auto make = [strong_gpu](std::wstring label, std::uint32_t height) {
        ShareQuality quality;
        quality.label = std::move(label);
        quality.max_height = height;
        // "p" names the height; a wide box keeps ultrawide sources at that height.
        quality.max_width = height * 32 / 9;
        quality.fps = strong_gpu || height <= 720 ? 60 : 30;
        // About 0.06 bits per pixel of a 16:9 frame, inside what a home uplink sustains.
        const auto pixels = static_cast<double>(height) * height * 16.0 / 9.0;
        quality.bitrate = static_cast<std::uint32_t>(
            std::clamp(pixels * quality.fps * 0.06, 2'500'000.0, 25'000'000.0));
        return quality;
    };

    ShareQualityChoice choice;
    const std::uint32_t ceiling = strong_gpu ? 1440 : 1080;
    for (const std::uint32_t height : {720U, 1080U, 1440U}) {
        if (height <= source_height && height <= ceiling) {
            choice.options.push_back(make(std::to_wstring(height) + L"p", height));
        }
    }
    const std::uint32_t largest = choice.options.empty() ? 0 : choice.options.back().max_height;
    if (source_height > largest && (strong_gpu || source_height <= ceiling)) {
        // The encoder never upscales, so the floor only keeps the box inside what it accepts.
        auto source = make(L"Source", std::max(source_height, 180U));
        source.max_width = std::max(source_width, 320U);
        choice.options.push_back(std::move(source));
    }
    if (choice.options.empty()) {
        choice.options.push_back(make(L"1080p", 1080));
    }
    // The best the computer handles, but not a 4K source by default.
    choice.recommended = choice.options.size() - 1;
    if (choice.recommended > 0 && choice.options[choice.recommended].max_height > 1440) {
        --choice.recommended;
    }
    return choice;
}

// Drops the presets above what this GPU's encoder actually started (it fell back to a smaller
// size once), so the picker stops offering a resolution that cannot be met. 0 means no limit.
inline void cap_share_qualities(ShareQualityChoice& choice, std::uint32_t encoder_max_height) {
    if (encoder_max_height == 0) {
        return;
    }
    const auto fits = [encoder_max_height](const ShareQuality& quality) {
        return quality.max_height <= encoder_max_height;
    };
    if (std::ranges::none_of(choice.options, fits)) {
        // Even the smallest preset failed: offer one the encoder did start.
        auto quality = choice.options.front();
        quality.label = std::to_wstring(encoder_max_height) + L"p";
        quality.max_height = encoder_max_height;
        quality.max_width = encoder_max_height * 32 / 9;
        choice.options = {std::move(quality)};
    } else {
        std::erase_if(choice.options, [&fits](const ShareQuality& quality) { return !fits(quality); });
    }
    choice.recommended = choice.options.size() - 1;
}

namespace detail {

inline std::wstring lower(std::wstring_view text) {
    std::wstring result(text);
    for (auto& c : result) {
        c = static_cast<wchar_t>(std::towlower(c));
    }
    return result;
}

inline std::wstring_view trim(std::wstring_view text) {
    while (!text.empty() && (std::iswspace(text.front()) || text.front() == L'\u200B')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (std::iswspace(text.back()) || text.back() == L'\u200B')) {
        text.remove_suffix(1);
    }
    return text;
}

// "Video - YouTube — Brave" -> {"Video", "YouTube", "Brave"}
inline std::vector<std::wstring> title_parts(std::wstring_view title) {
    std::vector<std::wstring> parts;
    std::wstring current;
    for (std::size_t i = 0; i < title.size(); ++i) {
        const bool dash = title[i] == L'-' || title[i] == L'\u2013' || title[i] == L'\u2014';
        if (dash && i > 0 && i + 1 < title.size() && title[i - 1] == L' ' && title[i + 1] == L' ') {
            parts.emplace_back(trim(current));
            current.clear();
            ++i;
            continue;
        }
        current += title[i];
    }
    parts.emplace_back(trim(current));
    std::erase_if(parts, [](const std::wstring& part) { return part.empty(); });
    return parts;
}

} // namespace detail

// Short browser name for a browser executable, empty for anything else.
inline std::wstring browser_name(std::wstring_view process_name) {
    const auto process = detail::lower(process_name);
    if (process == L"brave.exe") return L"Brave";
    if (process == L"chrome.exe") return L"Chrome";
    if (process == L"msedge.exe") return L"Edge";
    if (process == L"firefox.exe") return L"Firefox";
    if (process == L"opera.exe") return L"Opera";
    if (process == L"vivaldi.exe") return L"Vivaldi";
    return {};
}

// The name a person recognizes in the share picker: "Brave - YouTube", "Counter-Strike 2",
// "Visual Studio Code". app_name is the program's own description, or empty.
inline std::wstring share_window_name(
    std::wstring_view title, std::wstring_view process_name, std::wstring_view app_name,
    bool game) {
    const auto clean_title = std::wstring(detail::trim(title));
    if (game && !clean_title.empty()) {
        return clean_title;
    }

    const auto browser = browser_name(process_name);
    if (!browser.empty()) {
        auto parts = detail::title_parts(clean_title);
        const auto key = detail::lower(browser);
        std::erase_if(parts, [&](const std::wstring& part) {
            return detail::lower(part).find(key) != std::wstring::npos;
        });
        if (parts.empty()) {
            return browser;
        }
        auto site = parts.back();
        // "(3) Inbox" -> "Inbox"
        if (site.size() > 2 && site.front() == L'(') {
            const auto close = site.find(L") ");
            if (close != std::wstring::npos) {
                site.erase(0, close + 2);
            }
        }
        return browser + L" - " + site;
    }

    if (!app_name.empty()) {
        return std::wstring(detail::trim(app_name));
    }
    if (!clean_title.empty()) {
        return clean_title;
    }
    // "spotify.exe" -> "Spotify"
    std::wstring stem(process_name.substr(0, process_name.rfind(L'.')));
    if (!stem.empty()) {
        stem[0] = static_cast<wchar_t>(std::towupper(stem[0]));
    }
    return stem;
}

} // namespace catro::shell
