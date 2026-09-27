#include <catro/screen_runtime.hpp>

#include <catro/platform/windows/video_encoder.hpp>
#include <catro/platform/windows/video_presenter.hpp>
#include <catro/video/geometry.hpp>
#include <catro/video/rtp_h264.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <variant>

namespace catro::screen {
namespace {

using Microsoft::WRL::ComPtr;
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
using platform::windows::D3D11CompositionVideoPresenter;
using platform::windows::EncodedAccessUnit;
using platform::windows::GpuCaptureFrame;
using platform::windows::HardwareEncoderConfig;
using platform::windows::VideoPresenterConfig;
using platform::windows::WindowsGraphicsCapture;
using platform::windows::WindowsH264HardwareEncoder;
using transport::UdpError;
using transport::UdpErrorCode;
using transport::UdpPeerSocket;

constexpr auto kFirstFrameTimeout = 3s;
constexpr std::uint32_t kPreviewMaxFps = 30;

[[nodiscard]] bool valid_config(const ScreenShareConfig& config) noexcept {
    return config.source.native_handle != 0 &&
           !config.bind.address.empty() &&
           !config.peer.address.empty() &&
           config.max_width >= 320 &&
           config.max_width <= 7680 &&
           config.max_height >= 180 &&
           config.max_height <= 4320 &&
           config.fps >= 1 &&
           config.fps <= 120 &&
           config.bitrate >= 128'000 &&
           config.bitrate <= 50'000'000 &&
           config.ssrc != 0 &&
           config.payload_type >= 96 &&
           config.payload_type <= 127 &&
           config.mtu_bytes >= 576 &&
           config.mtu_bytes <= 1400 &&
           config.max_access_unit_bytes >= 262'144 &&
           config.max_access_unit_bytes <= 16U * 1024U * 1024U;
}

[[nodiscard]] std::chrono::nanoseconds frame_period(
    std::uint32_t fps) noexcept {
    constexpr std::uint64_t kNanosecondsPerSecond =
        1'000'000'000ULL;
    return std::chrono::nanoseconds(
        static_cast<std::int64_t>(
            (kNanosecondsPerSecond + fps / 2U) / fps));
}

[[nodiscard]] std::uint32_t monotonic_rtp_timestamp(
    Clock::time_point started,
    Clock::time_point now) noexcept {
    const auto elapsed_100ns =
        std::chrono::duration_cast<
            std::chrono::duration<std::uint64_t, std::ratio<1, 10'000'000>>>(
            now - started)
            .count();
    return video::rtp_timestamp_90khz(elapsed_100ns);
}

[[nodiscard]] std::string udp_error_text(const UdpError& error) {
    std::string text{transport::name(error.code)};
    if (error.native_code != 0) {
        text += " (native ";
        text += std::to_string(error.native_code);
        text += ")";
    }
    return text;
}

} // namespace

struct WindowsScreenShareRuntime::Impl {
    struct PacketContext {
        UdpPeerSocket* socket = nullptr;
        Impl* owner = nullptr;
        bool soft_drop = false;
        std::optional<UdpError> fatal_error;

        static bool send(
            void* opaque,
            const video::RtpPacketSlice& packet) noexcept {
            auto& context =
                *static_cast<PacketContext*>(opaque);
            const std::array<std::span<const std::byte>, 2>
                segments{
                    std::span<const std::byte>(
                        packet.prefix.data(),
                        static_cast<std::size_t>(
                            packet.prefix_size)),
                    packet.payload,
                };
            const auto result =
                context.socket->send_segments(segments);
            if (const auto* bytes =
                    std::get_if<std::size_t>(&result)) {
                context.owner->packets_sent_.fetch_add(
                    1, std::memory_order_relaxed);
                context.owner->wire_bytes_.fetch_add(
                    *bytes, std::memory_order_relaxed);
                return true;
            }

            const auto failure = std::get<UdpError>(result);
            if (failure.code == UdpErrorCode::would_block) {
                context.owner->backpressure_events_.fetch_add(
                    1, std::memory_order_relaxed);
                context.soft_drop = true;
                return false;
            }
            if (failure.code ==
                UdpErrorCode::peer_unreachable) {
                context.owner->peer_unreachable_events_.fetch_add(
                    1, std::memory_order_relaxed);
                context.soft_drop = true;
                return false;
            }

            context.fatal_error = failure;
            return false;
        }
    };

