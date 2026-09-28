#include <catro/screen_runtime.hpp>

#include <catro/platform/windows/video_decoder.hpp>
#include <catro/platform/windows/video_encoder.hpp>
#include <catro/platform/windows/video_presenter.hpp>
#include <catro/video/geometry.hpp>
#include <catro/video/rtp_h264.hpp>

#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <iterator>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>

namespace catro::screen {
namespace {

using Microsoft::WRL::ComPtr;
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
using platform::windows::D3D11CompositionVideoPresenter;
using platform::windows::DecodedGpuFrame;
using platform::windows::EncodedAccessUnit;
using platform::windows::GpuCaptureFrame;
using platform::windows::H264DecoderConfig;
using platform::windows::HardwareEncoderConfig;
using platform::windows::VideoPresenterConfig;
using platform::windows::WindowsGraphicsCapture;
using platform::windows::WindowsH264D3D11Decoder;
using platform::windows::WindowsH264HardwareEncoder;
using transport::UdpError;
using transport::UdpErrorCode;
using transport::UdpPeerSocket;

constexpr auto kFirstFrameTimeout = 3s;
constexpr auto kGameFirstFrameTimeout = 30s;
constexpr auto kRemoteInactiveTimeout = 2s;
constexpr auto kReceiveWait = 20ms;
constexpr std::uint32_t kPreviewMaxFps = 10;
constexpr std::size_t kReceiveDatagramBytes = 1500;
constexpr std::size_t kReceiveDrainLimit = 512;

[[nodiscard]] bool valid_media_bounds(
    std::uint8_t payload_type,
    std::uint16_t mtu_bytes,
    std::size_t max_access_unit_bytes) noexcept {
    return payload_type >= 96 &&
           payload_type <= 127 &&
           mtu_bytes >= 576 &&
           mtu_bytes <= 1400 &&
           max_access_unit_bytes >= 262'144 &&
           max_access_unit_bytes <= 16U * 1024U * 1024U;
}

[[nodiscard]] bool valid_direct_endpoints(
    const transport::UdpEndpoint& bind,
    const transport::UdpEndpoint& peer) noexcept {
    return !bind.address.empty() &&
           !peer.address.empty() &&
           bind.port != 0 &&
           peer.port != 0;
}

[[nodiscard]] bool valid_transport(
    const ScreenTransportConfig& config) noexcept {
    return valid_media_bounds(
               config.payload_type,
               config.mtu_bytes,
               config.max_access_unit_bytes) &&
           (config.room_runtime != nullptr ||
            valid_direct_endpoints(
                config.bind, config.peer));
}

[[nodiscard]] ScreenTransportConfig transport_from_share(
    const ScreenShareConfig& config) {
    return ScreenTransportConfig{
        .room_runtime = config.room_runtime,
        .bind = config.bind,
        .peer = config.peer,
        .payload_type = config.payload_type,
        .mtu_bytes = config.mtu_bytes,
        .max_access_unit_bytes = config.max_access_unit_bytes,
    };
}

[[nodiscard]] bool valid_share(
    const ScreenShareConfig& config) noexcept {
    return config.source.native_handle != 0 &&
           valid_media_bounds(
               config.payload_type,
               config.mtu_bytes,
               config.max_access_unit_bytes) &&
           (config.room_runtime != nullptr ||
            valid_direct_endpoints(
                config.bind, config.peer)) &&
           config.max_width >= 320 &&
           config.max_width <= 7680 &&
           config.max_height >= 180 &&
           config.max_height <= 4320 &&
           config.fps >= 1 &&
           config.fps <= 120 &&
           config.bitrate >= 128'000 &&
           config.bitrate <= 50'000'000 &&
           config.ssrc != 0;
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

[[nodiscard]] std::int64_t extended_rtp_to_100ns(
    std::uint64_t timestamp_90khz) noexcept {
    // 10,000,000 / 90,000 = 1000 / 9. The receiver extends the 32-bit RTP clock before this
    // conversion, so a long-lived room can cross the ~13-hour RTP wrap without PTS moving back.
    return static_cast<std::int64_t>(
        (timestamp_90khz * 1000ULL + 4ULL) / 9ULL);
}

[[nodiscard]] std::int64_t steady_now_ns() noexcept {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               Clock::now().time_since_epoch())
        .count();
}

// Lifecycle-only diagnostic breadcrumbs. This never runs on every media frame: it is intentionally
// limited to worker/capture/encoder/presenter transitions so production hot paths stay untouched.
// The file is also useful for native access violations that bypass C++ exception handling.
void trace_event(std::string_view event) noexcept {
    wchar_t path[MAX_PATH + 64]{};
    const auto length = GetTempPathW(MAX_PATH, path);
    if (length == 0 || length >= MAX_PATH) {
        return;
    }
    constexpr wchar_t suffix[] = L"catro-screen-runtime.log";
    if (length + std::size(suffix) >= std::size(path)) {
        return;
    }
    std::copy(std::begin(suffix), std::end(suffix), path + length);

    const auto file = CreateFileW(
        path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }

    SYSTEMTIME now{};
    GetLocalTime(&now);
    char line[512]{};
    const auto written = std::snprintf(
        line, sizeof(line),
        "%04u-%02u-%02u %02u:%02u:%02u.%03u pid=%lu tid=%lu %.*s\r\n",
        static_cast<unsigned>(now.wYear),
        static_cast<unsigned>(now.wMonth),
        static_cast<unsigned>(now.wDay),
        static_cast<unsigned>(now.wHour),
        static_cast<unsigned>(now.wMinute),
        static_cast<unsigned>(now.wSecond),
        static_cast<unsigned>(now.wMilliseconds),
        static_cast<unsigned long>(GetCurrentProcessId()),
        static_cast<unsigned long>(GetCurrentThreadId()),
        static_cast<int>(std::min<std::size_t>(event.size(), 300U)),
        event.data());
    if (written > 0) {
        DWORD bytes_written = 0;
        (void)WriteFile(
            file,
            line,
            static_cast<DWORD>(std::min<int>(
                written, static_cast<int>(sizeof(line) - 1))),
            &bytes_written,
            nullptr);
    }
    CloseHandle(file);
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
            if (failure.code == UdpErrorCode::peer_unreachable) {
                context.owner->peer_unreachable_events_.fetch_add(
                    1, std::memory_order_relaxed);
                context.soft_drop = true;
                return false;
            }

            context.fatal_error = failure;
            return false;
        }
    };

