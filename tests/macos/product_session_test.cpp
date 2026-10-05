#include <catro/macos_product_session.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

using namespace catro;

namespace {

constexpr std::string_view kCode = "CAT-1234-5678-9ABC-DEF0-1234";

std::string server_json(const std::string& id, const std::string& role, std::size_t members = 2) {
    const auto suffix = id.substr(id.find('-') + 1);
    return R"({"id":")" + id + R"(","owner_id":"user-1","name":"Server )" + suffix +
           R"(","public_code":")" + (role == "owner" ? std::string{kCode} : std::string{}) +
           R"(","text_channel_id":"text-)" + suffix + R"(","voice_channel_id":"voice-)" + suffix +
           R"(","role":")" + role + R"(","member_count":)" + std::to_string(members) + "}";
}

std::string request_json(const std::string& id, const std::string& server_name, const std::string& status) {
    return R"({"id":")" + id + R"(","server_name":")" + server_name + R"(","public_code":")" +
           std::string{kCode} + R"(","requester_display_name":"Guest","message":"hi","status":")" + status +
           R"(","created_at":100,"updated_at":100,"expires_at":200})";
}

std::string join(const std::vector<std::string>& items) {
    std::string out;
    for (const auto& item : items) {
        out += (out.empty() ? "" : ",") + item;
    }
    return out;
}

std::string param(std::string_view endpoint, std::string_view name) {
    const auto key = std::string{name} + "=";
    auto start = endpoint.find(key);
    if (start == std::string_view::npos) {
        return {};
    }
    start += key.size();
    const auto end = endpoint.find('&', start);
    return std::string{endpoint.substr(start, end == std::string_view::npos ? endpoint.size() - start : end - start)};
}

// Route-based fake of the production directory endpoints; called only on the session worker.
struct FakeDirectory {
    std::mutex mutex;
    std::vector<std::string> servers;                        // server JSON objects
    std::map<std::string, std::vector<std::string>> members; // server id -> member JSON
    std::map<std::string, std::uint64_t> message_counts;     // server id -> highest sequence
    std::map<std::string, std::vector<std::string>> pending; // server id -> request JSON
    std::vector<std::string> outgoing;                       // request JSON
    std::vector<std::string> endpoints;
    std::map<std::uint64_t, std::string> sent; // server-1 sequence -> content posted by the session
    bool offline = false;                      // every request fails like an unreachable network

    std::string message_json(const std::string& server_id, std::uint64_t sequence) {
        const auto suffix = server_id.substr(server_id.find('-') + 1);
        const auto found = server_id == "server-1" ? sent.find(sequence) : sent.end();
        const auto content =
            found != sent.end() ? found->second : server_id + ":" + std::to_string(sequence);
        return R"({"id":"m-)" + suffix + "-" + std::to_string(sequence) + R"(","sequence":)" +
               std::to_string(sequence) + R"(,"server_id":")" + server_id + R"(","channel_id":"text-)" + suffix +
               R"(","author_id":"user-2","author_display_name":"Guest","content":")" + content +
               R"(","created_at":100})";
    }

    platform::macos::DirectoryHttpResult route(const platform::macos::DirectoryHttpRequest& request) {
        std::scoped_lock lock(mutex);
        endpoints.push_back(request.method + " " + request.endpoint);
        if (offline) {
            return community::DirectoryError{community::DirectoryErrorCode::network_failure, "offline"};
        }
        const std::string_view endpoint = request.endpoint;
        const auto ok = [](std::string body) {
            return platform::macos::DirectoryHttpResponse{200, std::move(body)};
        };
        if (endpoint == "/v1/users/register") {
            return ok(R"({"access_token":"token"})");
        }
        if (endpoint == "/v1/servers/sync") {
            return ok(server_json("server-1", "owner"));
        }
        if (endpoint == "/v1/servers") {
            return ok(R"({"servers":[)" + join(servers) + "]}");
        }
        if (endpoint.starts_with("/v1/members?")) {
            return ok(R"({"members":[)" + join(members[param(endpoint, "server_id")]) + "]}");
        }
        if (endpoint.starts_with("/v1/messages?")) {
            const auto server_id = param(endpoint, "server_id");
            const auto after = std::stoull(param(endpoint, "after"));
            const auto limit = std::stoull(param(endpoint, "limit"));
            std::vector<std::string> page;
            auto last = after;
            for (auto sequence = after + 1; sequence <= message_counts[server_id] && page.size() < limit;
                 ++sequence) {
                page.push_back(message_json(server_id, sequence));
                last = sequence;
            }
            return ok(R"({"messages":[)" + join(page) + R"(],"next_after":)" + std::to_string(last) + "}");
        }
        if (endpoint == "/v1/messages") {
            // Fixture content has no escapes, so a plain scan of the JSON body is enough.
            constexpr std::string_view key = R"("content":")";
            const auto start = request.body.find(key) + key.size();
            const auto sequence = ++message_counts["server-1"];
            sent[sequence] = request.body.substr(start, request.body.find('"', start) - start);
            return ok(message_json("server-1", sequence));
        }
        if (endpoint == "/v1/rtc-token") {
            return ok(
                R"({"token":"rtc","expires":200,"server_id":"server-1","channel_id":"voice-1","peer_id":"peer-self","signaling_url":"wss://catro.example.com/v1/rtc","ice_servers":["stun:turn.example.com:3478"],"max_room_peers":4})");
        }
        if (endpoint == "/v1/join-requests?mine=1") {
            return ok(R"({"requests":[)" + join(outgoing) + "]}");
        }
        if (endpoint.starts_with("/v1/join-requests?server_id=")) {
            return ok(R"({"requests":[)" + join(pending[param(endpoint, "server_id")]) + "]}");
        }
        return community::DirectoryError{community::DirectoryErrorCode::rejected, "unrouted fake endpoint"};
    }
};

