#include <catro/rtc/room_mesh_transport.hpp>

#include <nlohmann/json.hpp>
#include <rtc/rtc.hpp>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <chrono>
#include <exception>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace catro::rtc {
namespace {

using Json = nlohmann::json;

constexpr std::size_t kMaximumPeers = 64;
constexpr std::size_t kMaximumMediaDatagram = 2048;
constexpr std::size_t kMaximumSignalMessage = 64U * 1024U;
constexpr std::string_view kVoiceLabel = "catro.voice.v1";
constexpr std::string_view kVideoLabel = "catro.video.v1";
constexpr std::string_view kStreamAudioLabel =
    "catro.stream-audio.v1";

[[nodiscard]] bool starts_with(
    std::string_view value,
    std::string_view prefix) noexcept {
    return value.size() >= prefix.size() &&
           value.substr(0, prefix.size()) == prefix;
}

[[nodiscard]] bool has_turn(
    const std::vector<std::string>& urls) noexcept {
    return std::ranges::any_of(
        urls,
        [](const auto& url) {
            return starts_with(url, "turn:") ||
                   starts_with(url, "turns:");
        });
}

[[nodiscard]] bool valid_id(
    std::string_view value) noexcept {
    return !value.empty() && value.size() <= 128;
}

[[nodiscard]] std::optional<RoomTransportError>
validate(const RoomMeshConfig& config) {
    const bool secure =
        starts_with(config.signaling_url, "wss://");
    const bool explicitly_insecure =
        config.allow_insecure_signaling &&
        starts_with(config.signaling_url, "ws://");

    if ((!secure && !explicitly_insecure) ||
        config.signaling_url.size() > 2048 ||
        config.access_token.empty() ||
        config.access_token.size() > 8192 ||
        !valid_id(config.server_id) ||
        !valid_id(config.channel_id) ||
        !valid_id(config.user_id) ||
        config.max_peers == 0 ||
        config.max_peers > kMaximumPeers ||
        config.ice_server_urls.empty() ||
        (!config.allow_no_turn &&
         !has_turn(config.ice_server_urls))) {
        return RoomTransportError{
            RoomTransportErrorCode::invalid_config,
            "invalid production RTC room configuration"};
    }

    return std::nullopt;
}

} // namespace

struct RoomMeshTransport::Impl {
    struct Peer {
        std::string id;
        std::shared_ptr<::rtc::PeerConnection> connection;
        std::shared_ptr<::rtc::DataChannel> voice;
        std::shared_ptr<::rtc::DataChannel> video;
        std::shared_ptr<::rtc::DataChannel> stream_audio;
    };

    [[nodiscard]] std::optional<RoomTransportError> start(
        RoomMeshConfig next_config,
        RoomTransportCallbacks next_callbacks) {
        stop();

        if (const auto failure =
                validate(next_config)) {
            return failure;
        }

        {
            std::scoped_lock lock(mutex_);
            config_ = std::move(next_config);
            callbacks_ = std::move(next_callbacks);
            stopping_.store(
                false, std::memory_order_release);
            state_.store(
                RoomTransportState::connecting,
                std::memory_order_release);
        }
        publish_state(RoomTransportState::connecting);

        try {
            ::rtc::WebSocket::Configuration ws_config;
            ws_config.connectionTimeout =
                std::chrono::seconds{10};
            ws_config.pingInterval =
                std::chrono::seconds{15};
            ws_config.maxOutstandingPings = 3;
            ws_config.maxMessageSize =
                kMaximumSignalMessage;

            auto websocket =
                std::make_shared<::rtc::WebSocket>(
                    ws_config);

            websocket->onOpen(
                [this] {
                    send_join();
                });
            websocket->onMessage(
                [this](::rtc::message_variant message) {
                    if (const auto* text =
                            std::get_if<std::string>(
                                &message)) {
                        handle_signal(*text);
                    }
                });
            websocket->onError(
                [this](std::string error) {
                    fail(
                        RoomTransportErrorCode::
                            signaling_failed,
                        error);
                });
            websocket->onClosed(
                [this] {
                    if (!stopping_.load(
                            std::memory_order_acquire)) {
                        fail(
                            RoomTransportErrorCode::
                                signaling_failed,
                            "signaling connection closed");
                    }
                });

            std::string url;
            {
                std::scoped_lock lock(mutex_);
                websocket_ = websocket;
                url = config_.signaling_url;
            }
            websocket->open(url);
        } catch (const std::exception& error) {
            stop();
            return RoomTransportError{
                RoomTransportErrorCode::signaling_failed,
                error.what()};
        } catch (...) {
            stop();
            return RoomTransportError{
                RoomTransportErrorCode::signaling_failed,
                "RTC signaling startup failed"};
        }

        return std::nullopt;
    }

