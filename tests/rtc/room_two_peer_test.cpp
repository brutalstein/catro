#include <catro/rtc/room_mesh_transport.hpp>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <rtc/rtc.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <variant>
#include <vector>

// Two to five people over real WebRTC (ICE, DTLS, SCTP data channels) on this machine, through
// an in-process relay that follows services/signaling: they talk, one goes live, the other watches
// the stream and hears its audio, and loss recovery reaches only the sharer.
using namespace catro::rtc;
using namespace std::chrono_literals;
using Json = nlohmann::json;

namespace {

// Destroy the peers and relay before waiting for the library's background work to finish.
struct RtcScope {
    ~RtcScope() {
        CHECK(::rtc::Cleanup().wait_for(10s) == std::future_status::ready);
    }
};

class Relay {
public:
    Relay() : server_(configuration()) {
        server_.onClient([this](std::shared_ptr<::rtc::WebSocket> client) {
            auto id = std::make_shared<std::string>();
            std::weak_ptr<::rtc::WebSocket> weak = client;
            client->onMessage([this, id, weak](::rtc::message_variant data) {
                const auto* text = std::get_if<std::string>(&data);
                const auto socket = weak.lock();
                if (text != nullptr && socket) {
                    handle(*id, socket, Json::parse(*text));
                }
            });
            std::scoped_lock lock(mutex_);
            sockets_.push_back(std::move(client));
        });
    }

    ~Relay() {
        server_.stop();
        std::vector<std::shared_ptr<::rtc::WebSocket>> sockets;
        {
            std::scoped_lock lock(mutex_);
            sockets.swap(sockets_);
            peers_.clear();
        }
        for (const auto& socket : sockets) {
            socket->resetCallbacks();
            socket->close();
        }
    }

    Relay(const Relay&) = delete;
    Relay& operator=(const Relay&) = delete;

    [[nodiscard]] std::uint16_t port() const { return server_.port(); }

private:
    static ::rtc::WebSocketServer::Configuration configuration() {
        ::rtc::WebSocketServer::Configuration config;
        config.port = 0;
        config.bindAddress = "127.0.0.1";
        return config;
    }

    void broadcast(const Json& message, std::string_view except = {}) {
        for (const auto& [peer, socket] : peers_) {
            if (peer != except) {
                socket->send(message.dump());
            }
        }
    }

    void handle(std::string& id, const std::shared_ptr<::rtc::WebSocket>& socket, const Json& message) {
        const auto type = message.value("type", "");
        std::scoped_lock lock(mutex_);
        if (type == "join") {
            id = message.value("peer_id", "");
            auto peers = Json::array();
            for (const auto& entry : peers_) {
                peers.push_back(entry.first);
            }
            peers_[id] = socket;
            Json joined{{"type", "joined"}, {"peers", peers}, {"protocol", 1}};
            if (!owner_.empty()) {
                joined["screen_owner"] = owner_;
            }
            socket->send(joined.dump());
            broadcast(Json{{"type", "peer_joined"}, {"peer_id", id}}, id);
        } else if (type == "screen_claim") {
            if (owner_.empty() || owner_ == id) {
                owner_ = id;
                broadcast(Json{{"type", "screen_state"}, {"screen_owner", owner_}});
            } else {
                socket->send(Json{{"type", "screen_busy"}, {"screen_owner", owner_}}.dump());
            }
        } else if (type == "screen_release" && owner_ == id) {
            owner_.clear();
            broadcast(Json{{"type", "screen_state"}});
        } else if (type == "signal") {
            const auto to = peers_.find(message.value("to", ""));
            if (to != peers_.end() && to->first != id) {
                auto forwarded = message;
                forwarded["from"] = id;
                to->second->send(forwarded.dump());
            }
        }
    }

