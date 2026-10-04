#include "capture_check.hpp"

#include <catro/platform/windows/screen_capture.hpp>

#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

std::string utf8(std::wstring_view text) {
    if (text.empty()) {
        return {};
    }
    const auto length = static_cast<int>(text.size());
    const auto size =
        WideCharToMultiByte(CP_UTF8, 0, text.data(), length, nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(std::max(size, 0)), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), length, result.data(), size, nullptr, nullptr);
    return result;
}

std::string format_name(DXGI_FORMAT format) {
    if (format == DXGI_FORMAT_B8G8R8A8_UNORM) {
        return "BGRA8";
    }
    return "DXGI_FORMAT(" + std::to_string(static_cast<int>(format)) + ")";
}

void report_error(const catro::platform::windows::ScreenCaptureError& failure) {
    std::cerr << "catro-capture-check: "
              << catro::platform::windows::name(failure.code);
    if (failure.native_code != 0) {
        std::cerr << " (0x" << std::hex
                  << static_cast<std::uint64_t>(failure.native_code)
                  << std::dec << ")";
    }
    std::cerr << '\n';
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    std::vector<std::string> storage;
    storage.reserve(static_cast<std::size_t>(std::max(argc - 1, 0)));
    for (int index = 1; index < argc; ++index) {
        storage.push_back(utf8(argv[index]));
    }
    const std::vector<std::string_view> arguments(storage.begin(), storage.end());
    const auto options = catro::tools::parse_capture_check_arguments(arguments);
    if (!options) {
        std::cerr << catro::tools::kCaptureCheckUsage;
        return 2;
    }

    namespace windows = catro::platform::windows;
    const auto backend_name = [](windows::ScreenCaptureBackend backend) {
        return backend == windows::ScreenCaptureBackend::desktop_duplication ? "dxgi" : "wgc";
    };
    windows::WindowsGraphicsCapture capture;
    if (options->list || !options->source.empty()) {
        const auto sources = windows::enumerate_capture_sources();
        if (options->list) {
            for (const auto& source : sources) {
                std::cout << (source.kind == windows::CaptureSourceKind::display ? "display " : "window  ")
                          << backend_name(windows::recommended_capture_backend(source))
                          << (source.game ? " game " : "      ") << source.width << "x" << source.height
                          << " [" << source.process_name << "] " << source.title << '\n';
            }
            return 0;
        }
        const auto match = std::find_if(sources.begin(), sources.end(), [&](const auto& source) {
            return source.title.find(options->source) != std::string::npos ||
                   source.process_name.find(options->source) != std::string::npos;
        });
        if (match == sources.end()) {
            std::cerr << "catro-capture-check: no source matches \"" << options->source << "\"\n";
            return 4;
        }
        windows::ScreenCaptureConfig config;
        config.backend = windows::recommended_capture_backend(*match);
        std::cout << "source: " << match->title << " via " << backend_name(config.backend) << '\n';
        if (const auto failure = capture.start_source(*match, config)) {
            report_error(*failure);
            return 5;
        }
    } else {
        if (const auto failure = capture.start_primary_display()) {
            report_error(*failure);
            return 5;
        }
        std::cout << "source: primary display\n";
    }

    auto initial = capture.statistics();
    std::cout << "adapter-luid: 0x" << std::hex << initial.adapter_luid << std::dec << '\n';
    std::cout << "initial-size: " << initial.width << "x" << initial.height << '\n';
    std::cout << "pipeline: WGC -> D3D11 texture -> one-frame latest mailbox; no CPU pixel copy\n";

    const auto started = Clock::now();
    const auto deadline = started + options->duration;
    auto next_report = started + 1s;
    std::uint64_t consumed = 0;
    std::uint64_t last_sequence = 0;

    while (Clock::now() < deadline) {
        catro::platform::windows::GpuCaptureFrame frame;
        if (capture.wait_for_latest(frame, 50ms)) {
            ++consumed;
            last_sequence = frame.sequence;
        }

        const auto now = Clock::now();
        if (now >= next_report) {
            const auto stats = capture.statistics();
            const auto elapsed =
                std::chrono::duration_cast<std::chrono::seconds>(now - started).count();

            std::cout << elapsed << "s:"
                      << " received " << stats.frames_received
                      << " published " << stats.frames_published
                      << " consumed " << consumed
                      << " overwrite " << stats.mailbox_overwrites
                      << " contention-drop " << stats.contention_drops
                      << " resize " << stats.resize_events
                      << " size " << stats.width << "x" << stats.height
                      << " format " << format_name(stats.format)
                      << " last-seq " << last_sequence
                      << '\n';

            if (stats.state == catro::platform::windows::ScreenCaptureState::failed ||
                stats.state == catro::platform::windows::ScreenCaptureState::source_closed) {
                break;
            }

            do {
                next_report += 1s;
            } while (next_report <= now);
        }
    }

    const auto final = capture.statistics();
    capture.stop();

    std::cout << "final:"
              << " received " << final.frames_received
              << ", published " << final.frames_published
              << ", consumed " << consumed
              << ", overwrite " << final.mailbox_overwrites
              << ", contention-drop " << final.contention_drops
              << ", resize " << final.resize_events
              << ", size " << final.width << "x" << final.height
              << ", format " << format_name(final.format)
              << ", last-seq " << last_sequence
              << '\n';

    if (final.error) {
        report_error(*final.error);
        return 5;
    }
    if (final.state == catro::platform::windows::ScreenCaptureState::source_closed) {
        std::cerr << "catro-capture-check: capture source closed before the test finished\n";
        return 5;
    }
    if (final.frames_received == 0 || consumed == 0) {
        std::cerr << "catro-capture-check: no GPU frames were delivered\n";
        return 6;
    }
    return 0;
}