    [[nodiscard]] std::optional<ScreenShareError> start(
        const ScreenShareConfig& config) {
        stop();

        if (!valid_config(config)) {
            return ScreenShareError{
                ScreenShareErrorCode::invalid_config,
                "invalid screen-share configuration"};
        }

        reset_statistics();
        {
            std::scoped_lock lock(metadata_mutex_);
            source_title_ = config.source.title;
            error_.clear();
        }
        state_.store(
            ScreenShareState::starting,
            std::memory_order_release);
        stop_requested_.store(false, std::memory_order_release);

        try {
            worker_ = std::thread(
                [this, config] { run(config); });
        } catch (...) {
            state_.store(
                ScreenShareState::failed,
                std::memory_order_release);
            return ScreenShareError{
                ScreenShareErrorCode::worker_start_failed,
                "screen-share worker could not start"};
        }
        return std::nullopt;
    }

    void stop() noexcept {
        stop_requested_.store(true, std::memory_order_release);
        stop_cv_.notify_all();
        if (worker_.joinable()) {
            worker_.join();
        }
        {
            std::scoped_lock lock(preview_mutex_);
            preview_swap_chain_.Reset();
        }
        state_.store(
            ScreenShareState::idle,
            std::memory_order_release);
        stop_requested_.store(false, std::memory_order_release);
    }