    ::rtc::WebSocketServer server_;
    std::mutex mutex_;
    std::map<std::string, std::shared_ptr<::rtc::WebSocket>, std::less<>> peers_;
    std::vector<std::shared_ptr<::rtc::WebSocket>> sockets_;
    std::string owner_;
};

struct Person {
    RoomMeshTransport transport;
    std::atomic<int> voice{0};
    std::atomic<int> video{0};
    std::atomic<int> stream_audio{0};
    std::atomic<int> keyframe_requests{0};
    std::mutex mutex;
    std::string owner;
    std::map<std::string, std::size_t, std::less<>> heard;

    ~Person() { transport.stop(); }

    void join(const std::string& id, std::uint16_t port) {
        RoomTransportCallbacks callbacks;
        callbacks.on_voice_datagram = [this](std::string_view peer, std::span<const std::byte>) {
            ++voice;
            std::scoped_lock lock(mutex);
            ++heard[std::string{peer}];
        };
        callbacks.on_video_datagram = [this](std::string_view, std::span<const std::byte>) { ++video; };
        callbacks.on_stream_audio_datagram = [this](std::string_view, std::span<const std::byte>) {
            ++stream_audio;
        };
        callbacks.on_keyframe_request = [this](std::string_view) { ++keyframe_requests; };
        callbacks.on_screen_owner = [this](std::string_view next) {
            std::scoped_lock lock(mutex);
            owner = next;
        };

        RoomMeshConfig config;
        config.signaling_url = "ws://127.0.0.1:" + std::to_string(port) + "/v1/rtc";
        config.access_token = "token";
        config.server_id = "server-1";
        config.channel_id = "voice-1";
        config.user_id = id;
        config.ice_server_urls = {"stun:127.0.0.1:3478"};
        config.allow_insecure_signaling = true;
        config.allow_no_turn = true;
        REQUIRE_FALSE(transport.start(config, std::move(callbacks)));
    }

    [[nodiscard]] std::string screen_owner() {
        std::scoped_lock lock(mutex);
        return owner;
    }

    [[nodiscard]] bool hears_four_peers() {
        std::scoped_lock lock(mutex);
        if (heard.size() != 4) {
            return false;
        }
        for (const auto& [peer, count] : heard) {
            (void)peer;
            if (count < 10) {
                return false;
            }
        }
        return true;
    }
};

// Peer connections need ICE and DTLS first; hosted runners can take several seconds.
template <typename Predicate>
[[nodiscard]] bool eventually(Predicate predicate, std::chrono::milliseconds budget = 15s) {
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return predicate();
        }
        std::this_thread::sleep_for(20ms);
    }
    return true;
}

// Five people in one process run twenty peer connections, the work of five machines, on one
// hosted runner (three vCPUs on Apple Silicon). Setup gets a matching budget.
constexpr auto kFivePeopleBudget = 60s;

template <std::size_t Count>
[[nodiscard]] std::string describe(std::array<Person, Count>& people) {
    std::string text;
    for (std::size_t index = 0; index < people.size(); ++index) {
        auto& person = people[index];
        std::size_t heard = 0;
        {
            std::scoped_lock lock(person.mutex);
            heard = person.heard.size();
        }
        text += "person-" + std::to_string(index) +
                ": state=" + std::to_string(static_cast<int>(person.transport.state())) +
                " peers=" + std::to_string(person.transport.peer_count()) +
                " heard=" + std::to_string(heard) +
                " video=" + std::to_string(person.video.load()) +
                " stream_audio=" + std::to_string(person.stream_audio.load()) + "\n";
    }
    return text;
}

} // namespace

