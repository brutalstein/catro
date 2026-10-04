#include <catro/screen_runtime.hpp>

#include <catro/audio/realtime.hpp>
#include <catro/platform/windows/process_loopback_audio.hpp>
#include <catro/platform/windows/video_decoder.hpp>
#include <catro/platform/windows/video_encoder.hpp>
#include <catro/platform/windows/video_presenter.hpp>
#include <catro/video/geometry.hpp>
#include <catro/video/rtp_h264.hpp>
#include <catro/voice/codec.hpp>
#include <catro/voice/jitter.hpp>
#include <catro/voice/packet.hpp>

#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
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
constexpr std::uint32_t kPreviewMaxFps = 10;

[[nodiscard]] RoomScreenApi room_api() noexcept {
    return RoomScreenApi{
        .snapshot = &catro_room_runtime_snapshot,
        .send_video = &catro_room_runtime_send_video,
        .receive_video = &catro_room_runtime_receive_video,
        .send_stream_audio = &catro_room_runtime_send_stream_audio,
        .receive_stream_audio = &catro_room_runtime_receive_stream_audio,
        .request_keyframe = &catro_room_runtime_request_keyframe,
        .keyframe_requests = &catro_room_runtime_keyframe_requests,
    };
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
           config.stream_audio_bitrate >= 32'000 &&
           config.stream_audio_bitrate <= 512'000 &&
           config.ssrc != 0;
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

class WasapiStreamOutput final : public StreamAudioOutput {
public:
    [[nodiscard]] bool start(StreamAudioRenderBridge& bridge) override {
        return !renderer_.start(
            [&bridge](std::span<float> samples) noexcept {
                bridge.on_render(samples);
            });
    }

    void stop() noexcept override {
        renderer_.stop();
    }

private:
    platform::windows::WasapiStreamAudioRenderer renderer_;
};

} // namespace

struct WindowsScreenShareRuntime::Impl {
    // D3D11 decode/presentation edge of the shared receive loop.
    class RemoteViewer final : public RemoteVideoViewer {
    public:
        RemoteViewer(Impl& owner, std::size_t max_access_unit_bytes)
            : owner_(owner),
              // Up to the largest share a sender offers (a 4K Source), so a full-screen viewer
              // sees every pixel of a 1440p stream. The presenter never upscales.
              presenter_(VideoPresenterConfig{
                  .max_width = 3840,
                  .max_height = 2160,
                  .frame_rate = 60,
              }) {
            decoder_config_.max_access_unit_bytes =
                max_access_unit_bytes;
        }

        void release() noexcept override {
            decoder_.stop();
            presenter_.reset();
            {
                std::scoped_lock lock(owner_.preview_mutex_);
                owner_.remote_swap_chain_.Reset();
            }
            first_present_traced_ = false;
        }

        [[nodiscard]] std::optional<ScreenShareError>
        start_decoder() override {
            if (const auto failure =
                    decoder_.start(decoder_config_)) {
                return ScreenShareError{
                    ScreenShareErrorCode::decoder_failed,
                    std::string{platform::windows::name(failure->code)},
                    failure->native_code};
            }
            return std::nullopt;
        }

        [[nodiscard]] std::optional<ScreenShareError>
        decode_and_present(
            std::span<const std::byte> annex_b,
            std::int64_t pts_100ns) override {
            DecodedGpuFrame decoded;
            if (const auto failure =
                    decoder_.decode(annex_b, pts_100ns, decoded)) {
                return ScreenShareError{
                    ScreenShareErrorCode::decoder_failed,
                    std::string{platform::windows::name(failure->code)},
                    failure->native_code};
            }
            if (!decoded.texture) {
                return std::nullopt;
            }
            auto& counters = owner_.counters_;
            if (counters.remote_decoded.fetch_add(
                    1, std::memory_order_relaxed) == 0) {
                trace_event("receiver-first-frame-decoded");
            }
            counters.remote_width.store(
                decoded.width, std::memory_order_relaxed);
            counters.remote_height.store(
                decoded.height, std::memory_order_relaxed);
            if (!first_present_traced_) {
                trace_event("receiver-first-frame-present-begin");
            }
            if (const auto failure =
                    presenter_.present(
                        *decoded.texture.Get(),
                        decoded.subresource_index,
                        decoded.width,
                        decoded.height)) {
                trace_event("receiver-present-error");
                return ScreenShareError{
                    ScreenShareErrorCode::remote_present_failed,
                    std::string{platform::windows::name(failure->code)},
                    failure->native_code};
            }
            if (!first_present_traced_) {
                trace_event("receiver-first-frame-presented");
                first_present_traced_ = true;
            }
            const auto presentation = presenter_.statistics();
            counters.remote_presented.store(
                presentation.frames_presented,
                std::memory_order_relaxed);
            counters.remote_present_drops.store(
                presentation.frames_dropped,
                std::memory_order_relaxed);
            counters.remote_last_frame_ns.store(
                steady_now_ns(), std::memory_order_release);
            if (const auto swap_chain = presenter_.swap_chain()) {
                std::scoped_lock lock(owner_.preview_mutex_);
                if (owner_.remote_swap_chain_.Get() !=
                    swap_chain.Get()) {
                    owner_.remote_swap_chain_ = swap_chain;
                }
            }
            return std::nullopt;
        }