class FakeTransport final : public platform::macos::DirectoryHttpTransport {
public:
    explicit FakeTransport(std::shared_ptr<FakeDirectory> directory) : directory_(std::move(directory)) {}

    platform::macos::DirectoryHttpResult request(const platform::macos::DirectoryHttpRequest& request,
                                                 platform::macos::DirectoryCancellationToken stop) noexcept override {
        if (stop.stop_requested()) {
            return community::DirectoryError{community::DirectoryErrorCode::cancelled, "cancelled"};
        }
        return directory_->route(request);
    }

private:
    std::shared_ptr<FakeDirectory> directory_;
};

community::LocalState local_state() {
    community::LocalState state;
    state.identity.id.bytes[0] = std::byte{1};
    state.identity.display_name = "Owner";
    state.personal_server.id.bytes[0] = std::byte{2};
    state.personal_server.owner_id = state.identity.id;
    state.personal_server.name = "Catro";
    community::Channel text;
    text.id.bytes[0] = std::byte{3};
    text.name = "general";
    community::Channel voice;
    voice.id.bytes[0] = std::byte{4};
    voice.name = "Voice";
    voice.kind = community::ChannelKind::voice;
    state.personal_server.channels = {text, voice};
    state.personal_server.members = {{state.identity.id, community::ServerRole::owner}};
    return state;
}

struct Recorder {
    std::mutex mutex;
    std::vector<std::string> notices;
    std::size_t calls = 0;

    product::ProductSession::Listener listener() {
        return [this](const product::ProductSnapshot& snapshot) {
            std::scoped_lock lock(mutex);
            ++calls;
            if (!snapshot.notice.empty()) {
                notices.push_back(snapshot.notice);
            }
        };
    }

    std::size_t count(const std::string& notice) {
        std::scoped_lock lock(mutex);
        return static_cast<std::size_t>(std::count(notices.begin(), notices.end(), notice));
    }
};

// Deterministic room + voice behind the C ABI table. Function pointers cannot capture, so the
// active test publishes its instance through g_media.
struct FakeMedia {
    std::mutex mutex;
    std::vector<std::string> calls;
    std::int32_t room_start_result = 0;
    std::int32_t voice_start_result = 0;
    std::int32_t claim_result = 0;
    std::string owner;             // room screen owner reported now
    std::string owner_after_claim; // owner reported once a claim succeeds
    std::string room_error;
    bool room_reconnecting = false;
    std::uint32_t peers = 1;
    std::string signaling_url;
    std::string user_id;
    std::uint32_t max_remote_peers = 0;
    void* voice_room = nullptr;
    std::int32_t voice_bitrate = 0;
    std::uint8_t muted = 0;
    std::uint8_t deafened = 0;
    std::uint8_t self_speaking = 0;
    std::string speaking_user;
    std::string volume_user;
    float volume = 1.0F;
    std::uint8_t processing = 0;
    float threshold = 0.0F;
    std::uint8_t transmit = 1;
    std::string voice_input;
    std::string live_input;
    std::string live_output;
    float input_level = -100.0F;
    int room_storage = 0;
    int voice_storage = 0;

    bool called(const std::string& name) {
        std::scoped_lock lock(mutex);
        return std::find(calls.begin(), calls.end(), name) != calls.end();
    }
};

FakeMedia* g_media = nullptr;

void record(const char* name) {
    std::scoped_lock lock(g_media->mutex);
    g_media->calls.emplace_back(name);
}

std::ptrdiff_t idle_receive(CatroRoomRuntimeHandle, std::byte*, std::size_t, std::uint32_t timeout_ms) noexcept {
    std::this_thread::sleep_for(std::chrono::milliseconds(std::min<std::uint32_t>(timeout_ms, 20)));
    return 0;
}

