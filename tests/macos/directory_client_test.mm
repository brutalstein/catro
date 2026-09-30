#include <catro/platform/macos/directory_client.hpp>

#include <catch2/catch_test_macros.hpp>

#import <Foundation/Foundation.h>
#import <Security/Security.h>

#include <atomic>
#include <filesystem>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include <unistd.h>

using namespace catro;

namespace {

class FakeTransport final : public platform::macos::DirectoryHttpTransport {
public:
    platform::macos::DirectoryHttpResult request(
        const platform::macos::DirectoryHttpRequest& request,
        platform::macos::DirectoryCancellationToken stop) noexcept override {
        requests.push_back(request);
        if (stop.stop_requested()) {
            return community::DirectoryError{
                community::DirectoryErrorCode::cancelled,
                "directory request cancelled"};
        }
        if (responses.empty()) {
            return community::DirectoryError{
                community::DirectoryErrorCode::network_failure,
                "missing fake response"};
        }
        auto response = std::move(responses.front());
        responses.erase(responses.begin());
        return response;
    }

    std::vector<platform::macos::DirectoryHttpRequest> requests;
    std::vector<platform::macos::DirectoryHttpResult> responses;
};

struct KeychainItem {
    std::string service =
        "catro-test-" + std::to_string(getpid()) + "-" +
        std::to_string(sequence.fetch_add(1, std::memory_order_relaxed));
    std::string account = "install";

    ~KeychainItem() {
        NSDictionary* query = @{
            (__bridge id)kSecClass : (__bridge id)kSecClassGenericPassword,
            (__bridge id)kSecAttrService :
                [NSString stringWithUTF8String:service.c_str()],
            (__bridge id)kSecAttrAccount :
                [NSString stringWithUTF8String:account.c_str()],
        };
        SecItemDelete((__bridge CFDictionaryRef)query);
    }

    inline static std::atomic_uint64_t sequence{0};
};

template <class Result, class Function>
Result off_main(Function&& function) {
    Result result;
    std::thread worker([&] { result = function(); });
    worker.join();
    return result;
}

community::Identity identity() {
    community::Identity value;
    value.id.bytes[0] = std::byte{1};
    value.display_name = "Owner";
    return value;
}

community::PersonalServer personal_server() {
    community::PersonalServer value;
    value.id.bytes[0] = std::byte{2};
    value.owner_id.bytes[0] = std::byte{1};
    value.name = "Catro";
    community::Channel text;
    text.id.bytes[0] = std::byte{3};
    text.name = "general";
    text.kind = community::ChannelKind::text;
    community::Channel voice;
    voice.id.bytes[0] = std::byte{4};
    voice.name = "Voice";
    voice.kind = community::ChannelKind::voice;
    value.channels = {text, voice};
    value.members = {{value.owner_id, community::ServerRole::owner}};
    return value;
}

} // namespace

TEST_CASE("macOS directory credential is stable and never creates a plaintext file") {
    KeychainItem keychain;
    const auto directory =
        std::filesystem::temp_directory_path() /
        ("catro-keychain-test-" + std::to_string(getpid()));
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
    std::filesystem::create_directories(directory);
    const auto previous = std::filesystem::current_path();
    std::filesystem::current_path(directory);

    const auto first = platform::macos::load_or_create_directory_credential(
        keychain.service, keychain.account);
    const auto second = platform::macos::load_or_create_directory_credential(
        keychain.service, keychain.account);

    std::filesystem::current_path(previous);
    REQUIRE(std::holds_alternative<std::string>(first));
    REQUIRE(std::holds_alternative<std::string>(second));
    CHECK(std::get<std::string>(first) == std::get<std::string>(second));
    CHECK(std::get<std::string>(first).size() == 43);
    CHECK(std::filesystem::is_empty(directory));
    std::filesystem::remove_all(directory, ignored);
}

TEST_CASE("macOS directory client rejects production HTTP before transport") {
    FakeTransport transport;
    platform::macos::DirectoryClient client(
        community::DirectoryServiceConfig{"http://catro.example.com", false},
        transport);

    const auto result = off_main<community::DirectoryServersResult>(
        [&] { return client.list_servers("token"); });

    CHECK(std::get<community::DirectoryError>(result).code ==
          community::DirectoryErrorCode::invalid_config);
    CHECK(transport.requests.empty());
}

TEST_CASE("macOS directory client keeps network and parsing off the main thread") {
    FakeTransport transport;
    platform::macos::DirectoryClient client(
        community::DirectoryServiceConfig{"https://catro.example.com", false},
        transport);

    const auto result = client.list_servers("token");

    REQUIRE(std::holds_alternative<community::DirectoryError>(result));
    CHECK(std::get<community::DirectoryError>(result).code ==
          community::DirectoryErrorCode::wrong_thread);
    CHECK(transport.requests.empty());
}

