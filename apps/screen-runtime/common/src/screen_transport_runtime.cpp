#include <catro/screen_transport_runtime.hpp>

#include <catro/voice/jitter.hpp>
#include <catro/voice/packet.hpp>

#include <algorithm>
#include <cstring>
#include <new>
#include <utility>
#include <variant>

namespace catro::screen {
namespace {

using transport::UdpError;
using transport::UdpErrorCode;
using transport::UdpPeerSocket;

constexpr std::int64_t kRemoteInactiveNs =
    std::chrono::duration_cast<std::chrono::nanoseconds>(kRemoteInactiveTimeout).count();

[[nodiscard]] bool inactive_since(std::int64_t last_ns, std::int64_t now_ns) noexcept {
    return last_ns != 0 && now_ns >= last_ns && now_ns - last_ns >= kRemoteInactiveNs;
}

[[nodiscard]] bool recently(std::int64_t last_ns, std::int64_t now_ns) noexcept {
    if (last_ns == 0) {
        return false;
    }
    const auto age = now_ns - last_ns;
    return age >= 0 && age < kRemoteInactiveNs;
}

[[nodiscard]] std::int64_t extended_rtp_to_100ns(std::uint64_t timestamp_90khz) noexcept {
    // 10,000,000 / 90,000 = 1000 / 9. The receiver extends the 32-bit RTP clock before this
    // conversion, so a long-lived room can cross the ~13-hour RTP wrap without PTS moving back.
    return static_cast<std::int64_t>((timestamp_90khz * 1000ULL + 4ULL) / 9ULL);
}

// Video receive source over either the room lane or the direct engineering socket. Room mode keeps
// one pending datagram so wait_readable() can block inside the room's bounded queue.
class VideoReceiveSource final {
public:
    VideoReceiveSource(const RoomScreenApi& api, UdpPeerSocket* socket, CatroRoomRuntimeHandle room) noexcept
        : api_(api), socket_(socket), room_(room) {}

    [[nodiscard]] bool valid() const noexcept {
        return socket_ != nullptr || (room_ != nullptr && api_.receive_video != nullptr);
    }

    [[nodiscard]] bool room_mode() const noexcept { return socket_ == nullptr; }

    [[nodiscard]] UdpPeerSocket::WaitResult wait_readable(std::chrono::microseconds timeout) noexcept {
        if (socket_ != nullptr) {
            return socket_->wait_readable(timeout);
        }
        if (pending_size_ != 0) {
            return true;
        }
        const auto rounded_ms = timeout <= std::chrono::microseconds::zero()
                                    ? 0ULL
                                    : static_cast<unsigned long long>((timeout.count() + 999) / 1000);
        const auto timeout_ms = static_cast<std::uint32_t>(
            std::min<unsigned long long>(rounded_ms, static_cast<unsigned long long>(UINT32_MAX)));
        const auto received = api_.receive_video(room_, pending_.data(), pending_.size(), timeout_ms);
        if (received < 0) {
            return UdpError{UdpErrorCode::receive_failed};
        }
        pending_size_ = static_cast<std::size_t>(received);
        return pending_size_ != 0;
    }

    [[nodiscard]] UdpPeerSocket::SizeResult receive(std::span<std::byte> destination) noexcept {
        if (socket_ != nullptr) {
            return socket_->receive(destination);
        }
        if (pending_size_ != 0) {
            if (pending_size_ > destination.size()) {
                pending_size_ = 0;
                return UdpError{UdpErrorCode::datagram_too_large};
            }
            std::memcpy(destination.data(), pending_.data(), pending_size_);
            return std::exchange(pending_size_, 0);
        }
        const auto received = api_.receive_video(room_, destination.data(), destination.size(), 0);
        if (received < 0) {
            return UdpError{UdpErrorCode::receive_failed};
        }
        return static_cast<std::size_t>(received);
    }

private:
    RoomScreenApi api_;
    UdpPeerSocket* socket_ = nullptr;
    CatroRoomRuntimeHandle room_ = nullptr;
    std::array<std::byte, kReceiveDatagramBytes> pending_{};
    std::size_t pending_size_ = 0;
};

// Deltas are useless until the decoder has seen a keyframe, and after a lost frame they would only
// smear artifacts, so the picture holds until the next one. The gate also extends the 32-bit RTP
// clock so presentation timestamps never move backwards across a wrap.
class KeyframeGate final {
public:
    void reset() noexcept { *this = KeyframeGate{}; }

    [[nodiscard]] bool awaiting_keyframe() const noexcept { return awaiting_keyframe_; }
    [[nodiscard]] bool decoder_open() const noexcept { return decoder_open_; }