    void run(ScreenShareConfig config) noexcept {
        auto opened = UdpPeerSocket::bind(config.bind);
        if (const auto* error = std::get_if<UdpError>(&opened)) {
            fail(ScreenShareErrorCode::network_failed,
                 udp_error_text(*error));
            return;
        }
        auto socket =
            std::move(
                std::get<std::unique_ptr<UdpPeerSocket>>(opened));
        const auto connected = socket->connect_peer(config.peer);
        if (const auto* error =
                std::get_if<UdpError>(&connected)) {
            fail(ScreenShareErrorCode::network_failed,
                 udp_error_text(*error));
            return;
        }

        WindowsGraphicsCapture capture;
        platform::windows::ScreenCaptureConfig capture_config;
        capture_config.borderless = config.borderless;
        if (const auto error =
                capture.start_source(config.source, capture_config)) {
            fail(
                ScreenShareErrorCode::capture_failed,
                platform::windows::name(error->code),
                error->native_code);
            return;
        }

        GpuCaptureFrame first;
        const auto first_deadline =
            Clock::now() + kFirstFrameTimeout;
        while (!stop_requested_.load(
                   std::memory_order_acquire) &&
               Clock::now() < first_deadline &&
               !capture.wait_for_latest(first, 50ms)) {
            const auto stats = capture.statistics();
            if (stats.error) {
                fail(
                    ScreenShareErrorCode::capture_failed,
                    platform::windows::name(
                        stats.error->code),
                    stats.error->native_code);
                capture.stop();
                return;
            }
        }
        if (stop_requested_.load(std::memory_order_acquire)) {
            capture.stop();
            return;
        }
        if (!first.texture) {
            fail(
                ScreenShareErrorCode::capture_failed,
                "capture source did not produce a GPU frame");
            capture.stop();
            return;
        }

        WindowsH264HardwareEncoder encoder;
        D3D11CompositionVideoPresenter presenter(
            VideoPresenterConfig{
                .max_width = 960,
                .max_height = 540,
                .frame_rate =
                    std::min(config.fps, kPreviewMaxFps),
            });

        EncodedAccessUnit access_unit;
        try {
            access_unit.bytes.reserve(
                config.max_access_unit_bytes);
        } catch (...) {
            fail(
                ScreenShareErrorCode::encoder_failed,
                "encoded access-unit reserve failed");
            capture.stop();
            return;
        }

        std::uint16_t next_sequence = 1;
        const video::H264RtpConfig rtp{
            config.ssrc,
            config.payload_type,
            config.mtu_bytes,
        };
        const auto started = Clock::now();
        std::uint32_t current_source_width = 0;
        std::uint32_t current_source_height = 0;
        std::uint32_t preview_accumulator = 0;

        const auto configure_encoder =
            [&](const GpuCaptureFrame& frame) -> bool {
            const auto extent = video::fit_even_video_extent(
                frame.width,
                frame.height,
                config.max_width,
                config.max_height);
            if (!extent) {
                fail(
                    ScreenShareErrorCode::encoder_failed,
                    "source cannot be fitted to an even H.264 size");
                return false;
            }

            encoder.stop();
            HardwareEncoderConfig encoder_config;
            encoder_config.width = extent->width;
            encoder_config.height = extent->height;
            encoder_config.frame_rate_numerator = config.fps;
            encoder_config.frame_rate_denominator = 1;
            encoder_config.bitrate = config.bitrate;
            encoder_config.gop_frames = config.fps * 2U;
            encoder_config.max_access_unit_bytes =
                config.max_access_unit_bytes;
            const auto capture_stats = capture.statistics();
            if (capture_stats.adapter_luid != 0) {
                encoder_config.adapter_luid =
                    capture_stats.adapter_luid;
            }

            if (const auto error =
                    encoder.start(
                        encoder_config,
                        *frame.texture.Get())) {
                fail(
                    ScreenShareErrorCode::encoder_failed,
                    platform::windows::name(error->code),
                    error->native_code);
                return false;
            }

            current_source_width = frame.width;
            current_source_height = frame.height;
            source_width_.store(
                frame.width, std::memory_order_relaxed);
            source_height_.store(
                frame.height, std::memory_order_relaxed);
            encoded_width_.store(
                extent->width, std::memory_order_relaxed);
            encoded_height_.store(
                extent->height, std::memory_order_relaxed);
            return true;
        };

        const auto publish_preview_swap_chain = [&] {
            const auto swap_chain = presenter.swap_chain();
            if (!swap_chain) {
                return;
            }
            std::scoped_lock lock(preview_mutex_);
            if (preview_swap_chain_.Get() !=
                swap_chain.Get()) {
                preview_swap_chain_ = swap_chain;
            }
        };

        const auto process_frame =
            [&](const GpuCaptureFrame& frame) -> bool {
            if (!frame.texture) {
                return true;
            }

            if (frame.width != current_source_width ||
                frame.height != current_source_height) {
                if (!configure_encoder(frame)) {
                    return false;
                }
            }

            preview_accumulator +=
                std::min(config.fps, kPreviewMaxFps);
            if (preview_accumulator >= config.fps) {
                preview_accumulator -= config.fps;
                if (const auto error =
                        presenter.present(
                            *frame.texture.Get())) {
                    fail(
                        ScreenShareErrorCode::preview_failed,
                        platform::windows::name(error->code),
                        error->native_code);
                    return false;
                }
                const auto preview_stats = presenter.statistics();
                preview_frames_.store(
                    preview_stats.frames_presented,
                    std::memory_order_relaxed);
                preview_drops_.store(
                    preview_stats.frames_dropped,
                    std::memory_order_relaxed);
                publish_preview_swap_chain();
            }

            if (const auto error =
                    encoder.encode(frame, access_unit)) {
                fail(
                    ScreenShareErrorCode::encoder_failed,
                    platform::windows::name(error->code),
                    error->native_code);
                return false;
            }
            frames_encoded_.fetch_add(
                1, std::memory_order_relaxed);

            PacketContext context{
                .socket = socket.get(),
                .owner = this,
            };
            const auto packetized =
                video::packetize_h264_annex_b(
                    access_unit.bytes,
                    monotonic_rtp_timestamp(
                        started, Clock::now()),
                    next_sequence,
                    rtp,
                    &context,
                    &PacketContext::send);
            next_sequence = packetized.next_sequence;

            if (!packetized) {
                if (context.fatal_error) {
                    fail(
                        ScreenShareErrorCode::network_failed,
                        udp_error_text(
                            *context.fatal_error));
                    return false;
                }
                if (context.soft_drop) {
                    frames_dropped_.fetch_add(
                        1, std::memory_order_relaxed);
                    return true;
                }
                fail(
                    ScreenShareErrorCode::packetization_failed,
                    "H.264 access unit is not RFC 6184 packetizable");
                return false;
            }

            frames_sent_.fetch_add(
                1, std::memory_order_relaxed);
            return true;
        };

        if (!configure_encoder(first)) {
            capture.stop();
            return;
        }
        state_.store(
            ScreenShareState::sharing,
            std::memory_order_release);

        if (!process_frame(first)) {
            encoder.stop();
            capture.stop();
            return;
        }
        first = {};

        const auto period = frame_period(config.fps);
        auto next_frame = Clock::now() + period;

        while (!stop_requested_.load(
                   std::memory_order_acquire)) {
            {
                std::unique_lock stop_lock(stop_mutex_);
                stop_cv_.wait_until(
                    stop_lock,
                    next_frame,
                    [this] {
                        return stop_requested_.load(
                            std::memory_order_acquire);
                    });
            }
            if (stop_requested_.load(std::memory_order_acquire)) {
                break;
            }

            auto now = Clock::now();
            do {
                next_frame += period;
            } while (next_frame <= now);

            GpuCaptureFrame frame;
            if (capture.wait_for_latest(frame, 5ms)) {
                if (!process_frame(frame)) {
                    break;
                }
            }

            const auto capture_stats = capture.statistics();
            if (capture_stats.error) {
                fail(
                    ScreenShareErrorCode::capture_failed,
                    platform::windows::name(
                        capture_stats.error->code),
                    capture_stats.error->native_code);
                break;
            }
        }

        const auto encoder_stats = encoder.statistics();
        const auto capture_stats = capture.statistics();
        const auto preview_stats = presenter.statistics();
        frames_encoded_.store(
            encoder_stats.frames_encoded,
            std::memory_order_relaxed);
        encoder_input_failures_.store(
            encoder_stats.input_failures,
            std::memory_order_relaxed);
        encoder_output_failures_.store(
            encoder_stats.output_failures,
            std::memory_order_relaxed);
        encoder_timeouts_.store(
            encoder_stats.output_timeouts,
            std::memory_order_relaxed);
        capture_contention_drops_.store(
            capture_stats.contention_drops,
            std::memory_order_relaxed);
        preview_frames_.store(
            preview_stats.frames_presented,
            std::memory_order_relaxed);
        preview_drops_.store(
            preview_stats.frames_dropped,
            std::memory_order_relaxed);

        encoder.stop();
        capture.stop();
        presenter.reset();

        if (state_.load(std::memory_order_acquire) !=
                ScreenShareState::failed &&
            !stop_requested_.load(
                std::memory_order_acquire)) {
            state_.store(
                ScreenShareState::idle,
                std::memory_order_release);
        }
    }