    void stop() noexcept {
        stopping_.store(
            true, std::memory_order_release);

        std::shared_ptr<::rtc::WebSocket> websocket;
        std::vector<std::shared_ptr<Peer>> peers;
        RoomTransportCallbacks callbacks;

        {
            std::scoped_lock lock(mutex_);
            websocket = std::move(websocket_);
            peers.reserve(peers_.size());
            for (auto& [id, peer] : peers_) {
                (void)id;
                peers.push_back(peer);
            }
            peers_.clear();
            callbacks = callbacks_;
        }

        for (const auto& peer : peers) {
            close_peer(peer);
        }

        if (websocket) {
            try {
                websocket->resetCallbacks();
                websocket->close();
            } catch (...) {
            }
        }

        state_.store(
            RoomTransportState::idle,
            std::memory_order_release);
        if (callbacks.on_state) {
            try {
                callbacks.on_state(
                    RoomTransportState::idle);
            } catch (...) {
            }
        }
    }

    [[nodiscard]] std::size_t send_voice(
        std::span<const std::byte> datagram) noexcept {
        return broadcast(kVoiceLabel, datagram);
    }

    [[nodiscard]] std::size_t send_video(
        std::span<const std::byte> datagram) noexcept {
        return broadcast(kVideoLabel, datagram);
    }

    [[nodiscard]] std::size_t send_stream_audio(
        std::span<const std::byte> datagram) noexcept {
        return broadcast(kStreamAudioLabel, datagram);
    }

    [[nodiscard]] std::size_t broadcast(
        std::string_view label,
        std::span<const std::byte> datagram) noexcept {
        if (datagram.empty() ||
            datagram.size() > kMaximumMediaDatagram ||
            state_.load(std::memory_order_acquire) !=
                RoomTransportState::joined) {
            return 0;
        }

        std::vector<
            std::shared_ptr<::rtc::DataChannel>>
            channels;
        {
            std::scoped_lock lock(mutex_);
            channels.reserve(peers_.size());
            for (const auto& [id, peer] : peers_) {
                (void)id;
                const auto channel =
                    label == kVoiceLabel
                        ? peer->voice
                        : (label == kVideoLabel
                               ? peer->video
                               : peer->stream_audio);
                if (channel && channel->isOpen()) {
                    channels.push_back(channel);
                }
            }
        }

        std::size_t sent = 0;
        for (const auto& channel : channels) {
            try {
                if (channel->send(
                        reinterpret_cast<
                            const ::rtc::byte*>(
                            datagram.data()),
                        datagram.size())) {
                    ++sent;
                }
            } catch (...) {
                // Backpressure/failure on one peer must not block healthy room peers.
            }
        }
        return sent;
    }

    [[nodiscard]] std::shared_ptr<Peer>
    find_peer(std::string_view id) const {
        std::scoped_lock lock(mutex_);
        const auto found =
            peers_.find(std::string{id});
        return found == peers_.end()
                   ? nullptr
                   : found->second;
    }