TEST_CASE("two people talk and watch a stream with its audio over real WebRTC") {
    RtcScope rtc_scope;
    Relay relay;
    Person alice;
    Person bob;
    // RTP-shaped payload (version bits 0b10), never a keyframe request.
    std::array<std::byte, 16> media{};
    media[0] = std::byte{0x80};

    alice.join("alice", relay.port());
    REQUIRE(eventually([&] { return alice.transport.state() == RoomTransportState::joined; }));
    bob.join("bob", relay.port());

    // Voice both ways once the data channels open.
    REQUIRE(eventually([&] {
        (void)alice.transport.send_voice(media);
        (void)bob.transport.send_voice(media);
        return alice.voice.load() > 0 && bob.voice.load() > 0;
    }));

    // Alice goes live; both see who owns the stream.
    REQUIRE(alice.transport.claim_screen());
    REQUIRE(eventually([&] { return alice.screen_owner() == "alice" && bob.screen_owner() == "alice"; }));

    // Bob watches: video and stream audio arrive on their own lanes.
    REQUIRE(eventually([&] {
        (void)alice.transport.send_video(media);
        (void)alice.transport.send_stream_audio(media);
        return bob.video.load() > 0 && bob.stream_audio.load() > 0;
    }));

    // Bob lost a frame: his keyframe request reaches only the sharer, never as media.
    REQUIRE(eventually([&] {
        (void)bob.transport.request_keyframe();
        return alice.keyframe_requests.load() > 0;
    }));
    CHECK(bob.keyframe_requests.load() == 0);

    // A viewer cannot inject video or stream audio into someone else's stream.
    for (int attempt = 0; attempt < 20; ++attempt) {
        (void)bob.transport.send_video(media);
        (void)bob.transport.send_stream_audio(media);
        std::this_thread::sleep_for(10ms);
    }
    CHECK(alice.video.load() == 0);
    CHECK(alice.stream_audio.load() == 0);

    // Talking continues during the stream.
    const auto heard = alice.voice.load();
    REQUIRE(eventually([&] {
        (void)bob.transport.send_voice(media);
        return alice.voice.load() > heard;
    }));

    bob.transport.stop();
    alice.transport.stop();
}

TEST_CASE("five people talk while four watch one screen and its audio over real WebRTC") {
    RtcScope rtc_scope;
    Relay relay;
    std::array<Person, 5> people;
    std::array<std::byte, 128> voice{};
    std::array<std::byte, 1200> video{};
    std::array<std::byte, 512> audio{};
    voice[0] = video[0] = audio[0] = std::byte{0x80};

    for (std::size_t index = 0; index < people.size(); ++index) {
        people[index].join("person-" + std::to_string(index), relay.port());
        if (!eventually([&] { return people[index].transport.state() == RoomTransportState::joined; },
                        kFivePeopleBudget)) {
            FAIL("person-" << index << " did not join\n" << describe(people));
        }
    }
    auto& sharer = people.front();
    REQUIRE(sharer.transport.claim_screen());
    if (!eventually(
            [&] {
                for (auto& person : people) {
                    if (person.screen_owner() != "person-0" || person.transport.peer_count() != 4) {
                        return false;
                    }
                }
                return true;
            },
            kFivePeopleBudget)) {
        FAIL("the mesh did not form\n" << describe(people));
    }

    // Every participant must hear all four other identities while all viewers receive both
    // screen lanes. Bounded wait tolerates ICE setup and unordered no-retransmit packet loss.
    const bool flowing = eventually(
        [&] {
            for (auto& person : people) {
                (void)person.transport.send_voice(voice);
            }
            (void)sharer.transport.send_video(video);
            (void)sharer.transport.send_stream_audio(audio);
            for (std::size_t index = 0; index < people.size(); ++index) {
                if (!people[index].hears_four_peers() ||
                    (index > 0 &&
                     (people[index].video.load() < 10 || people[index].stream_audio.load() < 10))) {
                    return false;
                }
            }
            return true;
        },
        kFivePeopleBudget);
    if (!flowing) {
        FAIL("media did not reach everyone\n" << describe(people));
    }

    for (std::size_t index = 1; index < people.size(); ++index) {
        CHECK(people[index].transport.send_video(video) == 0);
        CHECK(people[index].transport.send_stream_audio(audio) == 0);
    }
    CHECK(sharer.video.load() == 0);
    CHECK(sharer.stream_audio.load() == 0);
}