    void fail(
        ScreenShareErrorCode,
        std::string message,
        std::int64_t native_code = 0) noexcept {
        try {
            if (native_code != 0) {
                message += " (native ";
                message += std::to_string(native_code);
                message += ")";
            }
            std::scoped_lock lock(metadata_mutex_);
            error_ = std::move(message);
        } catch (...) {
        }
        state_.store(
            ScreenShareState::failed,
            std::memory_order_release);
    }

    void reset_statistics() noexcept {
        source_width_.store(0, std::memory_order_relaxed);
        source_height_.store(0, std::memory_order_relaxed);
        encoded_width_.store(0, std::memory_order_relaxed);
        encoded_height_.store(0, std::memory_order_relaxed);
        frames_encoded_.store(0, std::memory_order_relaxed);
        frames_sent_.store(0, std::memory_order_relaxed);
        frames_dropped_.store(0, std::memory_order_relaxed);
        packets_sent_.store(0, std::memory_order_relaxed);
        wire_bytes_.store(0, std::memory_order_relaxed);
        backpressure_events_.store(0, std::memory_order_relaxed);
        peer_unreachable_events_.store(0, std::memory_order_relaxed);
        preview_frames_.store(0, std::memory_order_relaxed);
        preview_drops_.store(0, std::memory_order_relaxed);
        encoder_input_failures_.store(0, std::memory_order_relaxed);
        encoder_output_failures_.store(0, std::memory_order_relaxed);
        encoder_timeouts_.store(0, std::memory_order_relaxed);
        capture_contention_drops_.store(0, std::memory_order_relaxed);
    }

