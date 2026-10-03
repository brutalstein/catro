#include <catro/rtc/room_mesh_transport.hpp>
#include <catro/rtc/room_media_policy.hpp>

#include <nlohmann/json.hpp>
#include <rtc/rtc.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace catro::rtc {
namespace {

using Json = nlohmann::json;

constexpr std::size_t kMaximumPeers = 4;
constexpr std::size_t kMaximumMediaDatagram = 2048;
constexpr std::size_t kMaximumSignalMessage = 64U * 1024U;
constexpr std::size_t kVoiceBufferedBytes = 8U * 1024U;
constexpr std::size_t kStreamAudioBufferedBytes = 16U * 1024U;
constexpr std::size_t kVideoBufferedBytes = 128U * 1024U;
constexpr std::string_view kVoiceLabel = "catro.voice.v1";
constexpr std::string_view kVideoLabel = "catro.video.v1";
constexpr std::string_view kStreamAudioLabel =
    "catro.stream-audio.v1";
// A dropped signaling socket is rejoined with backoff for this long. It outlasts the server's 45 s
// read timeout, which keeps a half-open old socket (and so this peer id) registered.
constexpr auto kReconnectWindow = std::chrono::seconds{60};
constexpr auto kReconnectAttemptTimeout = std::chrono::seconds{12};
constexpr auto kReconnectMaxDelay = std::chrono::milliseconds{4000};

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
            joined_once_ = false;
            lost_ = false;
            reclaim_screen_ = false;
            state_.store(
                RoomTransportState::connecting,
                std::memory_order_release);
        }
        publish_state(RoomTransportState::connecting);

        try {
            open_signaling();
            supervisor_ = std::thread([this] { supervise(); });
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
        {
            std::scoped_lock lock(mutex_);
            stopping_.store(
                true, std::memory_order_release);
        }
        reconnect_cv_.notify_all();
        // Joined first so no rejoin attempt can install a new socket after this point.
        if (supervisor_.joinable()) {
            supervisor_.join();
        }

        std::shared_ptr<::rtc::WebSocket> websocket;
        std::vector<std::shared_ptr<Peer>> peers;
        RoomTransportCallbacks callbacks;
        bool cleared_screen_owner = false;

        {
            std::scoped_lock lock(mutex_);
            websocket = std::move(websocket_);
            peers.reserve(peers_.size());
            for (auto& [id, peer] : peers_) {
                (void)id;
                peers.push_back(peer);
            }
            peers_.clear();
            cleared_screen_owner =
                !screen_owner_.empty();
            screen_owner_.clear();
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

        if (cleared_screen_owner &&
            callbacks.on_screen_owner) {
            try {
                callbacks.on_screen_owner({});
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

    [[nodiscard]] bool claim_screen() noexcept {
        if (state_.load(std::memory_order_acquire) !=
            RoomTransportState::joined) {
            return false;
        }
        return send_json(
            Json{{"type", "screen_claim"}});
    }

    void release_screen() noexcept {
        if (state_.load(std::memory_order_acquire) ==
            RoomTransportState::joined) {
            (void)send_json(
                Json{{"type", "screen_release"}});
        }
    }

    [[nodiscard]] std::string screen_owner()
        const {
        std::scoped_lock lock(mutex_);
        return screen_owner_;
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

        // Media send is a hot path: video can call this hundreds of times per second. Snapshot
        // shared channel handles into fixed stack storage instead of allocating a vector for every
        // RTP/Opus datagram. The room itself is already hard-bounded by kMaximumPeers.
        std::array<
            std::shared_ptr<::rtc::DataChannel>,
            kMaximumPeers>
            channels{};
        std::size_t channel_count = 0;
        {
            std::scoped_lock lock(mutex_);
            for (const auto& [id, peer] : peers_) {
                (void)id;
                const auto channel =
                    label == kVoiceLabel
                        ? peer->voice
                        : (label == kVideoLabel
                               ? peer->video
                               : peer->stream_audio);
                if (channel &&
                    channel->isOpen() &&
                    channel_count < channels.size()) {
                    channels[channel_count++] =
                        channel;
                }
            }
        }

        const auto buffered_limit =
            label == kVoiceLabel
                ? kVoiceBufferedBytes
                : (label == kVideoLabel
                       ? kVideoBufferedBytes
                       : kStreamAudioBufferedBytes);

        std::size_t sent = 0;
        for (std::size_t index = 0;
             index < channel_count;
             ++index) {
            const auto& channel = channels[index];
            try {
                // Unordered/unreliable SCTP prevents retransmission head-of-line blocking, but an
                // application can still enqueue faster than a path can drain. Bound that queue
                // explicitly; stale realtime media is less valuable than increasing latency.
                if (channel->bufferedAmount() >
                    buffered_limit) {
                    continue;
                }
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
            [this, peer_id, initiator](
                ::rtc::PeerConnection::State state) {
                using State = ::rtc::PeerConnection::State;
                if (state != State::Failed &&
                    state != State::Closed) {
                    return;
                }
                // remove_peer resets this callback, so nothing captured is used after it.
                auto* const self = this;
                const auto id = peer_id;
                const bool reoffer =
                    initiator && state == State::Failed;
                if (self->remove_peer(id) && reoffer &&
                    !self->stopping_.load(
                        std::memory_order_acquire) &&
                    self->state_.load(
                        std::memory_order_acquire) ==
                        RoomTransportState::joined) {
                    try {
                        (void)self->ensure_peer(id, true);
                    } catch (...) {
                    }
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
            [this, peer_id, voice, video](
                ::rtc::binary data) {
                if (data.empty() ||
                    data.size() >
                        kMaximumMediaDatagram) {
                    return;
                }

                RoomTransportCallbacks callbacks;
                bool media_allowed = voice;
                {
                    std::scoped_lock lock(mutex_);
                    callbacks = callbacks_;
                    if (!voice) {
                        media_allowed =
                            screen_media_allowed(
                                screen_owner_,
                                peer_id);
                    }
                }
                if (!media_allowed) {
                    return;
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
                bool reclaim = false;
                {
                    std::scoped_lock lock(mutex_);
                    joined_once_ = true;
                    reclaim = std::exchange(
                        reclaim_screen_, false);
                    state_.store(
                        RoomTransportState::joined,
                        std::memory_order_release);
                }
                reconnect_cv_.notify_all();
                const auto owner =
                    message.value("screen_owner", "");
                if (reclaim && owner.empty()) {
                    // The drop released this share; claim it back so viewers keep watching.
                    (void)send_json(
                        Json{{"type", "screen_claim"}});
                } else {
                    set_screen_owner(owner);
                }
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

            if (type == "screen_state" ||
                type == "screen_busy") {
                set_screen_owner(
                    message.value(
                        "screen_owner", ""));
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
                {
                    std::scoped_lock lock(mutex_);
                    if (joined_once_) {
                        return;
                    }
                }
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
                remove_peer(from);
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

    // Opens a signaling socket and makes it current. Events of replaced sockets are ignored.
    void open_signaling() {
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
        std::uint64_t generation = 0;
        std::shared_ptr<::rtc::WebSocket> previous;
        std::string url;
        {
            std::scoped_lock lock(mutex_);
            generation = ++generation_;
            previous = std::exchange(websocket_, websocket);
            url = config_.signaling_url;
        }
        if (previous) {
            try {
                previous->resetCallbacks();
                previous->close();
            } catch (...) {
            }
        }

        websocket->onOpen(
            [this, generation] {
                if (current(generation)) {
                    send_join();
                }
            });
        websocket->onMessage(
            [this, generation](::rtc::message_variant message) {
                if (const auto* text =
                        std::get_if<std::string>(
                            &message);
                    text != nullptr && current(generation)) {
                    handle_signal(*text);
                }
            });
        websocket->onError(
            [this, generation](std::string error) {
                connection_lost(generation, error);
            });
        websocket->onClosed(
            [this, generation] {
                connection_lost(
                    generation,
                    "signaling connection closed");
            });
        websocket->open(url);
    }

    [[nodiscard]] bool current(
        std::uint64_t generation) const noexcept {
        std::scoped_lock lock(mutex_);
        return generation == generation_;
    }

    // Before the first join a lost socket fails the room. After it, the room drops its peers and
    // the supervisor rejoins, like Discord's "RTC Connecting"; peers are rebuilt from the new join.
    void connection_lost(
        std::uint64_t generation,
        std::string_view message) noexcept {
        std::vector<std::shared_ptr<Peer>> peers;
        bool rejoin = false;
        {
            std::scoped_lock lock(mutex_);
            if (generation != generation_ ||
                stopping_.load(std::memory_order_acquire) ||
                state_.load(std::memory_order_acquire) ==
                    RoomTransportState::failed) {
                return;
            }
            rejoin = joined_once_;
            if (rejoin) {
                if (state_.load(std::memory_order_acquire) ==
                    RoomTransportState::joined) {
                    reclaim_screen_ =
                        !screen_owner_.empty() &&
                        screen_owner_ == config_.user_id;
                }
                lost_ = true;
                state_.store(
                    RoomTransportState::connecting,
                    std::memory_order_release);
                for (auto& [id, peer] : peers_) {
                    (void)id;
                    peers.push_back(std::move(peer));
                }
                peers_.clear();
            }
        }
        if (!rejoin) {
            fail(
                RoomTransportErrorCode::signaling_failed,
                message);
            return;
        }
        for (const auto& peer : peers) {
            close_peer(peer);
        }
        publish_state(RoomTransportState::connecting);
        reconnect_cv_.notify_all();
    }

    void supervise() noexcept {
        std::unique_lock lock(mutex_);
        const auto stopping = [this] {
            return stopping_.load(std::memory_order_acquire);
        };
        while (true) {
            reconnect_cv_.wait(
                lock, [&] { return stopping() || lost_; });
            if (stopping()) {
                return;
            }
            const auto deadline =
                std::chrono::steady_clock::now() +
                kReconnectWindow;
            auto delay = std::chrono::milliseconds{250};
            while (lost_) {
                if (reconnect_cv_.wait_for(
                        lock, delay, stopping)) {
                    return;
                }
                if (std::chrono::steady_clock::now() >=
                    deadline) {
                    lost_ = false;
                    lock.unlock();
                    fail(
                        RoomTransportErrorCode::
                            signaling_failed,
                        "signaling connection lost");
                    lock.lock();
                    break;
                }
                delay = std::min(
                    delay * 2, kReconnectMaxDelay);
                lost_ = false;
                lock.unlock();
                try {
                    open_signaling();
                } catch (...) {
                }
                lock.lock();
                // The attempt ends when it joins or its socket drops (which sets lost_ again).
                reconnect_cv_.wait_for(
                    lock, kReconnectAttemptTimeout, [&] {
                        return stopping() || lost_ ||
                               state_.load(
                                   std::memory_order_acquire) ==
                                   RoomTransportState::joined;
                    });
                if (stopping()) {
                    return;
                }
                if (state_.load(std::memory_order_acquire) !=
                    RoomTransportState::joined) {
                    lost_ = true;
                }
            }
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

    bool send_json(const Json& message) noexcept {
        std::shared_ptr<::rtc::WebSocket> websocket;
        {
            std::scoped_lock lock(mutex_);
            websocket = websocket_;
        }
        if (!websocket || !websocket->isOpen()) {
            return false;
        }

        try {
            const auto encoded = message.dump();
            if (encoded.size() <=
                kMaximumSignalMessage) {
                return websocket->send(encoded);
            }
        } catch (...) {
        }
        return false;
    }

    void set_screen_owner(
        std::string next_owner) noexcept {
        RoomTransportCallbacks callbacks;
        std::string published_owner;
        {
            std::scoped_lock lock(mutex_);
            if (screen_owner_ == next_owner) {
                return;
            }
            screen_owner_ = std::move(next_owner);
            published_owner = screen_owner_;
            callbacks = callbacks_;
        }
        if (callbacks.on_screen_owner) {
            try {
                callbacks.on_screen_owner(
                    published_owner);
            } catch (...) {
            }
        }
    }

    bool remove_peer(
        std::string_view peer_id) noexcept {
        if (peer_id.empty()) {
            return false;
        }
        std::shared_ptr<Peer> peer;
        {
            std::scoped_lock lock(mutex_);
            const auto found =
                peers_.find(std::string{peer_id});
            if (found == peers_.end()) {
                return false;
            }
            peer = std::move(found->second);
            peers_.erase(found);
        }
        close_peer(peer);
        return true;
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
    std::string screen_owner_;
    std::atomic<RoomTransportState> state_{
        RoomTransportState::idle};
    std::atomic_bool stopping_{true};
    // Rejoin state, guarded by mutex_.
    std::condition_variable reconnect_cv_;
    std::thread supervisor_;
    std::uint64_t generation_ = 0;
    bool joined_once_ = false;
    bool lost_ = false;
    bool reclaim_screen_ = false;
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

bool RoomMeshTransport::claim_screen() noexcept {
    return impl_->claim_screen();
}

void RoomMeshTransport::release_screen() noexcept {
    impl_->release_screen();
}

std::string RoomMeshTransport::screen_owner() const {
    return impl_->screen_owner();
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