    void opened() noexcept {
        awaiting_keyframe_ = false;
        if (!decoder_open_) {
            have_timestamp_ = false;
        }
        decoder_open_ = true;
    }

    // A lost frame broke the reference chain; the decoder and the clock stay.
    void lost() noexcept { awaiting_keyframe_ = true; }

    [[nodiscard]] std::int64_t pts_100ns(std::uint32_t timestamp) noexcept {
        if (!have_timestamp_) {
            extended_ = timestamp;
            have_timestamp_ = true;
        } else {
            extended_ += static_cast<std::uint32_t>(timestamp - last_);
        }
        last_ = timestamp;
        return extended_rtp_to_100ns(extended_);
    }

private:
    bool awaiting_keyframe_ = true;
    bool decoder_open_ = false;
    bool have_timestamp_ = false;
    std::uint32_t last_ = 0;
    std::uint64_t extended_ = 0;
};

} // namespace

bool valid_media_bounds(std::uint8_t payload_type, std::uint16_t mtu_bytes,
                        std::size_t max_access_unit_bytes) noexcept {
    return payload_type >= 96 && payload_type <= 127 && mtu_bytes >= 576 && mtu_bytes <= 1400 &&
           max_access_unit_bytes >= 262'144 && max_access_unit_bytes <= 16U * 1024U * 1024U;
}

bool valid_direct_endpoints(const transport::UdpEndpoint& bind, const transport::UdpEndpoint& peer) noexcept {
    return !bind.address.empty() && !peer.address.empty() && bind.port != 0 && peer.port != 0;
}

bool valid_transport(const ScreenTransportConfig& config) noexcept {
    return valid_media_bounds(config.payload_type, config.mtu_bytes, config.max_access_unit_bytes) &&
           (config.room_runtime != nullptr || valid_direct_endpoints(config.bind, config.peer));
}

std::chrono::nanoseconds frame_period(std::uint32_t fps) noexcept {
    constexpr std::uint64_t kNanosecondsPerSecond = 1'000'000'000ULL;
    return std::chrono::nanoseconds(static_cast<std::int64_t>((kNanosecondsPerSecond + fps / 2U) / fps));
}

std::uint32_t monotonic_rtp_timestamp(Clock::time_point started, Clock::time_point now) noexcept {
    const auto elapsed_100ns =
        std::chrono::duration_cast<std::chrono::duration<std::uint64_t, std::ratio<1, 10'000'000>>>(now - started)
            .count();
    return video::rtp_timestamp_90khz(elapsed_100ns);
}

std::int64_t steady_now_ns() noexcept {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();
}

std::string udp_error_text(const UdpError& error) {
    std::string text{transport::name(error.code)};
    if (error.native_code != 0) {
        text += " (native ";
        text += std::to_string(error.native_code);
        text += ")";
    }
    return text;
}

std::string room_error_text(const RoomScreenApi& api, CatroRoomRuntimeHandle room, std::string_view fallback) {
    if (room != nullptr && api.snapshot != nullptr) {
        const auto snapshot = api.snapshot(room);
        if (snapshot.error[0] != '\0') {
            return snapshot.error;
        }
    }
    return std::string{fallback};
}

void ScreenTransportCounters::reset_local() noexcept {
    frames_sent.store(0, std::memory_order_relaxed);
    frames_dropped.store(0, std::memory_order_relaxed);
    packets_sent.store(0, std::memory_order_relaxed);
    wire_bytes.store(0, std::memory_order_relaxed);
    backpressure_events.store(0, std::memory_order_relaxed);
    stream_audio_frames_encoded.store(0, std::memory_order_relaxed);
    stream_audio_packets_sent.store(0, std::memory_order_relaxed);
    stream_audio_capture_drops.store(0, std::memory_order_relaxed);
    stream_audio_encode_failures.store(0, std::memory_order_relaxed);
}

void ScreenTransportCounters::reset_remote() noexcept {
    remote_viewing_enabled.store(false, std::memory_order_relaxed);
    remote_last_stream_ns.store(0, std::memory_order_relaxed);
    remote_last_frame_ns.store(0, std::memory_order_relaxed);
    remote_width.store(0, std::memory_order_relaxed);
    remote_height.store(0, std::memory_order_relaxed);
    remote_packets.store(0, std::memory_order_relaxed);
    remote_wire_bytes.store(0, std::memory_order_relaxed);
    remote_frames.store(0, std::memory_order_relaxed);
    remote_decoded.store(0, std::memory_order_relaxed);
    remote_presented.store(0, std::memory_order_relaxed);
    remote_frame_drops.store(0, std::memory_order_relaxed);
    remote_packet_rejects.store(0, std::memory_order_relaxed);
    remote_decode_failures.store(0, std::memory_order_relaxed);
    remote_present_drops.store(0, std::memory_order_relaxed);
    remote_stream_resets.store(0, std::memory_order_relaxed);
    remote_stream_audio_active.store(false, std::memory_order_relaxed);
    remote_stream_audio_packets.store(0, std::memory_order_relaxed);
    remote_stream_audio_frames.store(0, std::memory_order_relaxed);
    remote_stream_audio_decode_failures.store(0, std::memory_order_relaxed);
    remote_stream_audio_render_drops.store(0, std::memory_order_relaxed);
    remote_stream_audio_last_ns.store(0, std::memory_order_relaxed);
}

void ScreenTransportCounters::fill(ScreenShareSnapshot& result) const noexcept {
    constexpr auto relaxed = std::memory_order_relaxed;
    result.frames_sent = frames_sent.load(relaxed);
    result.frames_dropped = frames_dropped.load(relaxed);
    result.packets_sent = packets_sent.load(relaxed);
    result.wire_bytes = wire_bytes.load(relaxed);
    result.backpressure_events = backpressure_events.load(relaxed);
    result.peer_unreachable_events = peer_unreachable_events.load(relaxed);
    result.stream_audio_frames_encoded = stream_audio_frames_encoded.load(relaxed);
    result.stream_audio_packets_sent = stream_audio_packets_sent.load(relaxed);
    result.stream_audio_capture_drops = stream_audio_capture_drops.load(relaxed);
    result.stream_audio_encode_failures = stream_audio_encode_failures.load(relaxed);

    const auto now = steady_now_ns();
    result.remote_viewing = remote_viewing_enabled.load(std::memory_order_acquire);
    result.remote_available = recently(remote_last_stream_ns.load(std::memory_order_acquire), now);
    result.remote_active =
        result.remote_viewing && recently(remote_last_frame_ns.load(std::memory_order_acquire), now);
    result.remote_width = remote_width.load(relaxed);
    result.remote_height = remote_height.load(relaxed);
    result.remote_packets = remote_packets.load(relaxed);
    result.remote_wire_bytes = remote_wire_bytes.load(relaxed);
    result.remote_frames = remote_frames.load(relaxed);
    result.remote_decoded = remote_decoded.load(relaxed);
    result.remote_presented = remote_presented.load(relaxed);
    result.remote_frame_drops = remote_frame_drops.load(relaxed);
    result.remote_packet_rejects = remote_packet_rejects.load(relaxed);
    result.remote_decode_failures = remote_decode_failures.load(relaxed);
    result.remote_present_drops = remote_present_drops.load(relaxed);
    result.remote_stream_resets = remote_stream_resets.load(relaxed);
    result.remote_stream_audio_active = remote_stream_audio_active.load(relaxed);
    result.remote_stream_audio_packets = remote_stream_audio_packets.load(relaxed);
    result.remote_stream_audio_frames = remote_stream_audio_frames.load(relaxed);
    result.remote_stream_audio_decode_failures = remote_stream_audio_decode_failures.load(relaxed);
    result.remote_stream_audio_render_drops = remote_stream_audio_render_drops.load(relaxed);
}

VideoSender::VideoSender(const RoomScreenApi& api, CatroRoomRuntimeHandle room, UdpPeerSocket* socket,
                         video::H264RtpConfig rtp, ScreenTransportCounters& counters) noexcept
    : api_(api), room_(room), socket_(socket), rtp_(rtp), counters_(counters) {}

bool VideoSender::keyframe_requested() noexcept {
    if (room_ == nullptr || api_.keyframe_requests == nullptr) {
        return false;
    }
    const auto requests = api_.keyframe_requests(room_);
    const auto now = steady_now_ns();
    if (requests == keyframe_requests_seen_ ||
        now - last_forced_keyframe_ns_ <
            std::chrono::duration_cast<std::chrono::nanoseconds>(kKeyframeRequestInterval).count()) {
        return false;
    }
    keyframe_requests_seen_ = requests;
    last_forced_keyframe_ns_ = now;
    return true;
}

std::uint32_t VideoSender::bitrate(std::uint32_t target) const noexcept {
    return bitrate_ != 0 ? std::min(bitrate_, target) : target;
}

std::uint32_t VideoSender::adapt_bitrate(std::uint32_t target, std::int64_t now_ns) noexcept {
    const auto requests =
        room_ != nullptr && api_.keyframe_requests != nullptr ? api_.keyframe_requests(room_) : 0;
    const auto backpressure = counters_.backpressure_events.load(std::memory_order_relaxed);
    if (bitrate_ == 0) {
        bitrate_ = target;
        adapt_last_ns_ = now_ns;
        adapt_requests_ = requests;
        adapt_backpressure_ = backpressure;
        return 0;
    }
    if (now_ns - adapt_last_ns_ < std::chrono::nanoseconds(kBitrateAdaptInterval).count()) {
        return 0;
    }
    // A single request is a viewer joining or one stray loss; a second one within the interval
    // means the picture keeps breaking.
    const bool congested = requests - adapt_requests_ >= 2 || backpressure != adapt_backpressure_;
    adapt_last_ns_ = now_ns;
    adapt_requests_ = requests;
    adapt_backpressure_ = backpressure;
    const auto floor = std::max<std::uint32_t>(target / 4, std::min<std::uint32_t>(target, 128'000));
    const auto next = congested ? std::max(floor, bitrate_ / 4 * 3)
                                : std::min(target, bitrate_ + bitrate_ / 12);
    if (next == bitrate_) {
        return 0;
    }
    bitrate_ = next;
    return next;
}

VideoSendResult VideoSender::send(std::span<const std::byte> annex_b, std::uint32_t timestamp_90khz) noexcept {
    soft_drop_ = false;
    room_failed_ = false;
    fatal_error_.reset();
    const auto packetized =
        video::packetize_h264_annex_b(annex_b, timestamp_90khz, next_sequence_, rtp_, this, &VideoSender::send_packet);
    next_sequence_ = packetized.next_sequence;
    if (packetized) {
        counters_.frames_sent.fetch_add(1, std::memory_order_relaxed);
        return {};
    }
    if (room_failed_) {
        return {VideoSendStatus::room_failed, std::nullopt};
    }
    if (fatal_error_) {
        return {VideoSendStatus::network_failed, fatal_error_};
    }
    if (soft_drop_) {
        counters_.frames_dropped.fetch_add(1, std::memory_order_relaxed);
        return {VideoSendStatus::dropped, std::nullopt};
    }
    return {VideoSendStatus::not_packetizable, std::nullopt};
}

bool VideoSender::send_packet(void* opaque, const video::RtpPacketSlice& packet) noexcept {
    auto& self = *static_cast<VideoSender*>(opaque);
    const auto prefix =
        std::span<const std::byte>(packet.prefix.data(), static_cast<std::size_t>(packet.prefix_size));
    if (self.room_ != nullptr) {
        if (prefix.size() + packet.payload.size() > kReceiveDatagramBytes) {
            self.soft_drop_ = true;
            return false;
        }
        std::array<std::byte, kReceiveDatagramBytes> datagram{};
        std::memcpy(datagram.data(), prefix.data(), prefix.size());
        std::memcpy(datagram.data() + prefix.size(), packet.payload.data(), packet.payload.size());
        const auto size = prefix.size() + packet.payload.size();
        const auto peers = self.api_.send_video(self.room_, datagram.data(), size);
        if (self.api_.snapshot(self.room_).state == CATRO_ROOM_FAILED) {
            self.room_failed_ = true;
            return false;
        }
        // Zero viewers is not a media failure. The stream stays live and starts fanning out
        // immediately when a room peer arrives.
        if (peers != 0) {
            self.counters_.packets_sent.fetch_add(1, std::memory_order_relaxed);
            self.counters_.wire_bytes.fetch_add(size * peers, std::memory_order_relaxed);
        }
        return true;
    }
    if (self.socket_ == nullptr) {
        self.room_failed_ = true;
        return false;
    }
    const std::array<std::span<const std::byte>, 2> segments{prefix, packet.payload};
    const auto result = self.socket_->send_segments(segments);
    if (const auto* bytes = std::get_if<std::size_t>(&result)) {
        self.counters_.packets_sent.fetch_add(1, std::memory_order_relaxed);
        self.counters_.wire_bytes.fetch_add(*bytes, std::memory_order_relaxed);
        return true;
    }
    const auto failure = std::get<UdpError>(result);
    if (failure.code == UdpErrorCode::would_block) {
        self.counters_.backpressure_events.fetch_add(1, std::memory_order_relaxed);
        self.soft_drop_ = true;
        return false;
    }
    if (failure.code == UdpErrorCode::peer_unreachable) {
        self.counters_.peer_unreachable_events.fetch_add(1, std::memory_order_relaxed);
        self.soft_drop_ = true;
        return false;
    }
    self.fatal_error_ = failure;
    return false;
}

std::optional<ScreenShareError> run_video_receive_loop(const VideoReceiveContext& context,
                                                       RemoteVideoViewer& viewer) {
    auto& counters = *context.counters;
    const auto& config = context.config;
    const auto trace = [&](std::string_view event) noexcept {
        if (context.trace != nullptr) {
            context.trace(event);
        }
    };
    VideoReceiveSource source{context.api, context.socket, config.room_runtime};
    if (!source.valid()) {
        return ScreenShareError{ScreenShareErrorCode::network_failed, "video transport is not running", 0};
    }
    std::unique_ptr<std::byte[]> frame_memory(new (std::nothrow) std::byte[config.max_access_unit_bytes]);
    if (!frame_memory) {
        return ScreenShareError{ScreenShareErrorCode::memory_failed,
                                "remote H.264 frame buffer allocation failed", 0};
    }
    // SSRC 0 locks onto the first stream seen; the reset rule below decides when it may change.
    video::H264RtpReassembler reassembler(std::span<std::byte>(frame_memory.get(), config.max_access_unit_bytes),
                                          video::H264RtpConfig{0, config.payload_type, config.mtu_bytes});
    std::array<std::byte, kReceiveDatagramBytes> datagram{};
    KeyframeGate gate;
    bool viewing_last = false;
    bool first_frame_traced = false;
    std::int64_t last_keyframe_request_ns = 0;

    // Asks the sharer for an IDR instead of waiting out its GOP; repeated while still waiting.
    const auto request_keyframe = [&] {
        if (config.room_runtime == nullptr || context.api.request_keyframe == nullptr) {
            return;
        }
        const auto now = steady_now_ns();
        if (now - last_keyframe_request_ns <
            std::chrono::duration_cast<std::chrono::nanoseconds>(kKeyframeRequestInterval).count()) {
            return;
        }
        last_keyframe_request_ns = now;
        context.api.request_keyframe(config.room_runtime);
    };

    const auto release_viewer = [&] {
        viewer.release();
        counters.remote_width.store(0, std::memory_order_relaxed);
        counters.remote_height.store(0, std::memory_order_relaxed);
        counters.remote_last_frame_ns.store(0, std::memory_order_release);
        gate.reset();
    };
    const auto network_failure = [&](const UdpError& failure) {
        return ScreenShareError{ScreenShareErrorCode::network_failed,
                                source.room_mode()
                                    ? room_error_text(context.api, config.room_runtime, "RTC room video receive failed")
                                    : udp_error_text(failure),
                                0};
    };
    const auto synchronize_viewing_state = [&] {
        auto requested = counters.remote_viewing_enabled.load(std::memory_order_acquire);
        // Viewing ends by itself when the stream goes quiet so no GPU work outlives the stream.
        if (requested && inactive_since(counters.remote_last_stream_ns.load(std::memory_order_acquire),
                                        steady_now_ns())) {
            counters.remote_viewing_enabled.store(false, std::memory_order_release);
            requested = false;
        }
        if (requested != viewing_last) {
            if (!requested) {
                trace("receiver-viewing-stopped");
                release_viewer();
            } else {
                trace("receiver-viewing-requested");
                gate.reset();
            }
            viewing_last = requested;
        }
        return requested;
    };
    const auto handle_frame = [&](const video::ReassembledH264Frame& frame) -> std::optional<ScreenShareError> {
        counters.remote_frames.fetch_add(1, std::memory_order_relaxed);
        counters.remote_last_stream_ns.store(steady_now_ns(), std::memory_order_release);
        if (!first_frame_traced) {
            trace("receiver-first-frame-reassembled");
            first_frame_traced = true;
        }
        if (!synchronize_viewing_state()) {
            return std::nullopt;
        }
        if (gate.awaiting_keyframe()) {
            if (!frame.keyframe) {
                request_keyframe();
                return std::nullopt;
            }
            if (!gate.decoder_open()) {
                if (auto failure = viewer.start_decoder()) {
                    counters.remote_decode_failures.fetch_add(1, std::memory_order_relaxed);
                    return failure;
                }
                trace("receiver-decoder-started");
            }
            gate.opened();
        }
        auto failure = viewer.decode_and_present(frame.annex_b, gate.pts_100ns(frame.timestamp_90khz));
        if (failure && failure->code == ScreenShareErrorCode::decoder_failed) {
            counters.remote_decode_failures.fetch_add(1, std::memory_order_relaxed);
        }
        return failure;
    };

    std::optional<ScreenShareError> fatal;
    while (!fatal && !context.stop_requested->load(std::memory_order_acquire)) {
        (void)synchronize_viewing_state();
        const auto ready =
            source.wait_readable(std::chrono::duration_cast<std::chrono::microseconds>(kReceiveWait));
        if (const auto* failure = std::get_if<UdpError>(&ready)) {
            if (failure->code == UdpErrorCode::peer_unreachable) {
                counters.peer_unreachable_events.fetch_add(1, std::memory_order_relaxed);
                continue;
            }
            fatal = network_failure(*failure);
            break;
        }
        if (!std::get<bool>(ready)) {
            continue;
        }
        for (std::size_t drained = 0; drained < kReceiveDrainLimit && !fatal; ++drained) {
            const auto received = source.receive(datagram);
            if (const auto* failure = std::get_if<UdpError>(&received)) {
                if (failure->code == UdpErrorCode::peer_unreachable) {
                    counters.peer_unreachable_events.fetch_add(1, std::memory_order_relaxed);
                    break;
                }
                if (failure->code == UdpErrorCode::datagram_too_large) {
                    counters.remote_packet_rejects.fetch_add(1, std::memory_order_relaxed);
                    continue;
                }
                fatal = network_failure(*failure);
                break;
            }
            const auto size = std::get<std::size_t>(received);
            if (size == 0) {
                break;
            }
            counters.remote_packets.fetch_add(1, std::memory_order_relaxed);
            counters.remote_wire_bytes.fetch_add(size, std::memory_order_relaxed);
            const auto bytes = std::span<const std::byte>(datagram.data(), size);
            auto reassembled = reassembler.push(bytes);
            if (reassembled.status == video::H264ReassemblyStatus::packet_rejected &&
                reassembled.error == video::H264ReassemblyError::ssrc_mismatch &&
                inactive_since(counters.remote_last_stream_ns.load(std::memory_order_acquire), steady_now_ns())) {
                // A quiet peer may have restarted and chosen a new SSRC. Reset stream and viewer
                // history only after the old sender is inactive; an alien packet cannot steal an
                // active session.
                reassembler.reset();
                release_viewer();
                counters.remote_last_stream_ns.store(0, std::memory_order_release);
                counters.remote_stream_resets.fetch_add(1, std::memory_order_relaxed);
                reassembled = reassembler.push(bytes);
            }
            switch (reassembled.status) {
            case video::H264ReassemblyStatus::packet_rejected:
                counters.remote_packet_rejects.fetch_add(1, std::memory_order_relaxed);
                break;
            case video::H264ReassemblyStatus::frame_dropped:
                counters.remote_frame_drops.fetch_add(1, std::memory_order_relaxed);
                if (viewing_last && gate.decoder_open()) {
                    gate.lost();
                    request_keyframe();
                }
                break;
            case video::H264ReassemblyStatus::frame_ready:
                fatal = handle_frame(reassembled.frame);
                break;
            case video::H264ReassemblyStatus::packet_accepted:
                break;
            }
        }
    }
    release_viewer();
    return fatal;
}

void apply_stream_volume(std::span<float> samples, float volume) noexcept {
    if (volume == 1.0F) {
        return;
    }
    for (auto& sample : samples) {
        sample = std::clamp(sample * volume, -1.0F, 1.0F);
    }
}

StreamAudioCaptureBridge::StreamAudioCaptureBridge() : ring_(kStreamAudioFrameSamples * kStreamAudioQueueFrames) {}

void StreamAudioCaptureBridge::on_captured(std::span<const float> samples) noexcept {
    if (samples.empty()) {
        return;
    }
    if (!ring_.write_exact(samples)) {
        dropped_callbacks_.fetch_add(1, std::memory_order_relaxed);
        resync_requested_.store(true, std::memory_order_release);
    }
}

bool StreamAudioCaptureBridge::try_pop(StreamAudioPcmFrame& frame) noexcept {
    if (resync_requested_.exchange(false, std::memory_order_acq_rel)) {
        ring_.discard(ring_.size());
        return false;
    }
    // Keep at most three frames of capture backlog; older audio would only add latency.
    const auto buffered = ring_.size();
    const auto keep = kStreamAudioFrameSamples * 3U;
    if (buffered > keep) {
        const auto stale = ((buffered - keep) / kStreamAudioFrameSamples) * kStreamAudioFrameSamples;
        if (stale != 0) {
            ring_.discard(stale);
        }
    }
    return ring_.read_exact(std::span<float>(frame));
}

std::uint64_t StreamAudioCaptureBridge::dropped_callbacks() const noexcept {
    return dropped_callbacks_.load(std::memory_order_relaxed);
}

StreamAudioRenderBridge::StreamAudioRenderBridge() : ring_(kStreamAudioFrameSamples * kStreamAudioQueueFrames) {}

bool StreamAudioRenderBridge::try_push(const StreamAudioPcmFrame& frame) noexcept {
    if (!ring_.write_exact(std::span<const float>(frame))) {
        request_resync_.store(true, std::memory_order_release);
        return false;
    }
    primed_.store(true, std::memory_order_relaxed);
    return true;
}

void StreamAudioRenderBridge::on_render(std::span<float> samples) noexcept {
    if (request_resync_.exchange(false, std::memory_order_acq_rel)) {
        ring_.discard(ring_.size());
    }
    const auto read = ring_.read(samples);
    if (read < samples.size()) {
        std::fill(samples.begin() + static_cast<std::ptrdiff_t>(read), samples.end(), 0.0F);
        if (primed_.load(std::memory_order_relaxed)) {
            underruns_.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

void StreamAudioRenderBridge::request_resync() noexcept {
    request_resync_.store(true, std::memory_order_release);
}

std::uint64_t StreamAudioRenderBridge::underruns() const noexcept {
    return underruns_.load(std::memory_order_relaxed);
}

StreamAudioSender::StreamAudioSender(const RoomScreenApi& api, CatroRoomRuntimeHandle room,
                                     ScreenTransportCounters& counters)
    : api_(api), room_(room), counters_(counters) {}

StreamAudioSender::~StreamAudioSender() = default;

std::optional<voice::CodecError> StreamAudioSender::start(std::int32_t bitrate, std::uint32_t video_ssrc) {
    voice::EncoderConfig config;
    config.bitrate = bitrate;
    config.channels = static_cast<std::uint32_t>(kStreamAudioChannels);
    config.application = voice::CodecApplication::audio;
    config.complexity = 8;
    config.expected_packet_loss_percent = 5;
    config.inband_fec = true;
    config.vbr = true;
    auto created = voice::Encoder::create(config);
    if (const auto* failure = std::get_if<voice::CodecError>(&created)) {
        counters_.stream_audio_encode_failures.fetch_add(1, std::memory_order_relaxed);
        return *failure;
    }
    encoder_ = std::move(std::get<std::unique_ptr<voice::Encoder>>(created));
    stream_id_ = video_ssrc ^ 0x41554430U; // "AUD0"
    if (stream_id_ == 0) {
        stream_id_ = 1;
    }
    sequence_ = 1;
    timestamp_ = 0;
    return std::nullopt;
}

void StreamAudioSender::send(const StreamAudioPcmFrame& pcm) noexcept {
    if (!encoder_) {
        counters_.stream_audio_encode_failures.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    std::array<std::byte, voice::kMaxOpusPacketBytes> payload{};
    std::array<std::byte, voice::kVoiceHeaderBytes + voice::kVoiceMaxPayloadBytes> datagram{};
    const auto encoded = encoder_->encode(std::span<const float>(pcm), payload);
    const auto* bytes = std::get_if<std::size_t>(&encoded);
    if (bytes == nullptr) {
        counters_.stream_audio_encode_failures.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    const voice::VoicePacketView packet{
        .stream_id = stream_id_,
        .sequence = sequence_,
        .timestamp = timestamp_,
        .payload = std::span<const std::byte>(payload.data(), *bytes),
    };
    const auto serialized = voice::serialize_packet(packet, datagram);
    const auto* size = std::get_if<std::size_t>(&serialized);
    if (size == nullptr) {
        counters_.stream_audio_encode_failures.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (api_.send_stream_audio(room_, datagram.data(), *size) != 0) {
        counters_.stream_audio_packets_sent.fetch_add(1, std::memory_order_relaxed);
    }
    counters_.stream_audio_frames_encoded.fetch_add(1, std::memory_order_relaxed);
    ++sequence_;
    timestamp_ += voice::kFrameSamples;
}

void run_stream_audio_receive_loop(const RoomScreenApi& api, CatroRoomRuntimeHandle room,
                                   ScreenTransportCounters& counters, const std::atomic_bool& stop_requested,
                                   StreamAudioOutput& output) {
    if (room == nullptr) {
        return;
    }
    auto created = voice::Decoder::create(static_cast<std::uint32_t>(kStreamAudioChannels));
    if (std::holds_alternative<voice::CodecError>(created)) {
        counters.remote_stream_audio_decode_failures.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    const auto decoder = std::move(std::get<std::unique_ptr<voice::Decoder>>(created));
    voice::JitterBuffer jitter{static_cast<std::uint16_t>(kStreamAudioJitterPackets)};
    StreamAudioRenderBridge bridge;
    std::array<std::byte, voice::kVoiceHeaderBytes + voice::kVoiceMaxPayloadBytes + 1> datagram{};
    StreamAudioPcmFrame pcm{};
    voice::PlayoutFrame playout;
    bool playout_started = false;
    bool output_started = false;
    bool viewing_last = false;
    auto next_playout = Clock::now();

    const auto reset_playout = [&] {
        jitter.resynchronize();
        bridge.request_resync();
        playout_started = false;
        next_playout = Clock::now();
        counters.remote_stream_audio_active.store(false, std::memory_order_release);
        counters.remote_stream_audio_last_ns.store(0, std::memory_order_release);
    };
    const auto stop_output = [&] {
        if (output_started) {
            output.stop();
            output_started = false;
        }
        reset_playout();
    };
    const auto decode_one = [&]() -> bool {
        const auto kind = jitter.pull(playout);
        if (kind == voice::PlayoutKind::waiting) {
            return true;
        }
        const auto decoded = kind == voice::PlayoutKind::plc
                                 ? decoder->conceal(pcm)
                                 : decoder->decode(playout.payload_view(), pcm, kind == voice::PlayoutKind::fec);
        if (std::holds_alternative<voice::CodecError>(decoded)) {
            counters.remote_stream_audio_decode_failures.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        apply_stream_volume(pcm, counters.remote_stream_volume.load(std::memory_order_relaxed));
        if (!bridge.try_push(pcm)) {
            counters.remote_stream_audio_render_drops.fetch_add(1, std::memory_order_relaxed);
        } else if (counters.echo_sink) {
            counters.echo_sink(pcm);
        }
        counters.remote_stream_audio_frames.fetch_add(1, std::memory_order_relaxed);
        return true;
    };
    const auto consume = [&](std::size_t size, bool viewing) {
        counters.remote_stream_audio_packets.fetch_add(1, std::memory_order_relaxed);
        if (!viewing) {
            return;
        }
        const auto parsed = voice::parse_packet(std::span<const std::byte>(datagram.data(), size));
        const auto* packet = std::get_if<voice::VoicePacketView>(&parsed);
        if (packet == nullptr) {
            counters.remote_stream_audio_decode_failures.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        (void)jitter.push(*packet);
        counters.remote_stream_audio_last_ns.store(steady_now_ns(), std::memory_order_release);
    };

    while (!stop_requested.load(std::memory_order_acquire)) {
        const bool viewing = counters.remote_viewing_enabled.load(std::memory_order_acquire);
        if (viewing != viewing_last) {
            if (!viewing) {
                stop_output();
            } else {
                jitter.resynchronize();
                bridge.request_resync();
                playout_started = false;
                next_playout = Clock::now();
            }
            viewing_last = viewing;
        }
        const auto received = api.receive_stream_audio(room, datagram.data(), datagram.size(), 20);
        if (received < 0) {
            if (api.snapshot(room).state == CATRO_ROOM_FAILED) {
                break;
            }
            continue;
        }
        if (received > 0) {
            consume(static_cast<std::size_t>(received), viewing);
            for (std::size_t drained = 1; drained < kStreamAudioReceiveDrainLimit; ++drained) {
                const auto more = api.receive_stream_audio(room, datagram.data(), datagram.size(), 0);
                if (more <= 0) {
                    break;
                }
                consume(static_cast<std::size_t>(more), viewing);
            }
        }
        if (!viewing) {
            continue;
        }
        if (!playout_started) {
            if (jitter.peek() == voice::PlayoutKind::waiting) {
                continue;
            }
            if (!decode_one()) {
                reset_playout();
                continue;
            }
            if (!output_started) {
                if (!output.start(bridge)) {
                    counters.remote_stream_audio_render_drops.fetch_add(1, std::memory_order_relaxed);
                    reset_playout();
                    continue;
                }
                output_started = true;
            }
            playout_started = true;
            next_playout = Clock::now() + kStreamAudioFramePeriod;
            counters.remote_stream_audio_active.store(true, std::memory_order_release);
            continue;
        }
        auto now = Clock::now();
        if (now - next_playout >= kStreamAudioFramePeriod * 3) {
            reset_playout();
            continue;
        }
        for (int caught_up = 0; now >= next_playout && caught_up < 3; ++caught_up) {
            if (!decode_one()) {
                reset_playout();
                break;
            }
            next_playout += kStreamAudioFramePeriod;
            now = Clock::now();
        }
        if (inactive_since(counters.remote_stream_audio_last_ns.load(std::memory_order_acquire), steady_now_ns())) {
            stop_output();
        }
    }
    if (output_started) {
        output.stop();
    }
    counters.remote_stream_audio_active.store(false, std::memory_order_release);
}

} // namespace catro::screen