    [[nodiscard]] std::shared_ptr<Peer>
    ensure_peer(
        const std::string& peer_id,
        bool initiator) {
        if (peer_id.empty()) {
            return nullptr;
        }

        {
            std::scoped_lock lock(mutex_);
            if (peer_id == config_.user_id) {
                return nullptr;
            }
            if (const auto found =
                    peers_.find(peer_id);
                found != peers_.end()) {
                return found->second;
            }
            if (peers_.size() >=
                config_.max_peers) {
                return nullptr;
            }
        }

        ::rtc::Configuration rtc_config;
        {
            std::scoped_lock lock(mutex_);
            for (const auto& url :
                 config_.ice_server_urls) {
                rtc_config.iceServers.emplace_back(
                    url);
            }
        }
        rtc_config.disableAutoNegotiation = true;
        rtc_config.mtu = 1200;
        rtc_config.maxMessageSize =
            kMaximumMediaDatagram;

        auto peer = std::make_shared<Peer>();
        peer->id = peer_id;
        peer->connection =
            std::make_shared<
                ::rtc::PeerConnection>(
                rtc_config);

        const auto connection = peer->connection;
        connection->onLocalDescription(
            [this, peer_id](
                ::rtc::Description description) {
                Json signal{
                    {"type", "signal"},
                    {"to", peer_id},
                    {"kind", description.typeString()},
                    {"sdp",
                     static_cast<std::string>(
                         description)},
                };
                send_json(signal);
            });
        connection->onLocalCandidate(
            [this, peer_id](
                ::rtc::Candidate candidate) {
                Json signal{
                    {"type", "signal"},
                    {"to", peer_id},
                    {"kind", "candidate"},
                    {"candidate",
                     candidate.candidate()},
                    {"mid", candidate.mid()},
                };
                send_json(signal);
            });
        connection->onDataChannel(
            [this, weak =
                       std::weak_ptr<Peer>{peer}](
                std::shared_ptr<::rtc::DataChannel>
                    channel) {
                if (const auto locked =
                        weak.lock()) {
                    bind_channel(locked, channel);
                }
            });
        connection->onStateChange(
            [this, peer_id](
                ::rtc::PeerConnection::State state) {
                if (state ==
                        ::rtc::PeerConnection::State::
                            Failed ||
                    state ==
                        ::rtc::PeerConnection::State::
                            Closed) {
                    remove_peer(peer_id);
                }
            });

        if (initiator) {
            ::rtc::DataChannelInit channel_config;
            channel_config.reliability.unordered =
                true;
            channel_config.reliability
                .maxRetransmits = 0U;

            bind_channel(
                peer,
                connection->createDataChannel(
                    std::string{kVoiceLabel},
                    channel_config));
            bind_channel(
                peer,
                connection->createDataChannel(
                    std::string{kVideoLabel},
                    channel_config));
            bind_channel(
                peer,
                connection->createDataChannel(
                    std::string{kStreamAudioLabel},
                    channel_config));
        }

        {
            std::scoped_lock lock(mutex_);
            if (stopping_.load(
                    std::memory_order_acquire)) {
                close_peer(peer);
                return nullptr;
            }
            const auto [it, inserted] =
                peers_.emplace(peer_id, peer);
            if (!inserted) {
                close_peer(peer);
                return it->second;
            }
        }

        if (initiator) {
            try {
                connection->setLocalDescription(
                    ::rtc::Description::Type::Offer);
            } catch (const std::exception& error) {
                remove_peer(peer_id);
                fail(
                    RoomTransportErrorCode::rtc_failed,
                    error.what());
                return nullptr;
            }
        }

        return peer;
    }

    void bind_channel(
        const std::shared_ptr<Peer>& peer,
        const std::shared_ptr<::rtc::DataChannel>&
            channel) {
        if (!peer || !channel) {
            return;
        }

        const auto label = channel->label();
        if (label != kVoiceLabel &&
            label != kVideoLabel &&
            label != kStreamAudioLabel) {
            channel->close();
            return;
        }

        {
            std::scoped_lock lock(mutex_);
            if (label == kVoiceLabel) {
                peer->voice = channel;
            } else if (label == kVideoLabel) {
                peer->video = channel;
            } else {
                peer->stream_audio = channel;
            }
        }

        const bool voice = label == kVoiceLabel;
        const bool video = label == kVideoLabel;
        const auto peer_id = peer->id;
        channel->onMessage(
            [this, peer_id, voice](
                ::rtc::binary data) {
                if (data.empty() ||
                    data.size() >
                        kMaximumMediaDatagram) {
                    return;
                }

                RoomTransportCallbacks callbacks;
                {
                    std::scoped_lock lock(mutex_);
                    callbacks = callbacks_;
                }

                const auto bytes =
                    std::span<const std::byte>(
                        reinterpret_cast<
                            const std::byte*>(
                            data.data()),
                        data.size());
                try {
                    if (voice &&
                        callbacks.on_voice_datagram) {
                        callbacks.on_voice_datagram(
                            peer_id, bytes);
                    } else if (
                        video &&
                        callbacks.on_video_datagram) {
                        callbacks.on_video_datagram(
                            peer_id, bytes);
                    } else if (
                        !voice && !video &&
                        callbacks.on_stream_audio_datagram) {
                        callbacks.on_stream_audio_datagram(
                            peer_id, bytes);
                    }
                } catch (...) {
                }
            },
            [](std::string) {});
    }

