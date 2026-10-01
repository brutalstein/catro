#include <catro/macos_product_session.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
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

product::ProductSessionDependencies online(const std::shared_ptr<FakeDirectory>& directory) {
    product::ProductSessionDependencies deps;
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
    CHECK(snapshot.members.front().self);
    CHECK_FALSE(snapshot.members.back().self);
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