product::ProductMediaApi fake_media_api() {
    product::ProductMediaApi api;
    api.room_create = []() noexcept -> CatroRoomRuntimeHandle {
        record("room_create");
        return &g_media->room_storage;
    };
    api.room_destroy = [](CatroRoomRuntimeHandle) noexcept { record("room_destroy"); };
    api.room_start = [](CatroRoomRuntimeHandle, const CatroRoomRuntimeConfig* config) noexcept {
        record("room_start");
        std::scoped_lock lock(g_media->mutex);
        g_media->signaling_url = config->signaling_url;
        g_media->user_id = config->user_id;
        g_media->max_remote_peers = config->max_remote_peers;
        return g_media->room_start_result;
    };
    api.room_stop = [](CatroRoomRuntimeHandle) noexcept { record("room_stop"); };
    api.room_claim_screen = [](CatroRoomRuntimeHandle) noexcept {
        record("room_claim_screen");
        std::scoped_lock lock(g_media->mutex);
        if (g_media->claim_result == 0) {
            g_media->owner = g_media->owner_after_claim;
        }
        return g_media->claim_result;
    };
    api.room_release_screen = [](CatroRoomRuntimeHandle) noexcept {
        record("room_release_screen");
        std::scoped_lock lock(g_media->mutex);
        g_media->owner.clear();
    };
    api.screen.snapshot = [](CatroRoomRuntimeHandle) noexcept {
        std::scoped_lock lock(g_media->mutex);
        CatroRoomRuntimeSnapshot snapshot{};
        snapshot.state = !g_media->room_error.empty() ? CATRO_ROOM_FAILED
                         : g_media->room_reconnecting ? CATRO_ROOM_CONNECTING
                                                      : CATRO_ROOM_JOINED;
        snapshot.peer_count = g_media->peers;
        g_media->owner.copy(snapshot.screen_owner, sizeof(snapshot.screen_owner) - 1);
        g_media->room_error.copy(snapshot.error, sizeof(snapshot.error) - 1);
        return snapshot;
    };
    api.screen.send_video = [](CatroRoomRuntimeHandle, const std::byte*, std::size_t) noexcept {
        return std::size_t{0};
    };
    api.screen.receive_video = idle_receive;
    api.screen.send_stream_audio = [](CatroRoomRuntimeHandle, const std::byte*, std::size_t) noexcept {
        return std::size_t{0};
    };
    api.screen.receive_stream_audio = idle_receive;
    api.voice_create = []() noexcept -> CatroVoiceRuntimeHandle {
        record("voice_create");
        return &g_media->voice_storage;
    };
    api.voice_destroy = [](CatroVoiceRuntimeHandle) noexcept { record("voice_destroy"); };
    api.voice_start = [](CatroVoiceRuntimeHandle, const CatroVoiceRuntimeConfig* config) noexcept {
        record("voice_start");
        std::scoped_lock lock(g_media->mutex);
        g_media->voice_room = config->room_runtime;
        g_media->voice_bitrate = config->bitrate;
        g_media->voice_input = config->input_endpoint != nullptr ? config->input_endpoint : "";
        return g_media->voice_start_result;
    };
    api.voice_stop = [](CatroVoiceRuntimeHandle) noexcept { record("voice_stop"); };
    api.voice_set_muted = [](CatroVoiceRuntimeHandle, std::uint8_t muted) noexcept {
        std::scoped_lock lock(g_media->mutex);
        g_media->muted = muted;
    };
    api.voice_set_deafened = [](CatroVoiceRuntimeHandle, std::uint8_t deafened) noexcept {
        std::scoped_lock lock(g_media->mutex);
        g_media->deafened = deafened;
    };
    api.voice_snapshot = [](CatroVoiceRuntimeHandle) noexcept {
        std::scoped_lock lock(g_media->mutex);
        CatroVoiceRuntimeSnapshot snapshot{};
        snapshot.state = CATRO_VOICE_JOINED;
        snapshot.muted = g_media->muted;
        snapshot.deafened = g_media->deafened;
        snapshot.speaking = g_media->self_speaking;
        snapshot.input_level = g_media->input_level;
        return snapshot;
    };
    api.voice_set_user_volume = [](CatroVoiceRuntimeHandle, const char* user_id, float volume) noexcept {
        std::scoped_lock lock(g_media->mutex);
        g_media->volume_user = user_id;
        g_media->volume = volume;
    };
    api.voice_user_speaking = [](CatroVoiceRuntimeHandle, const char* user_id) noexcept -> std::uint8_t {
        std::scoped_lock lock(g_media->mutex);
        return g_media->speaking_user == user_id ? 1U : 0U;
    };
    api.voice_set_processing = [](CatroVoiceRuntimeHandle, std::uint8_t echo, std::uint8_t noise,
                                  std::uint8_t gain) noexcept {
        std::scoped_lock lock(g_media->mutex);
        g_media->processing = static_cast<std::uint8_t>(echo | (noise << 1U) | (gain << 2U));
    };
    api.voice_set_input_threshold = [](CatroVoiceRuntimeHandle, float dbfs) noexcept {
        std::scoped_lock lock(g_media->mutex);
        g_media->threshold = dbfs;
    };
    api.voice_set_transmit = [](CatroVoiceRuntimeHandle, std::uint8_t transmit) noexcept {
        std::scoped_lock lock(g_media->mutex);
        g_media->transmit = transmit;
    };
    api.voice_set_devices = [](CatroVoiceRuntimeHandle, const char* input, const char* output) noexcept {
        std::scoped_lock lock(g_media->mutex);
        g_media->live_input = input;
        g_media->live_output = output;
    };
    return api;
}

struct MediaScope {
    FakeMedia media;
    MediaScope() { g_media = &media; }
    ~MediaScope() { g_media = nullptr; }
};

product::ProductSessionDependencies online(const std::shared_ptr<FakeDirectory>& directory) {
    product::ProductSessionDependencies deps;
    deps.media = fake_media_api();
    deps.local_state = local_state();
    deps.load_config = [] {
        return community::DirectoryConfigResult{community::DirectoryServiceConfig{"https://catro.example.com", false}};
    };
    deps.load_credential = [] { return community::DirectoryStringResult{std::string{"credential"}}; };
    deps.make_transport = [directory](const community::DirectoryServiceConfig&) {
        return std::unique_ptr<platform::macos::DirectoryHttpTransport>(std::make_unique<FakeTransport>(directory));
    };
    return deps;
}