TEST_CASE("macOS directory client maps status and malformed responses actionably") {
    FakeTransport transport;
    transport.responses = {
        platform::macos::DirectoryHttpResponse{
            401, R"({"error":"credential expired"})"},
        platform::macos::DirectoryHttpResponse{200, "{"},
    };
    platform::macos::DirectoryClient client(
        community::DirectoryServiceConfig{"https://catro.example.com", false},
        transport);

    const auto unauthorized = off_main<community::DirectoryServersResult>(
        [&] { return client.list_servers("token"); });
    const auto malformed = off_main<community::DirectoryServersResult>(
        [&] { return client.list_servers("token"); });

    CHECK(std::get<community::DirectoryError>(unauthorized).code ==
          community::DirectoryErrorCode::unauthorized);
    CHECK(std::get<community::DirectoryError>(unauthorized).message ==
          "credential expired");
    CHECK(std::get<community::DirectoryError>(malformed).code ==
          community::DirectoryErrorCode::malformed_response);
}

TEST_CASE("macOS directory operations use the production endpoint contract") {
    constexpr auto server =
        R"({"id":"server-1","owner_id":"user-1","name":"Catro","public_code":"CAT-1234-5678-9ABC-DEF0-1234","text_channel_id":"text-1","voice_channel_id":"voice-1","role":"owner","member_count":1})";
    constexpr auto join_request =
        R"({"id":"request-1","server_name":"Catro","public_code":"CAT-1234-5678-9ABC-DEF0-1234","requester_display_name":"Guest","message":"hello","status":"pending","created_at":100,"updated_at":100,"expires_at":200})";
    constexpr auto message =
        R"({"id":"message-1","sequence":1,"server_id":"server-1","channel_id":"text-1","author_id":"user-1","author_display_name":"Owner","content":"hello","created_at":100})";
    FakeTransport transport;
    transport.responses = {
        platform::macos::DirectoryHttpResponse{200, R"({"access_token":"token"})"},
        platform::macos::DirectoryHttpResponse{200, server},
        platform::macos::DirectoryHttpResponse{200, R"({"servers":[]})"},
        platform::macos::DirectoryHttpResponse{
            200, std::string{R"({"code":"INVITE","expires":200,"server":)"} + server + "}"},
        platform::macos::DirectoryHttpResponse{200, server},
        platform::macos::DirectoryHttpResponse{
            200,
            R"({"members":[{"user_id":"user-1","display_name":"Owner","role":"owner"}]})"},
        platform::macos::DirectoryHttpResponse{
            200,
            R"({"public_code":"CAT-1234-5678-9ABC-DEF0-1234","name":"Catro","member_count":1,"relationship":"none","request_id":""})"},
        platform::macos::DirectoryHttpResponse{200, join_request},
        platform::macos::DirectoryHttpResponse{
            200, std::string{R"({"requests":[)"} + join_request + "]}"},
        platform::macos::DirectoryHttpResponse{
            200, std::string{R"({"requests":[)"} + join_request + "]}"},
        platform::macos::DirectoryHttpResponse{200, join_request},
        platform::macos::DirectoryHttpResponse{200, join_request},
        platform::macos::DirectoryHttpResponse{
            200, std::string{R"({"messages":[)"} + message + R"(],"next_after":1})"},
        platform::macos::DirectoryHttpResponse{200, message},
        platform::macos::DirectoryHttpResponse{
            200,
            R"({"token":"rtc","expires":200,"server_id":"server-1","channel_id":"voice-1","peer_id":"user-1","signaling_url":"wss://catro.example.com/v1/rtc","ice_servers":["stun:turn.example.com:3478"],"max_room_peers":4})"},
    };
    platform::macos::DirectoryClient client(
        community::DirectoryServiceConfig{"https://catro.example.com/api", false},
        transport);

    const auto registered = off_main<community::DirectoryStringResult>(
        [&] { return client.register_identity(identity(), "credential"); });
    const auto synced = off_main<community::DirectoryServerResult>(
        [&] { return client.sync_personal_server("token", personal_server()); });
    const auto listed = off_main<community::DirectoryServersResult>(
        [&] { return client.list_servers("token"); });
    const auto invited = off_main<community::DirectoryInviteResult>(
        [&] { return client.create_invite("token", "server-1"); });
    const auto accepted = off_main<community::DirectoryServerResult>(
        [&] { return client.accept_invite("token", "INVITE"); });
    const auto members = off_main<community::DirectoryMembersResult>(
        [&] { return client.list_members("token", "server-1"); });
    const auto lookup = off_main<community::DirectoryServerLookupResult>(
        [&] {
            return client.lookup_server(
                "token", "CAT-1234-5678-9ABC-DEF0-1234");
        });
    const auto requested = off_main<community::DirectoryJoinRequestResult>(
        [&] {
            return client.create_join_request(
                "token", "CAT-1234-5678-9ABC-DEF0-1234", "hello");
        });
    const auto outgoing = off_main<community::DirectoryJoinRequestsResult>(
        [&] { return client.list_outgoing_join_requests("token"); });
    const auto pending = off_main<community::DirectoryJoinRequestsResult>(
        [&] {
            return client.list_pending_join_requests("token", "server-1");
        });
    const auto decided = off_main<community::DirectoryJoinRequestResult>(
        [&] {
            return client.decide_join_request("token", "request-1", true);
        });
    const auto cancelled = off_main<community::DirectoryJoinRequestResult>(
        [&] {
            return client.cancel_join_request("token", "request-1");
        });
    const auto messages = off_main<community::DirectoryMessagesResult>(
        [&] {
            return client.list_messages(
                "token", "server-1", "text-1", 0, 100);
        });
    const auto sent = off_main<community::DirectoryMessageResult>(
        [&] {
            return client.send_message(
                "token", "server-1", "text-1", "hello");
        });
    const auto rtc = off_main<community::RtcProvisioningResult>(
        [&] {
            return client.request_rtc_provisioning(
                "token", "server-1", "voice-1");
        });

    REQUIRE(std::holds_alternative<std::string>(registered));
    REQUIRE(std::holds_alternative<community::DirectoryServer>(synced));
    REQUIRE(std::holds_alternative<community::DirectoryServers>(listed));
    REQUIRE(std::holds_alternative<community::DirectoryInvite>(invited));
    REQUIRE(std::holds_alternative<community::DirectoryServer>(accepted));
    REQUIRE(std::holds_alternative<community::DirectoryMembers>(members));
    REQUIRE(std::holds_alternative<community::DirectoryServerLookup>(lookup));
    REQUIRE(std::holds_alternative<community::DirectoryJoinRequest>(requested));
    REQUIRE(std::holds_alternative<community::DirectoryJoinRequests>(outgoing));
    REQUIRE(std::holds_alternative<community::DirectoryJoinRequests>(pending));
    REQUIRE(std::holds_alternative<community::DirectoryJoinRequest>(decided));
    REQUIRE(std::holds_alternative<community::DirectoryJoinRequest>(cancelled));
    REQUIRE(std::holds_alternative<community::DirectoryMessagePage>(messages));
    REQUIRE(std::holds_alternative<community::DirectoryMessage>(sent));
    REQUIRE(std::holds_alternative<community::RtcProvisioning>(rtc));
    REQUIRE(transport.requests.size() == 15);
    CHECK(transport.requests[0].method == "POST");
    CHECK(transport.requests[0].endpoint == "/v1/users/register");
    CHECK(transport.requests[1].endpoint == "/v1/servers/sync");
    CHECK(transport.requests[2].method == "GET");
    CHECK(transport.requests[2].endpoint == "/v1/servers");
    CHECK(transport.requests[3].endpoint == "/v1/invites");
    CHECK(transport.requests[4].endpoint == "/v1/invites/accept");
    CHECK(transport.requests[5].endpoint == "/v1/members?server_id=server-1");
    CHECK(transport.requests[6].endpoint ==
          "/v1/server-lookup?code=CAT-1234-5678-9ABC-DEF0-1234");
    CHECK(transport.requests[7].endpoint == "/v1/join-requests");
    CHECK(transport.requests[8].endpoint == "/v1/join-requests?mine=1");
    CHECK(transport.requests[9].endpoint ==
          "/v1/join-requests?server_id=server-1");
    CHECK(transport.requests[10].endpoint == "/v1/join-requests/decision");
    CHECK(transport.requests[11].endpoint == "/v1/join-requests/cancel");
    CHECK(transport.requests[12].endpoint ==
          "/v1/messages?server_id=server-1&channel_id=text-1&after=0&limit=100");
    CHECK(transport.requests[13].endpoint == "/v1/messages");
    CHECK(transport.requests[14].endpoint == "/v1/rtc-token");
}

TEST_CASE("macOS directory requests honor cancellation before transport") {
    FakeTransport transport;
    platform::macos::DirectoryClient client(
        community::DirectoryServiceConfig{"https://catro.example.com", false},
        transport);
    platform::macos::DirectoryCancellationSource stop;
    stop.request_stop();

    const auto result = off_main<community::DirectoryServersResult>(
        [&] { return client.list_servers("token", stop.get_token()); });

    REQUIRE(std::holds_alternative<community::DirectoryError>(result));
    CHECK(std::get<community::DirectoryError>(result).code ==
          community::DirectoryErrorCode::cancelled);
    CHECK(transport.requests.empty());
}