    private:
        Impl& owner_;
        WindowsH264D3D11Decoder decoder_;
        H264DecoderConfig decoder_config_;
        D3D11CompositionVideoPresenter presenter_;
        bool first_present_traced_ = false;
    };

    [[nodiscard]] std::optional<ScreenShareError> start_listening(
        const ScreenTransportConfig& config) {
        std::scoped_lock lifecycle_lock(lifecycle_mutex_);

        if (!valid_transport(config)) {
            return ScreenShareError{
                ScreenShareErrorCode::invalid_config,
                "invalid screen transport configuration"};
        }

        if ((socket_ ||
             config.room_runtime != nullptr) &&
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

            // A window shares its app's audio; a display shares computer audio without Catro.
            const bool can_stream_audio =
                config.share_audio &&
                config.room_runtime != nullptr &&
                (config.source.kind ==
                     platform::windows::CaptureSourceKind::display ||
                 config.source.process_id != 0);
            stream_audio_enabled_.store(
                can_stream_audio,
                std::memory_order_release);
            if (can_stream_audio) {
                stream_audio_sender_worker_ =
                    std::thread(
                        [this, config] {
                            run_stream_audio_sender_guarded(
                                config);
                        });
            }
        } catch (...) {
            share_stop_requested_.store(
                true, std::memory_order_release);
            stop_cv_.notify_all();
            if (stream_audio_sender_worker_.joinable()) {
                stream_audio_sender_worker_.join();
            }
            if (sender_worker_.joinable()) {
                sender_worker_.join();
            }
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
                stream_audio_receiver_worker_ =
                    std::thread(
                        [this, config] {
                            run_stream_audio_receiver_guarded(
                                config);
                        });
            } catch (...) {
                stop_requested_.store(
                    true, std::memory_order_release);
                stop_cv_.notify_all();
                if (stream_audio_receiver_worker_.joinable()) {
                    stream_audio_receiver_worker_.join();
                }
                if (receiver_worker_.joinable()) {
                    receiver_worker_.join();
                }
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

    void set_local_preview_full(bool full) noexcept {
        local_preview_full_.store(full, std::memory_order_release);
    }

    void set_stream_volume(float volume) noexcept {
        counters_.remote_stream_volume.store(
            std::clamp(volume, 0.0F, 2.0F), std::memory_order_relaxed);
    }

    void set_echo_sink(std::function<void(std::span<const float>)> sink) {
        counters_.echo_sink = std::move(sink);
    }

    void set_remote_viewing_enabled(bool enabled) noexcept {
        counters_.remote_viewing_enabled.store(enabled, std::memory_order_release);
        if (!enabled) {
            // UI should detach immediately; the receive worker releases decoder/presenter GPU
            // resources on its next <=20 ms receive-loop iteration.
            counters_.remote_last_frame_ns.store(0, std::memory_order_release);
            counters_.remote_width.store(0, std::memory_order_relaxed);
            counters_.remote_height.store(0, std::memory_order_relaxed);
            std::scoped_lock lock(preview_mutex_);
            remote_swap_chain_.Reset();
        }
    }

    void stop_sharing_locked() noexcept {
        share_stop_requested_.store(
            true, std::memory_order_release);
        stop_cv_.notify_all();

        if (stream_audio_sender_worker_.joinable()) {
            stream_audio_sender_worker_.join();
        }
        if (sender_worker_.joinable()) {
            sender_worker_.join();
        }
        stream_audio_enabled_.store(
            false, std::memory_order_release);
        stream_audio_active_.store(
            false, std::memory_order_release);

        {
            std::scoped_lock lock(preview_mutex_);
            preview_swap_chain_.Reset();
        }

        const auto state =
            state_.load(std::memory_order_acquire);
        if ((socket_ ||
             (transport_config_ &&
              transport_config_->room_runtime != nullptr)) &&
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

        if (stream_audio_sender_worker_.joinable()) {
            stream_audio_sender_worker_.join();
        }
        if (sender_worker_.joinable()) {
            sender_worker_.join();
        }
        if (stream_audio_receiver_worker_.joinable()) {
            stream_audio_receiver_worker_.join();
        }
        if (receiver_worker_.joinable()) {
            receiver_worker_.join();
        }

        stream_audio_enabled_.store(
            false, std::memory_order_release);
        stream_audio_active_.store(
            false, std::memory_order_release);
        counters_.remote_stream_audio_active.store(
            false, std::memory_order_release);

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

    void set_stream_audio_error(
        std::string_view message,
        std::int64_t native_code = 0) noexcept {
        try {
            std::string owned{message};
            if (native_code != 0) {
                owned += " (native ";
                owned += std::to_string(native_code);
                owned += ")";
            }
            std::scoped_lock lock(metadata_mutex_);
            stream_audio_error_ = std::move(owned);
        } catch (...) {
        }
    }

    void run_stream_audio_sender_guarded(
        ScreenShareConfig config) noexcept {
        trace_event("stream-audio-sender-enter");
        try {
            run_stream_audio_sender(config);
            trace_event("stream-audio-sender-exit");
        } catch (const std::exception& error) {
            set_stream_audio_error(
                error.what() != nullptr
                    ? std::string_view{error.what()}
                    : std::string_view{
                          "stream audio sender exception"});
        } catch (...) {
            set_stream_audio_error(
                "stream audio sender exception");
        }
        stream_audio_active_.store(
            false, std::memory_order_release);
    }

    void run_stream_audio_sender(
        const ScreenShareConfig& config) {
        const bool display =
            config.source.kind ==
            platform::windows::CaptureSourceKind::display;
        if (config.room_runtime == nullptr ||
            (!display && config.source.process_id == 0)) {
            return;
        }

        StreamAudioSender sender(
            room_api(), config.room_runtime, counters_);
        if (const auto failure = sender.start(
                config.stream_audio_bitrate, config.ssrc)) {
            set_stream_audio_error(
                voice::name(failure->code),
                failure->native_code);
            return;
        }
        StreamAudioCaptureBridge bridge;
        platform::windows::ProcessLoopbackAudioCapture
            capture;
        const auto capture_failure =
            capture.start(
                display ? static_cast<std::uint32_t>(
                              GetCurrentProcessId())
                        : config.source.process_id,
                [&bridge](
                    std::span<const float> samples) noexcept {
                    bridge.on_captured(samples);
                },
                display);
        if (capture_failure) {
            set_stream_audio_error(
                platform::windows::name(
                    capture_failure->code),
                capture_failure->native_code);
            return;
        }
        {
            std::scoped_lock lock(metadata_mutex_);
            stream_audio_error_.clear();
        }
        stream_audio_active_.store(
            true, std::memory_order_release);
        trace_event("stream-audio-capture-started");
        StreamAudioPcmFrame pcm{};
        while (!should_stop_sender()) {
            if (!bridge.try_pop(pcm)) {
                std::unique_lock stop_lock(stop_mutex_);
                stop_cv_.wait_for(
                    stop_lock,
                    2ms,
                    [this] {
                        return should_stop_sender();
                    });
                continue;
            }
            sender.send(pcm);
            // Shared audio also plays on this PC's speakers.
            if (counters_.echo_sink) {
                counters_.echo_sink(pcm);
            }
        }
        capture.stop();
        counters_.stream_audio_capture_drops.store(
            bridge.dropped_callbacks(),
            std::memory_order_relaxed);
    }

    void run_stream_audio_receiver_guarded(
        ScreenTransportConfig config) noexcept {
        trace_event("stream-audio-receiver-enter");
        try {
            WasapiStreamOutput output;
            run_stream_audio_receive_loop(
                room_api(),
                config.room_runtime,
                counters_,
                stop_requested_,
                output);
            trace_event("stream-audio-receiver-exit");
        } catch (...) {
            counters_.remote_stream_audio_decode_failures.fetch_add(
                1, std::memory_order_relaxed);
        }
        counters_.remote_stream_audio_active.store(
            false, std::memory_order_release);
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
        if (socket == nullptr &&
            config.room_runtime == nullptr) {
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
        // The inline self-preview is a small 10 FPS thumbnail. Watching your own stream full
        // screen switches to the stream's own size and frame rate, so it shows what viewers get.
        bool presenter_full = local_preview_full_.load(std::memory_order_acquire);
        const auto make_presenter = [&config](bool full) {
            return std::make_unique<D3D11CompositionVideoPresenter>(
                full ? VideoPresenterConfig{
                           .max_width = config.max_width,
                           .max_height = config.max_height,
                           .frame_rate = config.fps,
                       }
                     : VideoPresenterConfig{
                           .max_width = 640,
                           .max_height = 360,
                           .frame_rate = std::min(config.fps, kPreviewMaxFps),
                       });
        };
        auto presenter = make_presenter(presenter_full);

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

        VideoSender video_sender(
            room_api(),
            config.room_runtime,
            socket,
            video::H264RtpConfig{
                config.ssrc,
                config.payload_type,
                config.mtu_bytes,
            },
            counters_);
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
            encoder_config.bitrate = video_sender.bitrate(config.bitrate);
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
            const auto swap_chain = presenter->swap_chain();
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
                    presenter->reset();
                    {
                        std::scoped_lock lock(preview_mutex_);
                        preview_swap_chain_.Reset();
                    }
                    preview_resources_live = false;
                }
            } else {
                const bool want_full = local_preview_full_.load(std::memory_order_acquire);
                if (want_full != presenter_full) {
                    presenter->reset();
                    {
                        std::scoped_lock lock(preview_mutex_);
                        preview_swap_chain_.Reset();
                    }
                    presenter = make_presenter(want_full);
                    presenter_full = want_full;
                    preview_resources_live = false;
                    preview_accumulator = config.fps;
                }
                preview_accumulator +=
                    want_full ? config.fps : std::min(config.fps, kPreviewMaxFps);
                if (preview_accumulator >= config.fps) {
                    preview_accumulator -= config.fps;
                    if (!preview_resources_live) {
                        trace_event("sender-local-preview-first-present");
                    }
                    bool preview_ok = true;
                    try {
                        if (const auto error =
                                presenter->present(
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
                        presenter->reset();
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
                            presenter->statistics();
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

            if (const auto bitrate = video_sender.adapt_bitrate(config.bitrate, steady_now_ns())) {
                encoder.set_bitrate(bitrate);
            }
            if (const auto error =
                    encoder.encode(frame, access_unit, video_sender.keyframe_requested())) {
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

            const auto sent = video_sender.send(
                access_unit.bytes,
                monotonic_rtp_timestamp(started, Clock::now()));
            switch (sent.status) {
            case VideoSendStatus::sent:
                break;
            case VideoSendStatus::dropped:
                return true;
            case VideoSendStatus::room_failed:
                fail_session(
                    ScreenShareErrorCode::network_failed,
                    room_error_text(
                        room_api(),
                        config.room_runtime,
                        "RTC room video transport failed"));
                return false;
            case VideoSendStatus::network_failed:
                fail_session(
                    ScreenShareErrorCode::network_failed,
                    udp_error_text(*sent.network_error));
                return false;
            case VideoSendStatus::not_packetizable:
                fail_share(
                    ScreenShareErrorCode::packetization_failed,
                    "H.264 access unit is not RFC 6184 packetizable");
                return false;
            }
            if (counters_.frames_sent.load(
                    std::memory_order_relaxed) == 1) {
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
        const auto preview_stats = presenter->statistics();
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
        presenter->reset();

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
        RemoteViewer viewer(*this, config.max_access_unit_bytes);
        const VideoReceiveContext context{
            .api = room_api(),
            .config = config,
            .socket = socket_.get(),
            .counters = &counters_,
            .stop_requested = &stop_requested_,
            .trace = &trace_event,
        };
        if (const auto failure =
                run_video_receive_loop(context, viewer)) {
            fail_session(
                failure->code,
                failure->message,
                failure->native_code);
        }
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
        preview_frames_.store(0, std::memory_order_relaxed);
        preview_drops_.store(0, std::memory_order_relaxed);
        encoder_input_failures_.store(0, std::memory_order_relaxed);
        encoder_output_failures_.store(0, std::memory_order_relaxed);
        encoder_timeouts_.store(0, std::memory_order_relaxed);
        capture_contention_drops_.store(0, std::memory_order_relaxed);
        stream_audio_active_.store(false, std::memory_order_relaxed);
        counters_.reset_local();
        {
            std::scoped_lock lock(metadata_mutex_);
            stream_audio_error_.clear();
        }
    }

    void reset_all_statistics() noexcept {
        reset_local_statistics();
        counters_.reset_remote();
        counters_.peer_unreachable_events.store(
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
            result.stream_audio_error =
                stream_audio_error_;
        }
        counters_.fill(result);
        constexpr auto relaxed = std::memory_order_relaxed;
        result.source_width = source_width_.load(relaxed);
        result.source_height = source_height_.load(relaxed);
        result.encoded_width = encoded_width_.load(relaxed);
        result.encoded_height = encoded_height_.load(relaxed);
        result.frames_encoded = frames_encoded_.load(relaxed);
        result.preview_frames = preview_frames_.load(relaxed);
        result.preview_drops = preview_drops_.load(relaxed);
        result.encoder_input_failures =
            encoder_input_failures_.load(relaxed);
        result.encoder_output_failures =
            encoder_output_failures_.load(relaxed);
        result.encoder_timeouts = encoder_timeouts_.load(relaxed);
        result.capture_contention_drops =
            capture_contention_drops_.load(relaxed);
        result.stream_audio_enabled =
            stream_audio_enabled_.load(relaxed);
        result.stream_audio_active =
            stream_audio_active_.load(relaxed);
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
    std::string stream_audio_error_;

    std::unique_ptr<UdpPeerSocket> socket_;
    std::optional<ScreenTransportConfig> transport_config_;
    ComPtr<IDXGISwapChain1> preview_swap_chain_;
    ComPtr<IDXGISwapChain1> remote_swap_chain_;

    std::thread sender_worker_;
    std::thread receiver_worker_;
    std::thread stream_audio_sender_worker_;
    std::thread stream_audio_receiver_worker_;
    std::atomic_bool stop_requested_{false};
    std::atomic_bool share_stop_requested_{true};
    std::atomic_bool local_preview_enabled_{true};
    std::atomic_bool local_preview_full_{false};
    std::atomic<ScreenShareState> state_{
        ScreenShareState::idle};

    std::atomic<std::uint32_t> source_width_{0};
    std::atomic<std::uint32_t> source_height_{0};
    std::atomic<std::uint32_t> encoded_width_{0};
    std::atomic<std::uint32_t> encoded_height_{0};
    std::atomic<std::uint64_t> frames_encoded_{0};
    std::atomic<std::uint64_t> preview_frames_{0};
    std::atomic<std::uint64_t> preview_drops_{0};
    std::atomic<std::uint64_t> encoder_input_failures_{0};
    std::atomic<std::uint64_t> encoder_output_failures_{0};
    std::atomic<std::uint64_t> encoder_timeouts_{0};
    std::atomic<std::uint64_t> capture_contention_drops_{0};

    std::atomic_bool stream_audio_enabled_{false};
    std::atomic_bool stream_audio_active_{false};

    ScreenTransportCounters counters_;
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

void WindowsScreenShareRuntime::set_local_preview_full(
    bool full) noexcept {
    impl_->set_local_preview_full(full);
}

void WindowsScreenShareRuntime::set_remote_viewing_enabled(
    bool enabled) noexcept {
    impl_->set_remote_viewing_enabled(enabled);
}

void WindowsScreenShareRuntime::set_stream_volume(
    float volume) noexcept {
    impl_->set_stream_volume(volume);
}

void WindowsScreenShareRuntime::set_echo_sink(
    std::function<void(std::span<const float>)> sink) {
    impl_->set_echo_sink(std::move(sink));
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