std::shared_ptr<FakeDirectory> seeded_directory() {
    auto directory = std::make_shared<FakeDirectory>();
    const auto self = community::to_hex(local_state().identity.id);
    directory->servers = {server_json("server-1", "owner"), server_json("server-2", "member")};
    directory->members["server-1"] = {R"({"user_id":")" + self + R"(","display_name":"Owner","role":"owner"})",
                                      R"({"user_id":"user-2","display_name":"Guest","role":"member"})"};
    directory->members["server-2"] = {R"({"user_id":"user-3","display_name":"Host","role":"owner"})"};
    directory->message_counts["server-1"] = 3;
    directory->message_counts["server-2"] = 2;
    directory->pending["server-1"] = {request_json("request-1", "Server 1", "pending")};
    return directory;
}

bool wait_until(const product::ProductSession& session,
                const std::function<bool(const product::ProductSnapshot&)>& predicate) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate(session.snapshot())) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}

void poll_once(product::ProductSession& session) {
    const auto revision = session.snapshot().revision;
    session.poll();
    REQUIRE(wait_until(session, [revision](const auto& snapshot) { return snapshot.revision > revision; }));
}

bool synchronized_with_messages(const product::ProductSnapshot& snapshot) {
    return snapshot.workspace.connection == app::ConnectionState::synchronized && !snapshot.messages.empty();
}

} // namespace

TEST_CASE("macOS product session fails closed without a local profile") {
    bool config_loaded = false;
    product::ProductSessionDependencies deps;
    deps.load_config = [&config_loaded] {
        config_loaded = true;
        return community::DirectoryConfigResult{community::DirectoryServiceConfig{}};
    };
    product::ProductSession session(std::move(deps), {});
    session.start();

    REQUIRE(wait_until(session, [](const auto& snapshot) {
        return snapshot.workspace.connection == app::ConnectionState::failed;
    }));
    CHECK(session.snapshot().workspace.connection_message == "Local profile is unavailable.");
    CHECK(session.snapshot().servers.empty());
    session.stop();
    CHECK_FALSE(config_loaded);
}

TEST_CASE("macOS product session keeps the personal server when online services are not configured") {
    bool transport_made = false;
    auto deps = online(std::make_shared<FakeDirectory>());
    deps.load_config = [] {
        return community::DirectoryConfigResult{
            community::DirectoryError{community::DirectoryErrorCode::not_configured, "missing"}};
    };
    deps.make_transport = [&transport_made](const community::DirectoryServiceConfig&) {
        transport_made = true;
        return std::unique_ptr<platform::macos::DirectoryHttpTransport>{};
    };
    product::ProductSession session(std::move(deps), {});
    session.start();

    REQUIRE(wait_until(session, [](const auto& snapshot) {
        return snapshot.workspace.connection == app::ConnectionState::failed;
    }));
    const auto snapshot = session.snapshot();
    CHECK(snapshot.workspace.connection_message ==
          "Online services are not configured. Local mode remains available.");
    CHECK_FALSE(snapshot.workspace.send_message.available());
    REQUIRE(snapshot.servers.size() == 1);
    CHECK(snapshot.servers.front().name == "Catro");
    CHECK(snapshot.servers.front().owner);
    CHECK(snapshot.active_server_id == snapshot.servers.front().id);
    CHECK(snapshot.identity_name == "Owner");
    session.stop();
    CHECK_FALSE(transport_made);
}