    [[nodiscard]] std::optional<ScreenShareError> start_listening(
        const ScreenTransportConfig& config) {
        std::scoped_lock lifecycle_lock(lifecycle_mutex_);

        if (!valid_transport(config)) {
            return ScreenShareError{
                ScreenShareErrorCode::invalid_config,
                "invalid screen transport configuration"};
        }

        if (socket_ &&
            receiver_worker_.joinable() &&
            !stop_requested_.load(std::memory_order_acquire) &&
            transport_config_ &&
            *transport_config_ == config) {
            return std::nullopt;
        }

        stop_locked();
        return start_transport_locked(config);
    }

    [[nodiscard]] std::optional<ScreenShareError> start(
        const ScreenShareConfig& config) {
        std::scoped_lock lifecycle_lock(lifecycle_mutex_);

        if (!valid_share(config)) {
            return ScreenShareError{
                ScreenShareErrorCode::invalid_config,
                "invalid screen-share configuration"};
        }

        ScreenTransportConfig transport;
        try {
            transport = transport_from_share(config);
        } catch (...) {
            return ScreenShareError{
                ScreenShareErrorCode::memory_failed,
                "screen transport configuration allocation failed"};
        }

        const bool reuse_transport =
            (socket_ || transport.room_runtime != nullptr) &&
            receiver_worker_.joinable() &&
            !stop_requested_.load(std::memory_order_acquire) &&
            transport_config_ &&
            *transport_config_ == transport &&
            state_.load(std::memory_order_acquire) ==
                ScreenShareState::listening;

        if (!reuse_transport) {
            stop_locked();
            if (const auto failure =
                    start_transport_locked(transport)) {
                return failure;
            }
        }

        stop_sharing_locked();
        reset_local_statistics();
        {
            std::scoped_lock lock(metadata_mutex_);
            source_title_ = config.source.title;
            error_.clear();
        }

        share_stop_requested_.store(
            false, std::memory_order_release);
        state_.store(
            ScreenShareState::starting,
            std::memory_order_release);

        try {
            sender_worker_ = std::thread(
                [this, config] { run_sender_guarded(config); });
        } catch (...) {
            share_stop_requested_.store(
                true, std::memory_order_release);
            state_.store(
                ScreenShareState::failed,
                std::memory_order_release);
            return ScreenShareError{
                ScreenShareErrorCode::worker_start_failed,
                "screen-share worker could not start"};
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<ScreenShareError>
    start_transport_locked(const ScreenTransportConfig& config) {
        reset_all_statistics();
        {
            std::scoped_lock lock(metadata_mutex_);
            source_title_.clear();
            error_.clear();
        }

        if (config.room_runtime != nullptr) {
            const auto room =
                catro_room_runtime_snapshot(
                    config.room_runtime);
            if (room.state == CATRO_ROOM_FAILED ||
                room.state == CATRO_ROOM_IDLE) {
                state_.store(
                    ScreenShareState::failed,
                    std::memory_order_release);
                return ScreenShareError{
                    ScreenShareErrorCode::network_failed,
                    room.error[0] != '\0'
                        ? room.error
                        : "RTC room transport is not connected"};
            }

            socket_.reset();
            transport_config_ = config;
            stop_requested_.store(
                false, std::memory_order_release);
            share_stop_requested_.store(
                true, std::memory_order_release);
            state_.store(
                ScreenShareState::listening,
                std::memory_order_release);

            try {
                receiver_worker_ = std::thread(
                    [this, config] {
                        run_receiver_guarded(config);
                    });
            } catch (...) {
                transport_config_.reset();
                state_.store(
                    ScreenShareState::failed,
                    std::memory_order_release);
                return ScreenShareError{
                    ScreenShareErrorCode::worker_start_failed,
                    "screen receive worker could not start"};
            }
            return std::nullopt;
        }

        auto opened = UdpPeerSocket::bind(config.bind);
        if (const auto* error = std::get_if<UdpError>(&opened)) {
            state_.store(
                ScreenShareState::failed,
                std::memory_order_release);
            return ScreenShareError{
                ScreenShareErrorCode::network_failed,
                udp_error_text(*error),
                error->native_code};
        }

        socket_ = std::move(
            std::get<std::unique_ptr<UdpPeerSocket>>(opened));
        const auto connected = socket_->connect_peer(config.peer);
        if (const auto* error =
                std::get_if<UdpError>(&connected)) {
            socket_.reset();
            state_.store(
                ScreenShareState::failed,
                std::memory_order_release);
            return ScreenShareError{
                ScreenShareErrorCode::network_failed,
                udp_error_text(*error),
                error->native_code};
        }

        transport_config_ = config;
        stop_requested_.store(false, std::memory_order_release);
        share_stop_requested_.store(true, std::memory_order_release);
        state_.store(
            ScreenShareState::listening,
            std::memory_order_release);

        try {
            receiver_worker_ = std::thread(
                [this, config] { run_receiver_guarded(config); });
        } catch (...) {
            socket_.reset();
            transport_config_.reset();
            state_.store(
                ScreenShareState::failed,
                std::memory_order_release);
            return ScreenShareError{
                ScreenShareErrorCode::worker_start_failed,
                "screen receive worker could not start"};
        }
        return std::nullopt;
    }

    void stop_sharing() noexcept {
        std::scoped_lock lifecycle_lock(lifecycle_mutex_);
        stop_sharing_locked();
    }

    void set_local_preview_enabled(bool enabled) noexcept {
        // The sender samples this on its normal frame cadence. No lifecycle lock or cross-thread
        // wakeup is needed, so hiding the page cannot perturb encode/transport scheduling.
        local_preview_enabled_.store(enabled, std::memory_order_release);
    }

    void set_remote_viewing_enabled(bool enabled) noexcept {
        remote_viewing_enabled_.store(enabled, std::memory_order_release);
        if (!enabled) {
            // UI should detach immediately; the receive worker releases decoder/presenter GPU
            // resources on its next <=20 ms receive-loop iteration.
            remote_last_frame_ns_.store(0, std::memory_order_release);
            remote_width_.store(0, std::memory_order_relaxed);
            remote_height_.store(0, std::memory_order_relaxed);
            std::scoped_lock lock(preview_mutex_);
            remote_swap_chain_.Reset();
        }
    }

    void stop_sharing_locked() noexcept {
        share_stop_requested_.store(
            true, std::memory_order_release);
        stop_cv_.notify_all();

        if (sender_worker_.joinable()) {
            sender_worker_.join();
        }

        {
            std::scoped_lock lock(preview_mutex_);
            preview_swap_chain_.Reset();
        }

        const auto state =
            state_.load(std::memory_order_acquire);
        if (socket_ &&
            receiver_worker_.joinable() &&
            state != ScreenShareState::failed) {
            state_.store(
                ScreenShareState::listening,
                std::memory_order_release);
        }
    }

    void stop() noexcept {
        std::scoped_lock lifecycle_lock(lifecycle_mutex_);
        stop_locked();
    }

    void stop_locked() noexcept {
        share_stop_requested_.store(
            true, std::memory_order_release);
        stop_requested_.store(
            true, std::memory_order_release);
        stop_cv_.notify_all();

        if (sender_worker_.joinable()) {
            sender_worker_.join();
        }
        if (receiver_worker_.joinable()) {
            receiver_worker_.join();
        }

        socket_.reset();
        transport_config_.reset();

        {
            std::scoped_lock lock(preview_mutex_);
            preview_swap_chain_.Reset();
            remote_swap_chain_.Reset();
        }

        state_.store(
            ScreenShareState::idle,
            std::memory_order_release);
        share_stop_requested_.store(
            true, std::memory_order_release);
        stop_requested_.store(
            false, std::memory_order_release);
    }

    [[nodiscard]] bool should_stop_sender() const noexcept {
        return stop_requested_.load(std::memory_order_acquire) ||
               share_stop_requested_.load(std::memory_order_acquire);
    }

    void run_sender_guarded(ScreenShareConfig config) noexcept {
        trace_event("sender-worker-enter");
        try {
            run_sender(std::move(config));
            trace_event("sender-worker-exit");
        } catch (const std::exception& error) {
            trace_event("sender-worker-cxx-exception");
            fail_share(
                ScreenShareErrorCode::worker_start_failed,
                error.what() != nullptr
                    ? std::string_view{error.what()}
                    : std::string_view{"screen sender worker exception"});
        } catch (...) {
            trace_event("sender-worker-unknown-exception");
            fail_share(
                ScreenShareErrorCode::worker_start_failed,
                "screen sender worker exception");
        }
    }

    void run_sender(ScreenShareConfig config) {
        auto* const socket = socket_.get();
        if (socket == nullptr) {
            fail_share(
                ScreenShareErrorCode::network_failed,
                "video transport is not running");
            return;
        }

        trace_event("sender-capture-configure");
        WindowsGraphicsCapture capture;
        platform::windows::ScreenCaptureConfig capture_config;
        capture_config.backend =
            platform::windows::recommended_capture_backend(config.source);
        capture_config.borderless =
            config.borderless &&
            capture_config.backend ==
                platform::windows::ScreenCaptureBackend::windows_graphics_capture;
        if (const auto error =
                capture.start_source(config.source, capture_config)) {
            fail_share(
                ScreenShareErrorCode::capture_failed,
                platform::windows::name(error->code),
                error->native_code);
            return;
        }
        trace_event(
            capture_config.backend ==
                    platform::windows::ScreenCaptureBackend::desktop_duplication
                ? "sender-capture-started-dxgi"
                : "sender-capture-started-wgc");

        GpuCaptureFrame first;
        const bool wait_for_game_restore =
            capture_config.backend ==
                platform::windows::ScreenCaptureBackend::desktop_duplication &&
            config.source.kind ==
                platform::windows::CaptureSourceKind::window;
        const auto first_deadline =
            Clock::now() +
            (wait_for_game_restore
                 ? kGameFirstFrameTimeout
                 : kFirstFrameTimeout);
        while (!should_stop_sender() &&
               Clock::now() < first_deadline &&
               !capture.wait_for_latest(first, 50ms)) {
            const auto stats = capture.statistics();
            if (stats.error) {
                fail_share(
                    ScreenShareErrorCode::capture_failed,
                    platform::windows::name(
                        stats.error->code),
                    stats.error->native_code);
                capture.stop();
                return;
            }
        }

        if (should_stop_sender()) {
            capture.stop();
            return;
        }
        if (!first.texture) {
            const auto backend_name =
                capture_config.backend ==
                        platform::windows::ScreenCaptureBackend::desktop_duplication
                    ? "DXGI Desktop Duplication"
                    : "Windows Graphics Capture";
            std::string message{backend_name};
            message += wait_for_game_restore
                           ? " did not receive the selected game after waiting for it to be restored"
                           : " did not produce a GPU frame for the selected source";
            fail_share(ScreenShareErrorCode::capture_failed, std::move(message));
            capture.stop();
            return;
        }
        trace_event("sender-first-frame-ready");

        WindowsH264HardwareEncoder encoder;
        D3D11CompositionVideoPresenter presenter(
            VideoPresenterConfig{
                .max_width = 640,
                .max_height = 360,
                .frame_rate =
                    std::min(config.fps, kPreviewMaxFps),
            });

        EncodedAccessUnit access_unit;
        try {
            access_unit.bytes.reserve(
                config.max_access_unit_bytes);
        } catch (...) {
            fail_share(
                ScreenShareErrorCode::memory_failed,
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
        bool preview_resources_live = false;

        const auto configure_encoder =
            [&](const GpuCaptureFrame& frame) -> bool {
            const auto extent = video::fit_even_video_extent(
                frame.width,
                frame.height,
                config.max_width,
                config.max_height);
            if (!extent) {
                fail_share(
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
                fail_share(
                    ScreenShareErrorCode::encoder_failed,
                    platform::windows::name(error->code),
                    error->native_code);
                return false;
            }

            if (current_source_width == 0 && current_source_height == 0) {
                trace_event("sender-encoder-started");
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

            if (!local_preview_enabled_.load(
                    std::memory_order_acquire)) {
                preview_accumulator = 0;
                if (preview_resources_live) {
                    presenter.reset();
                    {
                        std::scoped_lock lock(preview_mutex_);
                        preview_swap_chain_.Reset();
                    }
                    preview_resources_live = false;
                }
            } else {
                preview_accumulator +=
                    std::min(config.fps, kPreviewMaxFps);
                if (preview_accumulator >= config.fps) {
                    preview_accumulator -= config.fps;
                    if (!preview_resources_live) {
                        trace_event("sender-local-preview-first-present");
                    }
                    bool preview_ok = true;
                    try {
                        if (const auto error =
                                presenter.present(
                                    *frame.texture.Get())) {
                            trace_event("sender-local-preview-disabled-error");
                            preview_ok = false;
                        }
                    } catch (...) {
                        trace_event("sender-local-preview-disabled-exception");
                        preview_ok = false;
                    }
                    if (!preview_ok) {
                        local_preview_enabled_.store(
                            false, std::memory_order_release);
                        presenter.reset();
                        {
                            std::scoped_lock lock(preview_mutex_);
                            preview_swap_chain_.Reset();
                        }
                        preview_resources_live = false;
                    } else {
                        if (!preview_resources_live) {
                            trace_event("sender-local-preview-ready");
                        }
                        preview_resources_live = true;
                    }
                    if (preview_ok) {
                        const auto preview_stats =
                            presenter.statistics();
                        preview_frames_.store(
                            preview_stats.frames_presented,
                            std::memory_order_relaxed);
                        preview_drops_.store(
                            preview_stats.frames_dropped,
                            std::memory_order_relaxed);
                        publish_preview_swap_chain();
                    }
                }
            }

            if (const auto error =
                    encoder.encode(frame, access_unit)) {
                fail_share(
                    ScreenShareErrorCode::encoder_failed,
                    platform::windows::name(error->code),
                    error->native_code);
                return false;
            }
            const auto encoded_before =
                frames_encoded_.fetch_add(
                    1, std::memory_order_relaxed);
            if (encoded_before == 0) {
                trace_event("sender-first-frame-encoded");
            }

            PacketContext context{
                .socket = socket,
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
                    fail_session(
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
                fail_share(
                    ScreenShareErrorCode::packetization_failed,
                    "H.264 access unit is not RFC 6184 packetizable");
                return false;
            }

            const auto sent_before =
                frames_sent_.fetch_add(
                    1, std::memory_order_relaxed);
            if (sent_before == 0) {
                trace_event("sender-first-frame-sent");
            }
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

        while (!should_stop_sender()) {
            {
                std::unique_lock stop_lock(stop_mutex_);
                stop_cv_.wait_until(
                    stop_lock,
                    next_frame,
                    [this] {
                        return should_stop_sender();
                    });
            }
            if (should_stop_sender()) {
                break;
            }

            const auto now = Clock::now();
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
                fail_share(
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

        {
            std::scoped_lock lock(preview_mutex_);
            preview_swap_chain_.Reset();
        }

        if (state_.load(std::memory_order_acquire) !=
                ScreenShareState::failed &&
            !stop_requested_.load(
                std::memory_order_acquire)) {
            state_.store(
                ScreenShareState::listening,
                std::memory_order_release);
        }
    }

    void run_receiver_guarded(ScreenTransportConfig config) noexcept {
        trace_event("receiver-worker-enter");
        try {
            run_receiver(std::move(config));
            trace_event("receiver-worker-exit");
        } catch (const std::exception& error) {
            trace_event("receiver-worker-cxx-exception");
            fail_session(
                ScreenShareErrorCode::worker_start_failed,
                error.what() != nullptr
                    ? std::string_view{error.what()}
                    : std::string_view{"screen receiver worker exception"});
        } catch (...) {
            trace_event("receiver-worker-unknown-exception");
            fail_session(
                ScreenShareErrorCode::worker_start_failed,
                "screen receiver worker exception");
        }
    }

    void run_receiver(ScreenTransportConfig config) {
        auto* const socket = socket_.get();
        if (socket == nullptr) {
            fail_session(
                ScreenShareErrorCode::network_failed,
                "video transport is not running");
            return;
        }

        std::unique_ptr<std::byte[]> frame_memory(
            new (std::nothrow)
                std::byte[config.max_access_unit_bytes]);
        if (!frame_memory) {
            fail_session(
                ScreenShareErrorCode::memory_failed,
                "remote H.264 frame buffer allocation failed");
            return;
        }

        const video::H264RtpConfig receive_rtp{
            0,
            config.payload_type,
            config.mtu_bytes,
        };
        video::H264RtpReassembler reassembler(
            std::span<std::byte>(
                frame_memory.get(),
                config.max_access_unit_bytes),
            receive_rtp);

        WindowsH264D3D11Decoder decoder;
        H264DecoderConfig decoder_config;
        decoder_config.max_access_unit_bytes =
            config.max_access_unit_bytes;

        D3D11CompositionVideoPresenter presenter(
            VideoPresenterConfig{
                .max_width = 1920,
                .max_height = 1080,
                .frame_rate = 60,
            });

        std::array<std::byte, kReceiveDatagramBytes>
            datagram{};
        bool have_timestamp = false;
        bool first_remote_frame_traced = false;
        bool first_remote_present_traced = false;
        bool viewing_last = false;
        bool awaiting_keyframe = true;
        std::uint32_t last_timestamp = 0;
        std::uint64_t extended_timestamp = 0;

        const auto release_viewer_resources = [&] {
            decoder.stop();
            presenter.reset();
            {
                std::scoped_lock lock(preview_mutex_);
                remote_swap_chain_.Reset();
            }
            remote_width_.store(0, std::memory_order_relaxed);
            remote_height_.store(0, std::memory_order_relaxed);
            remote_last_frame_ns_.store(0, std::memory_order_release);
            have_timestamp = false;
            last_timestamp = 0;
            extended_timestamp = 0;
            awaiting_keyframe = true;
            first_remote_present_traced = false;
        };

        const auto synchronize_viewing_state = [&] {
            auto requested =
                remote_viewing_enabled_.load(std::memory_order_acquire);

            const auto last_stream =
                remote_last_stream_ns_.load(std::memory_order_acquire);
            if (requested && last_stream != 0) {
                const auto now = steady_now_ns();
                const auto timeout =
                    std::chrono::duration_cast<std::chrono::nanoseconds>(
                        kRemoteInactiveTimeout)
                        .count();
                if (now >= last_stream &&
                    now - last_stream >= timeout) {
                    remote_viewing_enabled_.store(
                        false, std::memory_order_release);
                    requested = false;
                }
            }

            if (requested != viewing_last) {
                if (!requested) {
                    trace_event("receiver-viewing-stopped");
                    release_viewer_resources();
                } else {
                    trace_event("receiver-viewing-requested");
                    awaiting_keyframe = true;
                    have_timestamp = false;
                    last_timestamp = 0;
                    extended_timestamp = 0;
                }
                viewing_last = requested;
            }
            return requested;
        };

        while (!stop_requested_.load(
                   std::memory_order_acquire)) {
            (void)synchronize_viewing_state();

            const auto ready =
                socket->wait_readable(
                    std::chrono::duration_cast<
                        std::chrono::microseconds>(
                        kReceiveWait));
            if (const auto* failure =
                    std::get_if<UdpError>(&ready)) {
                if (failure->code ==
                    UdpErrorCode::peer_unreachable) {
                    peer_unreachable_events_.fetch_add(
                        1, std::memory_order_relaxed);
                    continue;
                }
                fail_session(
                    ScreenShareErrorCode::network_failed,
                    udp_error_text(*failure));
                break;
            }

            if (!std::get<bool>(ready)) {
                continue;
            }

            bool fatal = false;
            for (std::size_t drained = 0;
                 drained < kReceiveDrainLimit;
                 ++drained) {
                const auto received =
                    socket->receive(datagram);
                if (const auto* failure =
                        std::get_if<UdpError>(&received)) {
                    if (failure->code ==
                        UdpErrorCode::peer_unreachable) {
                        peer_unreachable_events_.fetch_add(
                            1, std::memory_order_relaxed);
                        break;
                    }
                    if (failure->code ==
                        UdpErrorCode::datagram_too_large) {
                        remote_packet_rejects_.fetch_add(
                            1, std::memory_order_relaxed);
                        continue;
                    }
                    fail_session(
                        ScreenShareErrorCode::network_failed,
                        udp_error_text(*failure));
                    fatal = true;
                    break;
                }

                const auto size =
                    std::get<std::size_t>(received);
                if (size == 0) {
                    break;
                }

                remote_packets_.fetch_add(
                    1, std::memory_order_relaxed);
                remote_wire_bytes_.fetch_add(
                    size, std::memory_order_relaxed);

                auto reassembled =
                    reassembler.push(
                        std::span<const std::byte>(
                            datagram.data(), size));

                if (reassembled.status ==
                        video::H264ReassemblyStatus::packet_rejected &&
                    reassembled.error ==
                        video::H264ReassemblyError::ssrc_mismatch) {
                    const auto last =
                        remote_last_stream_ns_.load(
                            std::memory_order_acquire);
                    const auto now = steady_now_ns();
                    const auto timeout =
                        std::chrono::duration_cast<std::chrono::nanoseconds>(
                            kRemoteInactiveTimeout)
                            .count();
                    if (last != 0 &&
                        now >= last &&
                        now - last >= timeout) {
                        // A quiet peer may have restarted and chosen a new SSRC. Reset stream and
                        // viewer history only after the old sender is inactive; an alien packet
                        // cannot steal an active session.
                        reassembler.reset();
                        release_viewer_resources();
                        remote_last_stream_ns_.store(
                            0, std::memory_order_release);
                        remote_stream_resets_.fetch_add(
                            1, std::memory_order_relaxed);
                        reassembled =
                            reassembler.push(
                                std::span<const std::byte>(
                                    datagram.data(), size));
                    }
                }

                if (reassembled.status ==
                    video::H264ReassemblyStatus::packet_rejected) {
                    remote_packet_rejects_.fetch_add(
                        1, std::memory_order_relaxed);
                    continue;
                }
                if (reassembled.status ==
                    video::H264ReassemblyStatus::frame_dropped) {
                    remote_frame_drops_.fetch_add(
                        1, std::memory_order_relaxed);
                    continue;
                }
                if (reassembled.status !=
                    video::H264ReassemblyStatus::frame_ready) {
                    continue;
                }

                remote_frames_.fetch_add(
                    1, std::memory_order_relaxed);
                remote_last_stream_ns_.store(
                    steady_now_ns(), std::memory_order_release);
                if (!first_remote_frame_traced) {
                    trace_event("receiver-first-frame-reassembled");
                    first_remote_frame_traced = true;
                }

                const bool viewing = synchronize_viewing_state();
                if (!viewing) {
                    continue;
                }

                if (awaiting_keyframe) {
                    if (!reassembled.frame.keyframe) {
                        continue;
                    }
                    if (const auto failure =
                            decoder.start(decoder_config)) {
                        remote_decode_failures_.fetch_add(
                            1, std::memory_order_relaxed);
                        fail_session(
                            ScreenShareErrorCode::decoder_failed,
                            platform::windows::name(failure->code),
                            failure->native_code);
                        fatal = true;
                        break;
                    }
                    trace_event("receiver-decoder-started");
                    awaiting_keyframe = false;
                    have_timestamp = false;
                    last_timestamp = 0;
                    extended_timestamp = 0;
                }

                const auto timestamp =
                    reassembled.frame.timestamp_90khz;
                if (!have_timestamp) {
                    extended_timestamp = timestamp;
                    last_timestamp = timestamp;
                    have_timestamp = true;
                } else {
                    extended_timestamp +=
                        static_cast<std::uint32_t>(
                            timestamp - last_timestamp);
                    last_timestamp = timestamp;
                }

                DecodedGpuFrame decoded;
                if (const auto failure =
                        decoder.decode(
                            reassembled.frame.annex_b,
                            extended_rtp_to_100ns(
                                extended_timestamp),
                            decoded)) {
                    remote_decode_failures_.fetch_add(
                        1, std::memory_order_relaxed);
                    fail_session(
                        ScreenShareErrorCode::decoder_failed,
                        platform::windows::name(
                            failure->code),
                        failure->native_code);
                    fatal = true;
                    break;
                }

                if (!decoded.texture) {
                    continue;
                }

                const auto decoded_before =
                    remote_decoded_.fetch_add(
                        1, std::memory_order_relaxed);
                if (decoded_before == 0) {
                    trace_event("receiver-first-frame-decoded");
                }
                remote_width_.store(
                    decoded.width, std::memory_order_relaxed);
                remote_height_.store(
                    decoded.height, std::memory_order_relaxed);

                if (!first_remote_present_traced) {
                    trace_event("receiver-first-frame-present-begin");
                }
                if (const auto failure =
                        presenter.present(
                            *decoded.texture.Get(),
                            decoded.subresource_index)) {
                    trace_event("receiver-present-error");
                    fail_session(
                        ScreenShareErrorCode::remote_present_failed,
                        platform::windows::name(
                            failure->code),
                        failure->native_code);
                    fatal = true;
                    break;
                }
                if (!first_remote_present_traced) {
                    trace_event("receiver-first-frame-presented");
                    first_remote_present_traced = true;
                }

                const auto presentation =
                    presenter.statistics();
                remote_presented_.store(
                    presentation.frames_presented,
                    std::memory_order_relaxed);
                remote_present_drops_.store(
                    presentation.frames_dropped,
                    std::memory_order_relaxed);
                remote_last_frame_ns_.store(
                    steady_now_ns(),
                    std::memory_order_release);

                const auto swap_chain =
                    presenter.swap_chain();
                if (swap_chain) {
                    std::scoped_lock lock(preview_mutex_);
                    if (remote_swap_chain_.Get() !=
                        swap_chain.Get()) {
                        remote_swap_chain_ = swap_chain;
                    }
                }
            }

            if (fatal) {
                break;
            }
        }

        release_viewer_resources();
    }

    void fail_share(
        ScreenShareErrorCode,
        std::string_view message,
        std::int64_t native_code = 0) noexcept {
        set_error(message, native_code);
        share_stop_requested_.store(
            true, std::memory_order_release);
        stop_cv_.notify_all();
        state_.store(
            ScreenShareState::failed,
            std::memory_order_release);
    }

    void fail_session(
        ScreenShareErrorCode,
        std::string_view message,
        std::int64_t native_code = 0) noexcept {
        set_error(message, native_code);
        share_stop_requested_.store(
            true, std::memory_order_release);
        stop_requested_.store(
            true, std::memory_order_release);
        stop_cv_.notify_all();
        state_.store(
            ScreenShareState::failed,
            std::memory_order_release);
    }

    void set_error(
        std::string_view message,
        std::int64_t native_code) noexcept {
        try {
            std::string owned{message};
            if (native_code != 0) {
                owned += " (native ";
                owned += std::to_string(native_code);
                owned += ")";
            }
            std::scoped_lock lock(metadata_mutex_);
            error_ = std::move(owned);
        } catch (...) {
        }
    }

    void reset_local_statistics() noexcept {
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
        preview_frames_.store(0, std::memory_order_relaxed);
        preview_drops_.store(0, std::memory_order_relaxed);
        encoder_input_failures_.store(0, std::memory_order_relaxed);
        encoder_output_failures_.store(0, std::memory_order_relaxed);
        encoder_timeouts_.store(0, std::memory_order_relaxed);
        capture_contention_drops_.store(0, std::memory_order_relaxed);
    }

    void reset_remote_statistics() noexcept {
        remote_viewing_enabled_.store(
            false, std::memory_order_relaxed);
        remote_last_stream_ns_.store(
            0, std::memory_order_relaxed);
        remote_width_.store(0, std::memory_order_relaxed);
        remote_height_.store(0, std::memory_order_relaxed);
        remote_packets_.store(0, std::memory_order_relaxed);
        remote_wire_bytes_.store(0, std::memory_order_relaxed);
        remote_frames_.store(0, std::memory_order_relaxed);
        remote_decoded_.store(0, std::memory_order_relaxed);
        remote_presented_.store(0, std::memory_order_relaxed);
        remote_frame_drops_.store(0, std::memory_order_relaxed);
        remote_packet_rejects_.store(0, std::memory_order_relaxed);
        remote_decode_failures_.store(0, std::memory_order_relaxed);
        remote_present_drops_.store(0, std::memory_order_relaxed);
        remote_stream_resets_.store(0, std::memory_order_relaxed);
        remote_last_frame_ns_.store(0, std::memory_order_relaxed);
    }

    void reset_all_statistics() noexcept {
        reset_local_statistics();
        reset_remote_statistics();
        peer_unreachable_events_.store(
            0, std::memory_order_relaxed);
    }

    ScreenShareSnapshot snapshot() const {
        ScreenShareSnapshot result;
        result.state =
            state_.load(std::memory_order_acquire);
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

        result.remote_viewing =
            remote_viewing_enabled_.load(
                std::memory_order_acquire);
        const auto stream_last =
            remote_last_stream_ns_.load(
                std::memory_order_acquire);
        if (stream_last != 0) {
            const auto age =
                steady_now_ns() - stream_last;
            result.remote_available =
                age >= 0 &&
                age < std::chrono::duration_cast<
                          std::chrono::nanoseconds>(
                          kRemoteInactiveTimeout)
                          .count();
        }

        result.remote_width =
            remote_width_.load(std::memory_order_relaxed);
        result.remote_height =
            remote_height_.load(std::memory_order_relaxed);
        result.remote_packets =
            remote_packets_.load(std::memory_order_relaxed);
        result.remote_wire_bytes =
            remote_wire_bytes_.load(std::memory_order_relaxed);
        result.remote_frames =
            remote_frames_.load(std::memory_order_relaxed);
        result.remote_decoded =
            remote_decoded_.load(std::memory_order_relaxed);
        result.remote_presented =
            remote_presented_.load(std::memory_order_relaxed);
        result.remote_frame_drops =
            remote_frame_drops_.load(std::memory_order_relaxed);
        result.remote_packet_rejects =
            remote_packet_rejects_.load(std::memory_order_relaxed);
        result.remote_decode_failures =
            remote_decode_failures_.load(std::memory_order_relaxed);
        result.remote_present_drops =
            remote_present_drops_.load(std::memory_order_relaxed);
        result.remote_stream_resets =
            remote_stream_resets_.load(std::memory_order_relaxed);

        const auto last =
            remote_last_frame_ns_.load(
                std::memory_order_acquire);
        if (result.remote_viewing && last != 0) {
            const auto age =
                steady_now_ns() - last;
            result.remote_active =
                age >= 0 &&
                age < std::chrono::duration_cast<
                          std::chrono::nanoseconds>(
                          kRemoteInactiveTimeout)
                          .count();
        }
        return result;
    }

    ComPtr<IDXGISwapChain1>
    preview_swap_chain() const {
        std::scoped_lock lock(preview_mutex_);
        return preview_swap_chain_;
    }

    ComPtr<IDXGISwapChain1>
    remote_swap_chain() const {
        std::scoped_lock lock(preview_mutex_);
        return remote_swap_chain_;
    }

    mutable std::mutex lifecycle_mutex_;
    mutable std::mutex metadata_mutex_;
    mutable std::mutex preview_mutex_;
    std::mutex stop_mutex_;
    std::condition_variable stop_cv_;

    std::string source_title_;
    std::string error_;

    std::unique_ptr<UdpPeerSocket> socket_;
    std::optional<ScreenTransportConfig> transport_config_;
    ComPtr<IDXGISwapChain1> preview_swap_chain_;
    ComPtr<IDXGISwapChain1> remote_swap_chain_;

    std::thread sender_worker_;
    std::thread receiver_worker_;
    std::atomic_bool stop_requested_{false};
    std::atomic_bool share_stop_requested_{true};
    std::atomic_bool local_preview_enabled_{true};
    std::atomic_bool remote_viewing_enabled_{false};
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

    std::atomic<std::uint32_t> remote_width_{0};
    std::atomic<std::uint32_t> remote_height_{0};
    std::atomic<std::uint64_t> remote_packets_{0};
    std::atomic<std::uint64_t> remote_wire_bytes_{0};
    std::atomic<std::uint64_t> remote_frames_{0};
    std::atomic<std::uint64_t> remote_decoded_{0};
    std::atomic<std::uint64_t> remote_presented_{0};
    std::atomic<std::uint64_t> remote_frame_drops_{0};
    std::atomic<std::uint64_t> remote_packet_rejects_{0};
    std::atomic<std::uint64_t> remote_decode_failures_{0};
    std::atomic<std::uint64_t> remote_present_drops_{0};
    std::atomic<std::uint64_t> remote_stream_resets_{0};
    std::atomic<std::int64_t> remote_last_stream_ns_{0};
    std::atomic<std::int64_t> remote_last_frame_ns_{0};
};

WindowsScreenShareRuntime::WindowsScreenShareRuntime()
    : impl_(std::make_unique<Impl>()) {}

WindowsScreenShareRuntime::~WindowsScreenShareRuntime() {
    impl_->stop();
}

std::optional<ScreenShareError>
WindowsScreenShareRuntime::start_listening(
    const ScreenTransportConfig& config) {
    return impl_->start_listening(config);
}

std::optional<ScreenShareError>
WindowsScreenShareRuntime::start(
    const ScreenShareConfig& config) {
    return impl_->start(config);
}

void WindowsScreenShareRuntime::stop_sharing() noexcept {
    impl_->stop_sharing();
}

void WindowsScreenShareRuntime::set_local_preview_enabled(
    bool enabled) noexcept {
    impl_->set_local_preview_enabled(enabled);
}

void WindowsScreenShareRuntime::set_remote_viewing_enabled(
    bool enabled) noexcept {
    impl_->set_remote_viewing_enabled(enabled);
}

void WindowsScreenShareRuntime::stop() noexcept {
    impl_->stop();
}

ScreenShareSnapshot
WindowsScreenShareRuntime::snapshot() const {
    return impl_->snapshot();
}

ComPtr<IDXGISwapChain1>
WindowsScreenShareRuntime::preview_swap_chain() const {
    return impl_->preview_swap_chain();
}

ComPtr<IDXGISwapChain1>
WindowsScreenShareRuntime::remote_swap_chain() const {
    return impl_->remote_swap_chain();
}

} // namespace catro::screen