    void handle_signal(
        std::string_view text) noexcept {
        try {
            if (text.empty() ||
                text.size() >
                    kMaximumSignalMessage) {
                return;
            }
            const auto message =
                Json::parse(text);
            const auto type =
                message.value("type", "");

            if (type == "joined") {
                state_.store(
                    RoomTransportState::joined,
                    std::memory_order_release);
                publish_state(
                    RoomTransportState::joined);

                if (const auto peers =
                        message.find("peers");
                    peers != message.end() &&
                    peers->is_array()) {
                    std::string local;
                    {
                        std::scoped_lock lock(mutex_);
                        local = config_.user_id;
                    }
                    for (const auto& value : *peers) {
                        if (!value.is_string()) {
                            continue;
                        }
                        const auto peer_id =
                            value.get<std::string>();
                        if (local < peer_id) {
                            (void)ensure_peer(
                                peer_id, true);
                        }
                    }
                }
                return;
            }

            if (type == "peer_joined") {
                const auto peer_id =
                    message.value("peer_id", "");
                std::string local;
                {
                    std::scoped_lock lock(mutex_);
                    local = config_.user_id;
                }
                if (!peer_id.empty() &&
                    local < peer_id) {
                    (void)ensure_peer(
                        peer_id, true);
                }
                return;
            }

            if (type == "peer_left") {
                remove_peer(
                    message.value("peer_id", ""));
                return;
            }

            if (type == "error") {
                fail(
                    RoomTransportErrorCode::
                        signaling_failed,
                    message.value(
                        "message",
                        "signaling server error"));
                return;
            }

            if (type != "signal") {
                return;
            }

            const auto from =
                message.value("from", "");
            const auto kind =
                message.value("kind", "");
            if (from.empty() || kind.empty()) {
                return;
            }

            if (kind == "offer") {
                const auto peer =
                    ensure_peer(from, false);
                if (!peer) {
                    return;
                }
                peer->connection
                    ->setRemoteDescription(
                        ::rtc::Description{
                            message.value("sdp", ""),
                            "offer"});
                peer->connection
                    ->setLocalDescription(
                        ::rtc::Description::Type::
                            Answer);
                return;
            }

            const auto peer = find_peer(from);
            if (!peer) {
                return;
            }

            if (kind == "answer") {
                peer->connection
                    ->setRemoteDescription(
                        ::rtc::Description{
                            message.value("sdp", ""),
                            "answer"});
            } else if (kind == "candidate") {
                peer->connection
                    ->addRemoteCandidate(
                        ::rtc::Candidate{
                            message.value(
                                "candidate", ""),
                            message.value(
                                "mid", "")});
            }
        } catch (const std::exception& error) {
            fail(
                RoomTransportErrorCode::rtc_failed,
                error.what());
        } catch (...) {
            fail(
                RoomTransportErrorCode::rtc_failed,
                "RTC signaling message failed");
        }
    }

    void send_join() noexcept {
        RoomMeshConfig config;
        {
            std::scoped_lock lock(mutex_);
            config = config_;
        }

        send_json(
            Json{
                {"type", "join"},
                {"token", config.access_token},
                {"server_id", config.server_id},
                {"channel_id", config.channel_id},
                {"peer_id", config.user_id},
                {"protocol", 1},
            });
    }