TEST_CASE("macOS product session keeps retrying until the network is back") {
    auto directory = seeded_directory();
    directory->offline = true;
    product::ProductSession session(online(directory), {});
    session.start();

    REQUIRE(wait_until(session, [](const auto& snapshot) {
        return snapshot.workspace.connection == app::ConnectionState::connecting &&
               snapshot.workspace.connection_message.find("Retrying in 2 s") != std::string::npos;
    }));
    CHECK_FALSE(session.snapshot().workspace.send_message.available());

    {
        std::scoped_lock lock(directory->mutex);
        directory->offline = false;
    }
    // The app polls once a second; the first retry is due two seconds after the failure.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(6);
    while (!synchronized_with_messages(session.snapshot()) && std::chrono::steady_clock::now() < deadline) {
        session.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    CHECK(synchronized_with_messages(session.snapshot()));
    session.stop();
}

TEST_CASE("macOS product session renames the profile locally and online") {
    auto directory = seeded_directory();
    Recorder recorder;
    std::mutex saved_mutex;
    std::vector<std::string> saved;
    auto deps = online(directory);
    deps.save_local_state = [&](const community::LocalState& state) -> std::optional<std::string> {
        std::scoped_lock lock(saved_mutex);
        saved.push_back(state.identity.display_name);
        return std::nullopt;
    };
    product::ProductSession session(std::move(deps), recorder.listener());
    session.start();
    REQUIRE(wait_until(session, synchronized_with_messages));

    session.rename_profile(" \n ");
    session.rename_profile(std::string(65, 'x'));
    REQUIRE(wait_until(session, [&recorder](const auto&) {
        return recorder.count("Enter a name of 1 to 64 bytes without line breaks.") == 2;
    }));
    session.rename_profile("  Mira  ");
    REQUIRE(wait_until(session, [](const auto& snapshot) { return snapshot.identity_name == "Mira"; }));
    REQUIRE(wait_until(session, [&recorder](const auto&) {
        return recorder.count("Profile saved and synced.") == 1;
    }));
    session.stop();
    {
        std::scoped_lock lock(saved_mutex);
        CHECK(saved == std::vector<std::string>{"Mira"});
    }
    std::scoped_lock lock(directory->mutex);
    CHECK(std::count_if(directory->endpoints.begin(), directory->endpoints.end(), [](const std::string& endpoint) {
              return endpoint.ends_with("/v1/users/register");
          }) == 2);
}

TEST_CASE("macOS product session saves a renamed profile without online services") {
    auto deps = online(std::make_shared<FakeDirectory>());
    deps.load_config = [] {
        return community::DirectoryConfigResult{
            community::DirectoryError{community::DirectoryErrorCode::not_configured, "missing"}};
    };
    deps.save_local_state = [](const community::LocalState&) -> std::optional<std::string> { return std::nullopt; };
    Recorder recorder;
    product::ProductSession session(std::move(deps), recorder.listener());
    session.start();
    session.rename_profile("Mira");
    REQUIRE(wait_until(session, [&recorder](const auto&) {
        return recorder.count("Profile saved on this Mac. It will sync when you are online.") == 1;
    }));
    CHECK(session.snapshot().identity_name == "Mira");
    session.stop();
}

TEST_CASE("macOS product session synchronizes and resets state on server switch") {
    auto directory = seeded_directory();
    product::ProductSession session(online(directory), {});
    session.start();

    REQUIRE(wait_until(session, synchronized_with_messages));
    auto snapshot = session.snapshot();
    REQUIRE(snapshot.servers.size() == 2);
    CHECK(snapshot.active_server_id == "server-1");
    CHECK(snapshot.workspace.send_message.available());
    REQUIRE(snapshot.members.size() == 2);
    CHECK(snapshot.members.front().owner);
    CHECK(snapshot.members.front().is_self);
    CHECK_FALSE(snapshot.members.back().is_self);
    REQUIRE(snapshot.messages.size() == 3);
    CHECK(snapshot.messages.back().content == "server-1:3");
    REQUIRE(snapshot.pending_requests.size() == 1);
    CHECK(snapshot.pending_requests.front().requester == "Guest");

    session.select_server("server-2");
    REQUIRE(wait_until(session, [](const auto& value) { return value.active_server_id == "server-2"; }));
    snapshot = session.snapshot();
    REQUIRE(snapshot.messages.size() == 2);
    CHECK(snapshot.messages.front().content == "server-2:1");
    CHECK(snapshot.pending_requests.empty());
    REQUIRE(snapshot.members.size() == 1);
    CHECK(snapshot.members.front().display_name == "Host");

    session.select_server("missing-server");
    poll_once(session);
    CHECK(session.snapshot().active_server_id == "server-2");
    session.stop();

    std::scoped_lock lock(directory->mutex);
    CHECK(std::none_of(directory->endpoints.begin(), directory->endpoints.end(), [](const std::string& endpoint) {
        return endpoint == "GET /v1/join-requests?server_id=server-2";
    }));
}

TEST_CASE("macOS product session pages messages without duplicates and retains the newest 512") {
    auto directory = seeded_directory();
    directory->message_counts["server-1"] = 600;
    product::ProductSession session(online(directory), {});
    session.start();

    REQUIRE(wait_until(session, synchronized_with_messages));
    CHECK(session.snapshot().messages.size() == community::kMaxMessagePage);
    for (int tick = 0; tick < 6; ++tick) {
        poll_once(session);
    }
    const auto snapshot = session.snapshot();
    REQUIRE(snapshot.messages.size() == product::kMaxRetainedMessages);
    CHECK(snapshot.messages.front().sequence == 89);
    CHECK(snapshot.messages.back().sequence == 600);
    CHECK(std::adjacent_find(snapshot.messages.begin(), snapshot.messages.end(),
                             [](const auto& a, const auto& b) { return b.sequence != a.sequence + 1; }) ==
          snapshot.messages.end());
    session.stop();
}

TEST_CASE("macOS product session consumes outgoing join decisions once") {
    auto directory = seeded_directory();
    directory->servers = {server_json("server-1", "owner")};
    Recorder recorder;
    product::ProductSession session(online(directory), recorder.listener());
    session.start();
    REQUIRE(wait_until(session, synchronized_with_messages));
    REQUIRE(session.snapshot().servers.size() == 1);

    {
        std::scoped_lock lock(directory->mutex);
        directory->outgoing = {request_json("request-2", "Server 2", "approved"),
                               request_json("request-3", "Server 3", "rejected"),
                               request_json("request-4", "Server 4", "pending")};
        directory->servers.push_back(server_json("server-2", "member"));
    }
    for (std::uint32_t tick = 0; tick < product::kSlowPollTicks * 2; ++tick) {
        poll_once(session);
    }
    CHECK(session.snapshot().servers.size() == 2);
    CHECK(recorder.count("Request to Server 2 approved.") == 1);
    CHECK(recorder.count("Request to Server 3 was rejected.") == 1);
    session.stop();
}

TEST_CASE("macOS product session validates and sends messages, then stops silently") {
    auto directory = seeded_directory();
    Recorder recorder;
    product::ProductSession session(online(directory), recorder.listener());
    session.start();
    REQUIRE(wait_until(session, synchronized_with_messages));

    session.send_message("");
    REQUIRE(wait_until(session, [&recorder](const auto&) {
        return recorder.count("Messages must be 1 to 2000 bytes.") == 1;
    }));
    session.send_message("hello there");
    REQUIRE(wait_until(session, [](const auto& snapshot) { return snapshot.messages.size() == 4; }));
    CHECK(session.snapshot().messages.back().content == "hello there");

    session.stop();
    std::size_t calls = 0;
    {
        std::scoped_lock lock(recorder.mutex);
        calls = recorder.calls;
    }
    session.poll();
    session.send_message("late");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    std::scoped_lock lock(recorder.mutex);
    CHECK(recorder.calls == calls);
}

namespace {

bool joined(const product::ProductSnapshot& snapshot) {
    return snapshot.media.phase == product::VoicePhase::joined;
}

product::ShareRequest display_share() {
    product::ShareRequest request;
    request.source.native_id = 7;
    request.source.title = "Display";
    request.source.width = 1920;
    request.source.height = 1080;
    return request;
}

} // namespace

TEST_CASE("macOS product session waits for the signaling join before starting voice") {
    MediaScope scope;
    auto& media = scope.media;
    {
        std::scoped_lock lock(media.mutex);
        media.room_reconnecting = true;
    }
    product::ProductSession session(online(seeded_directory()), {});
    session.start();
    REQUIRE(wait_until(session, synchronized_with_messages));

    session.join_voice();
    REQUIRE(wait_until(session, [&media](const auto& snapshot) {
        return media.called("room_start") &&
               snapshot.media.status == "RTC connecting…";
    }));
    {
        std::scoped_lock lock(media.mutex);
        CHECK(std::find(media.calls.begin(), media.calls.end(), "voice_start") ==
              media.calls.end());
        media.room_reconnecting = false;
    }
    REQUIRE(wait_until(session, joined));
    CHECK(media.called("voice_start"));
    session.stop();
}

TEST_CASE("macOS product session reports an asynchronous room failure before voice starts") {
    MediaScope scope;
    auto& media = scope.media;
    {
        std::scoped_lock lock(media.mutex);
        media.room_error = "TLS connection failed";
    }
    product::ProductSession session(online(seeded_directory()), {});
    session.start();
    REQUIRE(wait_until(session, synchronized_with_messages));

    session.join_voice();
    REQUIRE(wait_until(session, [](const auto& snapshot) {
        return snapshot.media.phase == product::VoicePhase::failed;
    }));
    CHECK(session.snapshot().media.status ==
          "Room connection lost: TLS connection failed");
    CHECK_FALSE(media.called("voice_start"));
    session.stop();
}

TEST_CASE("macOS product session joins voice through provisioning, room, voice and listening") {
    MediaScope scope;
    auto& media = scope.media;
    product::ProductSession session(online(seeded_directory()), {});
    session.start();
    REQUIRE(wait_until(session, synchronized_with_messages));

    session.join_voice();
    REQUIRE(wait_until(session, joined));
    {
        std::scoped_lock lock(media.mutex);
        const std::vector<std::string> expected{"room_create", "voice_create", "room_start", "voice_start"};
        CHECK(media.calls == expected);
        CHECK(media.signaling_url == "wss://catro.example.com/v1/rtc");
        CHECK(media.user_id == "peer-self");
        CHECK(media.max_remote_peers == 3);
        CHECK(media.voice_room == &media.room_storage);
        CHECK(media.voice_bitrate == 48'000);
    }
    CHECK(session.snapshot().media.voice_server_id == "server-1");

    // Speaking indicators and per-user volume go straight to the voice runtime.
    REQUIRE(wait_until(session, [](const auto& s) { return s.members.size() == 2; }));
    CHECK(session.speaking_members().empty());
    {
        std::scoped_lock lock(media.mutex);
        media.speaking_user = "user-2";
        media.self_speaking = 1;
    }
    auto speaking = session.speaking_members();
    std::sort(speaking.begin(), speaking.end());
    REQUIRE(speaking.size() == 2);
    CHECK(speaking[1] == "user-2");
    session.set_member_volume("user-2", 0.5F);
    session.set_voice_processing(true, false, true);
    session.set_input_threshold(-42.0F);
    session.set_transmit(false);
    {
        std::scoped_lock lock(media.mutex);
        CHECK(media.volume_user == "user-2");
        CHECK(media.volume == 0.5F);
        CHECK(media.processing == 0b101);
        CHECK(media.threshold == -42.0F);
        CHECK(media.transmit == 0);
        media.speaking_user.clear();
        media.self_speaking = 0;
    }

    session.set_muted(true);
    session.set_deafened(true);
    // Both toggles publish; settle them first so poll_once observes the poll's own revision.
    REQUIRE(wait_until(session, [](const auto& s) { return s.media.muted && s.media.deafened; }));
    {
        std::scoped_lock lock(media.mutex);
        media.peers = 2;
        media.owner = "peer-2";
    }
    poll_once(session);
    auto snapshot = session.snapshot();
    CHECK(snapshot.media.muted);
    CHECK(snapshot.media.deafened);
    CHECK(snapshot.media.peer_count == 2);
    CHECK(snapshot.media.screen_owner == "peer-2");
    CHECK(media.muted == 1);
    CHECK(media.deafened == 1);

    session.join_voice(); // already joined: no second room
    session.select_server("server-2");
    REQUIRE(wait_until(session, [](const auto& value) {
        return value.active_server_id == "server-2" && value.media.phase == product::VoicePhase::idle;
    }));
    snapshot = session.snapshot();
    CHECK_FALSE(snapshot.media.muted);
    CHECK(snapshot.media.voice_server_id.empty());
    CHECK(media.called("voice_stop"));
    CHECK(media.called("room_stop"));
    session.stop();
    std::scoped_lock lock(media.mutex);
    CHECK(std::count(media.calls.begin(), media.calls.end(), "room_start") == 1);
    CHECK(media.calls.back() == "room_destroy");
}

TEST_CASE("macOS voice roster projects remote presence and corrects stale self presence immediately") {
    MediaScope scope;
    auto directory = seeded_directory();
    const auto self = community::to_hex(local_state().identity.id);
    directory->members["server-1"] = {
        R"({"user_id":")" + self +
            R"(","display_name":"Owner","role":"owner","voice_channel_id":"voice-1"})",
        R"({"user_id":"user-2","display_name":"Guest","role":"member","voice_channel_id":"voice-1"})",
        R"({"user_id":"user-4","display_name":"Offline","role":"member"})"};
    product::ProductSession session(online(directory), {});
    session.start();
    REQUIRE(wait_until(session, synchronized_with_messages));
    auto snapshot = session.snapshot();
    REQUIRE(snapshot.members.size() == 3);
    CHECK(snapshot.members[0].voice_channel_id.empty()); // stale HTTP self says joined
    CHECK(snapshot.members[1].voice_channel_id == "voice-1"); // silent remote is still connected
    CHECK(snapshot.members[2].voice_channel_id.empty());
    CHECK(session.speaking_members().empty());

    session.join_voice();
    REQUIRE(wait_until(session, joined));
    CHECK(session.snapshot().members[0].voice_channel_id == "voice-1");
    {
        std::scoped_lock lock(directory->mutex);
        directory->members["server-1"][0] = R"({"user_id":")" + self +
            R"(","display_name":"Owner","role":"owner"})";
    }
    for (std::uint32_t tick = 0; tick < product::kSlowPollTicks; ++tick) {
        poll_once(session);
    }
    CHECK(session.snapshot().members[0].voice_channel_id == "voice-1"); // stale HTTP self says left

    session.leave_voice();
    REQUIRE(wait_until(session, [](const auto& s) { return s.media.phase == product::VoicePhase::idle; }));
    snapshot = session.snapshot();
    CHECK(snapshot.members[0].voice_channel_id.empty());
    CHECK(snapshot.members[1].voice_channel_id == "voice-1"); // leaving does not erase remote presence
    session.stop();
}

