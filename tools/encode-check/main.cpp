#include "encode_check.hpp"

#include <catro/platform/windows/screen_capture.hpp>
#include <catro/platform/windows/video_encoder.hpp>

#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
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
    if (size > 0) {
        (void)WideCharToMultiByte(
            CP_UTF8, 0, text.data(), length, result.data(), size, nullptr, nullptr);
    }
    return result;
}

void report_capture_error(const catro::platform::windows::ScreenCaptureError& failure) {
    std::cerr << "catro-encode-check: "
              << catro::platform::windows::name(failure.code);
    if (failure.native_code != 0) {
        std::cerr << " (0x" << std::hex
                  << static_cast<std::uint64_t>(failure.native_code)
                  << std::dec << ")";
    }
    std::cerr << '\n';
}

void report_encoder_error(const catro::platform::windows::HardwareEncoderError& failure) {
    std::cerr << "catro-encode-check: "
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
    const auto options = catro::tools::parse_encode_check_arguments(arguments);
    if (!options) {
        std::cerr << catro::tools::kEncodeCheckUsage;
        return 2;
    }

    catro::platform::windows::WindowsGraphicsCapture capture;
    if (const auto failure = capture.start_primary_display()) {
        report_capture_error(*failure);
        return 5;
    }

    catro::platform::windows::GpuCaptureFrame first;
    if (!capture.wait_for_latest(first, 1500ms)) {
        const auto stats = capture.statistics();
        capture.stop();
        if (stats.error) {
            report_capture_error(*stats.error);
        } else {
            std::cerr << "catro-encode-check: no initial GPU frame arrived\n";
        }
        return 6;
    }

    auto config = options->encoder;
    config.adapter_luid = capture.statistics().adapter_luid;

    catro::platform::windows::WindowsH264HardwareEncoder encoder;
    if (const auto failure = encoder.start(config, *first.texture.Get())) {
        report_encoder_error(*failure);
        capture.stop();
        return 7;
    }

    const auto initial = encoder.statistics();
    std::cout << "source: primary display "
              << first.width << "x" << first.height << " BGRA8\n";
    std::cout << "target: " << config.width << "x" << config.height
              << "@" << config.frame_rate_numerator
              << " H264 " << config.bitrate << " bit/s\n";
    std::cout << "adapter-luid: 0x" << std::hex << initial.adapter_luid << std::dec << '\n';
    std::cout << "encoder: "
              << (initial.encoder_name.empty() ? "<unnamed hardware MFT>" : initial.encoder_name)
              << "\n";
    std::cout << "path: GPU BGRA -> GPU NV12 -> hardware MFT; raw pixels never map to CPU\n";
    std::cout << "async: " << (initial.asynchronous ? "yes" : "no")
              << ", d3d11-aware: " << (initial.d3d11_aware ? "yes" : "no")
              << ", low-latency: " << (initial.low_latency_applied ? "enabled" : "not-confirmed")
              << ", bt709: " << (initial.explicit_bt709_conversion ? "explicit" : "driver-default")
              << "\n";

    catro::platform::windows::EncodedAccessUnit access_unit;
    if (const auto failure = encoder.encode(first, access_unit)) {
        report_encoder_error(*failure);
        encoder.stop();
        capture.stop();
        return 8;
    }

    const auto started = Clock::now();
    const auto deadline = started + options->duration;
    const auto frame_period =
        std::chrono::duration_cast<Clock::duration>(
            std::chrono::duration<double>(
                static_cast<double>(config.frame_rate_denominator) /
                static_cast<double>(config.frame_rate_numerator)));
    auto next_frame = started + frame_period;
    auto next_report = started + 1s;

    while (Clock::now() < deadline) {
        if (Clock::now() < next_frame) {
            std::this_thread::sleep_until(next_frame);
        }
        do {
            next_frame += frame_period;
        } while (next_frame <= Clock::now());

        catro::platform::windows::GpuCaptureFrame frame;
        if (capture.wait_for_latest(frame, 5ms)) {
            if (const auto failure = encoder.encode(frame, access_unit)) {
                report_encoder_error(*failure);
                encoder.stop();
                capture.stop();
                return 8;
            }
        }

        const auto now = Clock::now();
        if (now >= next_report) {
            const auto capture_stats = capture.statistics();
            const auto encode_stats = encoder.statistics();
            const auto encoded = std::max<std::uint64_t>(encode_stats.frames_encoded, 1);
            const auto elapsed =
                std::chrono::duration_cast<std::chrono::seconds>(now - started).count();

            std::cout << elapsed << "s:"
                      << " capture " << capture_stats.frames_received
                      << " encoded " << encode_stats.frames_encoded
                      << " key " << encode_stats.keyframes
                      << " bytes " << encode_stats.encoded_bytes
                      << " convert " << (encode_stats.conversion_total_us / encoded)
                      << "/" << encode_stats.conversion_max_us << " us"
                      << " encode " << (encode_stats.encode_total_us / encoded)
                      << "/" << encode_stats.encode_max_us << " us"
                      << " cap-overwrite " << capture_stats.mailbox_overwrites
                      << " cap-contention " << capture_stats.contention_drops
                      << " timeout " << encode_stats.output_timeouts
                      << '\n';

            if (capture_stats.error) {
                report_capture_error(*capture_stats.error);
                encoder.stop();
                capture.stop();
                return 5;
            }

            do {
                next_report += 1s;
            } while (next_report <= now);
        }
    }

    const auto capture_stats = capture.statistics();
    const auto encode_stats = encoder.statistics();
    encoder.stop();
    capture.stop();

    const auto encoded = std::max<std::uint64_t>(encode_stats.frames_encoded, 1);
    std::cout << "final:"
              << " capture " << capture_stats.frames_received
              << ", submitted " << encode_stats.frames_submitted
              << ", encoded " << encode_stats.frames_encoded
              << ", bytes " << encode_stats.encoded_bytes
              << ", keyframes " << encode_stats.keyframes
              << ", conversion-fail " << encode_stats.conversion_failures
              << ", input-fail " << encode_stats.input_failures
              << ", output-fail " << encode_stats.output_failures
              << ", timeout " << encode_stats.output_timeouts
              << ", convert avg/max " << (encode_stats.conversion_total_us / encoded)
              << "/" << encode_stats.conversion_max_us << " us"
              << ", encode avg/max " << (encode_stats.encode_total_us / encoded)
              << "/" << encode_stats.encode_max_us << " us"
              << ", capture-overwrite " << capture_stats.mailbox_overwrites
              << ", capture-contention " << capture_stats.contention_drops
              << '\n';

    if (encode_stats.frames_encoded == 0 || encode_stats.encoded_bytes == 0) {
        std::cerr << "catro-encode-check: hardware encoder produced no H.264 access units\n";
        return 9;
    }
    if (encode_stats.conversion_failures != 0 || encode_stats.input_failures != 0 ||
        encode_stats.output_failures != 0 || encode_stats.output_timeouts != 0) {
        std::cerr << "catro-encode-check: hardware encoder reported pipeline failures\n";
        return 9;
    }
    return 0;
}