    void send_json(const Json& message) noexcept {
        std::shared_ptr<::rtc::WebSocket> websocket;
        {
            std::scoped_lock lock(mutex_);
            websocket = websocket_;
        }
        if (!websocket || !websocket->isOpen()) {
            return;
        }

        try {
            const auto encoded = message.dump();
            if (encoded.size() <=
                kMaximumSignalMessage) {
                (void)websocket->send(encoded);
            }
        } catch (...) {
        }
    }

    void remove_peer(
        std::string_view peer_id) noexcept {
        if (peer_id.empty()) {
            return;
        }
        std::shared_ptr<Peer> peer;
        {
            std::scoped_lock lock(mutex_);
            const auto found =
                peers_.find(std::string{peer_id});
            if (found == peers_.end()) {
                return;
            }
            peer = std::move(found->second);
            peers_.erase(found);
        }
        close_peer(peer);
    }

    static void close_peer(
        const std::shared_ptr<Peer>& peer) noexcept {
        if (!peer) {
            return;
        }
        try {
            if (peer->voice) {
                peer->voice->resetCallbacks();
                peer->voice->close();
            }
            if (peer->video) {
                peer->video->resetCallbacks();
                peer->video->close();
            }
            if (peer->stream_audio) {
                peer->stream_audio->resetCallbacks();
                peer->stream_audio->close();
            }
            if (peer->connection) {
                peer->connection->resetCallbacks();
                peer->connection->close();
            }
        } catch (...) {
        }
    }

    void publish_state(
        RoomTransportState state) noexcept {
        RoomTransportCallbacks callbacks;
        {
            std::scoped_lock lock(mutex_);
            callbacks = callbacks_;
        }
        if (callbacks.on_state) {
            try {
                callbacks.on_state(state);
            } catch (...) {
            }
        }
    }

    void fail(
        RoomTransportErrorCode code,
        std::string_view message) noexcept {
        RoomTransportCallbacks callbacks;
        {
            std::scoped_lock lock(mutex_);
            fail_locked(code, message);
            callbacks = callbacks_;
        }
        if (callbacks.on_error) {
            try {
                callbacks.on_error(message);
            } catch (...) {
            }
        }
        if (callbacks.on_state) {
            try {
                callbacks.on_state(
                    RoomTransportState::failed);
            } catch (...) {
            }
        }
    }

    void fail_locked(
        RoomTransportErrorCode,
        std::string_view) noexcept {
        state_.store(
            RoomTransportState::failed,
            std::memory_order_release);
    }

    [[nodiscard]] std::size_t peer_count()
        const noexcept {
        std::scoped_lock lock(mutex_);
        return peers_.size();
    }

    mutable std::mutex mutex_;
    RoomMeshConfig config_;
    RoomTransportCallbacks callbacks_;
    std::shared_ptr<::rtc::WebSocket> websocket_;
    std::unordered_map<
        std::string,
        std::shared_ptr<Peer>>
        peers_;
    std::atomic<RoomTransportState> state_{
        RoomTransportState::idle};
    std::atomic_bool stopping_{true};
};

RoomMeshTransport::RoomMeshTransport()
    : impl_(std::make_unique<Impl>()) {}

RoomMeshTransport::~RoomMeshTransport() {
    impl_->stop();
}

std::optional<RoomTransportError>
RoomMeshTransport::start(
    RoomMeshConfig config,
    RoomTransportCallbacks callbacks) {
    return impl_->start(
        std::move(config),
        std::move(callbacks));
}

void RoomMeshTransport::stop() noexcept {
    impl_->stop();
}

std::size_t RoomMeshTransport::send_voice(
    std::span<const std::byte> datagram) noexcept {
    return impl_->send_voice(datagram);
}

std::size_t RoomMeshTransport::send_video(
    std::span<const std::byte> datagram) noexcept {
    return impl_->send_video(datagram);
}

std::size_t RoomMeshTransport::send_stream_audio(
    std::span<const std::byte> datagram) noexcept {
    return impl_->send_stream_audio(datagram);
}

RoomTransportState RoomMeshTransport::state()
    const noexcept {
    return impl_->state_.load(
        std::memory_order_acquire);
}

std::size_t RoomMeshTransport::peer_count()
    const noexcept {
    return impl_->peer_count();
}

} // namespace catro::rtc