TEST_CASE("macOS product session shows a rejoining room without leaving voice") {
    MediaScope scope;
    auto& media = scope.media;
    product::ProductSession session(online(seeded_directory()), {});
    session.start();
    REQUIRE(wait_until(session, synchronized_with_messages));
    CHECK(session.input_level() == -100.0F);
    session.set_audio_devices("mic-1", "");
    session.join_voice();
    REQUIRE(wait_until(session, joined));
    {
        std::scoped_lock lock(media.mutex);
        CHECK(media.voice_input == "mic-1");
        media.input_level = -30.0F;
    }
    CHECK(session.input_level() == -30.0F);
    // A device picked during the call moves it live.
    session.set_audio_devices("mic-2", "speakers-2");
    {
        std::scoped_lock lock(media.mutex);
        CHECK(media.live_input == "mic-2");
        CHECK(media.live_output == "speakers-2");
    }

    {
        std::scoped_lock lock(media.mutex);
        media.room_reconnecting = true;
    }
    poll_once(session);
    CHECK(session.snapshot().media.status == "Reconnecting...");
    CHECK(session.snapshot().media.phase == product::VoicePhase::joined);

    {
        std::scoped_lock lock(media.mutex);
        media.room_reconnecting = false;
    }
    poll_once(session);
    CHECK(session.snapshot().media.status == "Voice connected");
    session.stop();
}