    ScreenShareSnapshot snapshot() const {
        ScreenShareSnapshot result;
        result.state = state_.load(std::memory_order_acquire);
        {
            std::scoped_lock lock(metadata_mutex_);
            result.source_title = source_title_;
            result.error = error_;
        }
        result.source_width =
            source_width_.load(std::memory_order_relaxed);
        result.source_height =
            source_height_.load(std::memory_order_relaxed);
        result.encoded_width =
            encoded_width_.load(std::memory_order_relaxed);
        result.encoded_height =
            encoded_height_.load(std::memory_order_relaxed);
        result.frames_encoded =
            frames_encoded_.load(std::memory_order_relaxed);
        result.frames_sent =
            frames_sent_.load(std::memory_order_relaxed);
        result.frames_dropped =
            frames_dropped_.load(std::memory_order_relaxed);
        result.packets_sent =
            packets_sent_.load(std::memory_order_relaxed);
        result.wire_bytes =
            wire_bytes_.load(std::memory_order_relaxed);
        result.backpressure_events =
            backpressure_events_.load(std::memory_order_relaxed);
        result.peer_unreachable_events =
            peer_unreachable_events_.load(
                std::memory_order_relaxed);
        result.preview_frames =
            preview_frames_.load(std::memory_order_relaxed);
        result.preview_drops =
            preview_drops_.load(std::memory_order_relaxed);
        result.encoder_input_failures =
            encoder_input_failures_.load(
                std::memory_order_relaxed);
        result.encoder_output_failures =
            encoder_output_failures_.load(
                std::memory_order_relaxed);
        result.encoder_timeouts =
            encoder_timeouts_.load(std::memory_order_relaxed);
        result.capture_contention_drops =
            capture_contention_drops_.load(
                std::memory_order_relaxed);
        return result;
    }

    ComPtr<IDXGISwapChain1> preview_swap_chain() const {
        std::scoped_lock lock(preview_mutex_);
        return preview_swap_chain_;
    }

    mutable std::mutex metadata_mutex_;
    mutable std::mutex preview_mutex_;
    std::string source_title_;
    std::string error_;
    ComPtr<IDXGISwapChain1> preview_swap_chain_;

    std::thread worker_;
    std::mutex stop_mutex_;
    std::condition_variable stop_cv_;
    std::atomic_bool stop_requested_{false};
    std::atomic<ScreenShareState> state_{
        ScreenShareState::idle};

    std::atomic<std::uint32_t> source_width_{0};
    std::atomic<std::uint32_t> source_height_{0};
    std::atomic<std::uint32_t> encoded_width_{0};
    std::atomic<std::uint32_t> encoded_height_{0};
    std::atomic<std::uint64_t> frames_encoded_{0};
    std::atomic<std::uint64_t> frames_sent_{0};
    std::atomic<std::uint64_t> frames_dropped_{0};
    std::atomic<std::uint64_t> packets_sent_{0};
    std::atomic<std::uint64_t> wire_bytes_{0};
    std::atomic<std::uint64_t> backpressure_events_{0};
    std::atomic<std::uint64_t> peer_unreachable_events_{0};
    std::atomic<std::uint64_t> preview_frames_{0};
    std::atomic<std::uint64_t> preview_drops_{0};
    std::atomic<std::uint64_t> encoder_input_failures_{0};
    std::atomic<std::uint64_t> encoder_output_failures_{0};
    std::atomic<std::uint64_t> encoder_timeouts_{0};
    std::atomic<std::uint64_t> capture_contention_drops_{0};
};

WindowsScreenShareRuntime::WindowsScreenShareRuntime()
    : impl_(std::make_unique<Impl>()) {}

WindowsScreenShareRuntime::~WindowsScreenShareRuntime() {
    impl_->stop();
}

std::optional<ScreenShareError>
WindowsScreenShareRuntime::start(
    const ScreenShareConfig& config) {
    return impl_->start(config);
}

void WindowsScreenShareRuntime::stop() noexcept {
    impl_->stop();
}

ScreenShareSnapshot
WindowsScreenShareRuntime::snapshot() const {
    return impl_->snapshot();
}

Microsoft::WRL::ComPtr<IDXGISwapChain1>
WindowsScreenShareRuntime::preview_swap_chain() const {
    return impl_->preview_swap_chain();
}

} // namespace catro::screen