TEST_CASE("macOS product session reports voice join failures and requires online services") {
    MediaScope scope;
    auto& media = scope.media;
    media.room_start_result = -1;
    media.room_error = "signaling refused";
    product::ProductSession session(online(seeded_directory()), {});
    session.start();
    REQUIRE(wait_until(session, synchronized_with_messages));

    session.join_voice();
    REQUIRE(wait_until(session, [](const auto& snapshot) {
        return snapshot.media.phase == product::VoicePhase::failed;
    }));
    CHECK(session.snapshot().media.status == "Room connection error: signaling refused");
    CHECK_FALSE(media.called("voice_start"));

    {
        std::scoped_lock lock(media.mutex);
        media.room_start_result = 0;
        media.room_error.clear();
        media.voice_start_result = 2;
    }
    session.join_voice();
    REQUIRE(wait_until(session, [&media](const auto& snapshot) {
        return snapshot.media.phase == product::VoicePhase::failed && media.called("voice_start");
    }));
    CHECK(session.snapshot().media.status.starts_with("Voice could not start"));
    session.stop();
}

TEST_CASE("macOS product session keeps voice unavailable without online services") {
    MediaScope scope;
    auto deps = online(std::make_shared<FakeDirectory>());
    deps.load_config = [] {
        return community::DirectoryConfigResult{
            community::DirectoryError{community::DirectoryErrorCode::not_configured, "missing"}};
    };
    Recorder recorder;
    product::ProductSession session(std::move(deps), recorder.listener());
    session.start();
    REQUIRE(wait_until(session, [](const auto& snapshot) {
        return snapshot.workspace.connection == app::ConnectionState::failed;
    }));
    session.join_voice();
    poll_once(session);
    CHECK(session.snapshot().media.phase == product::VoicePhase::idle);
    CHECK(recorder.count("Online services are not configured. Local mode remains available.") == 1);
    session.stop();
    CHECK_FALSE(scope.media.called("room_start"));
}

TEST_CASE("macOS product session claims screen ownership before capture and fails closed") {
    MediaScope scope;
    auto& media = scope.media;
    product::ProductSession session(online(seeded_directory()), {});
    session.start();
    REQUIRE(wait_until(session, synchronized_with_messages));
    session.join_voice();
    REQUIRE(wait_until(session, joined));

    const auto share_status = [&session](const std::string& status) {
        session.start_share(display_share());
        REQUIRE(wait_until(session, [&status](const auto& snapshot) { return snapshot.media.status == status; }));
        CHECK_FALSE(session.snapshot().media.sharing);
    };

    {
        std::scoped_lock lock(media.mutex);
        media.owner = "peer-2";
    }
    share_status("Another participant is sharing");
    CHECK_FALSE(media.called("room_claim_screen"));

    {
        std::scoped_lock lock(media.mutex);
        media.owner.clear();
        media.claim_result = -1;
    }
    share_status("Screen ownership request failed");

    {
        std::scoped_lock lock(media.mutex);
        media.claim_result = 0;
        media.owner_after_claim = "peer-2"; // another peer won the race
    }
    share_status("Another participant is sharing");

    {
        std::scoped_lock lock(media.mutex);
        media.owner.clear();
        media.owner_after_claim.clear(); // nobody confirms ownership
    }
    share_status("Screen ownership request timed out");
    CHECK(media.called("room_release_screen")); // a late grant must not hold the slot

    {
        std::scoped_lock lock(media.mutex);
        media.owner_after_claim = "peer-self";
        media.calls.clear();
    }
    auto invalid = display_share();
    invalid.fps = 0;
    session.start_share(invalid);
    REQUIRE(wait_until(session, [](const auto& snapshot) {
        return snapshot.media.status == "Invalid screen-share settings";
    }));
    CHECK_FALSE(media.called("room_claim_screen"));
    invalid = display_share();
    invalid.source.native_id = 0;
    session.start_share(invalid);
    poll_once(session);
    CHECK_FALSE(media.called("room_claim_screen"));
    session.stop();
}

TEST_CASE("macOS product session loads sources and toggles watching") {
    MediaScope scope;
    auto deps = online(seeded_directory());
    std::atomic_bool denied{false};
    deps.enumerate_sources = [&denied] {
        platform::macos::CaptureEnumerationResult result;
        if (denied) {
            platform::macos::CaptureSource camera;
            camera.kind = platform::macos::CaptureSourceKind::camera;
            camera.native_id = 3;
            camera.title = "FaceTime HD Camera";
            result.sources = {camera};
            result.error = platform::macos::ScreenCaptureError{
                platform::macos::ScreenCaptureErrorCode::permission_denied, 0};
            return result;
        }
        platform::macos::CaptureSource display;
        display.native_id = 1;
        display.title = "Display";
        platform::macos::CaptureSource window;
        window.kind = platform::macos::CaptureSourceKind::window;
        window.native_id = 2;
        window.title = "Game";
        result.sources = {display, window};
        return result;
    };
    product::ProductSession session(std::move(deps), {});
    session.start();
    REQUIRE(wait_until(session, synchronized_with_messages));

    session.load_sources();
    REQUIRE(wait_until(session, [](const auto& snapshot) { return snapshot.media.sources.size() == 2; }));
    denied = true;
    session.load_sources();
    REQUIRE(wait_until(session, [](const auto& snapshot) {
        return snapshot.media.status == "Screen Recording permission denied";
    }));
    REQUIRE(session.snapshot().media.sources.size() == 1);
    CHECK(session.snapshot().media.sources[0].kind == platform::macos::CaptureSourceKind::camera);

    session.join_voice();
    REQUIRE(wait_until(session, joined));
    session.set_watching(true);
    REQUIRE(wait_until(session, [](const auto& snapshot) { return snapshot.media.watching; }));
    session.set_watching(false);
    REQUIRE(wait_until(session, [](const auto& snapshot) { return !snapshot.media.watching; }));
    session.stop();
}
