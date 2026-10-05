#include <catro/community/directory.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <variant>

using namespace catro::community;

namespace {

constexpr auto kServer =
    R"({"id":"server-1","owner_id":"user-1","name":"Catro","public_code":"CAT-1234-5678-9ABC-DEF0-1234","text_channel_id":"text-1","voice_channel_id":"voice-1","role":"owner","member_count":2})";
constexpr auto kMember =
    R"({"user_id":"user-1","display_name":"Owner","role":"owner"})";
constexpr auto kJoinRequest =
    R"({"id":"request-1","server_name":"Catro","public_code":"CAT-1234-5678-9ABC-DEF0-1234","requester_display_name":"Guest","message":"hello","status":"pending","created_at":100,"updated_at":100,"expires_at":200})";
constexpr auto kMessage =
    R"({"id":"message-1","sequence":1,"server_id":"server-1","channel_id":"text-1","author_id":"user-1","author_display_name":"Owner","content":"hello","created_at":100})";

template <class Value>
const DirectoryError& failure(const Value& value) {
    REQUIRE(std::holds_alternative<DirectoryError>(value));
    return std::get<DirectoryError>(value);
}

} // namespace

TEST_CASE("production directory URLs reject secret-bearing and local endpoints") {
    CHECK_FALSE(validate_directory_service_config(
        DirectoryServiceConfig{"https://catro.example.com/api", false}));

    for (const auto* url : {
             "http://catro.example.com",
             "https://user:secret@catro.example.com",
             "https://catro.example.com?token=secret",
             "https://catro.example.com/#secret",
             "https://localhost",
             "https://127.0.0.1",
             "https://[::1]",
             "ftp://catro.example.com",
             "https:///missing-host",
         }) {
        INFO(url);
        const auto result = validate_directory_service_config(
            DirectoryServiceConfig{url, false});
        REQUIRE(result);
        CHECK(result->code == DirectoryErrorCode::invalid_config);
    }

    CHECK_FALSE(validate_directory_service_config(
        DirectoryServiceConfig{"http://127.0.0.1:8443", true}));
}

TEST_CASE("directory server payloads enforce every field and list uniqueness") {
    const auto parsed = parse_directory_server(kServer);
    REQUIRE(std::holds_alternative<DirectoryServer>(parsed));
    CHECK(std::get<DirectoryServer>(parsed).voice_channel_id == "voice-1");

    CHECK(failure(parse_directory_server("{")).code ==
          DirectoryErrorCode::malformed_response);
    CHECK(failure(parse_directory_server(
              R"({"id":"server-1"})"))
              .code == DirectoryErrorCode::malformed_response);
    CHECK(failure(parse_directory_server(
              R"({"id":"server-1","owner_id":"user-1","name":"Catro","public_code":"","text_channel_id":"text-1","voice_channel_id":"voice-1","role":"owner","member_count":2})"))
              .code == DirectoryErrorCode::malformed_response);
    CHECK(failure(parse_directory_server(
              R"({"id":"server-1","owner_id":"user-1","name":"Catro","public_code":"","text_channel_id":"text-1","voice_channel_id":"voice-1","role":"admin","member_count":2})"))
              .code == DirectoryErrorCode::malformed_response);

    const std::string over_name(kMaxServerNameBytes + 1, 'x');
    CHECK(failure(parse_directory_server(
              std::string{
                  R"({"id":"server-1","owner_id":"user-1","name":")"} +
              over_name +
              R"(","public_code":"CAT-1234-5678-9ABC-DEF0-1234","text_channel_id":"text-1","voice_channel_id":"voice-1","role":"owner","member_count":2})"))
              .code == DirectoryErrorCode::malformed_response);

    CHECK(failure(parse_directory_servers(
              std::string{R"({"servers":[)"} + kServer + "," + kServer + "]}"))
              .code == DirectoryErrorCode::malformed_response);
}

TEST_CASE("directory member rosters require one first owner and unique bounded members") {
    const auto valid = parse_directory_members(
        std::string{R"({"members":[)"} + kMember +
        R"(,{"user_id":"user-2","display_name":"Member","role":"member"}]})");
    REQUIRE(std::holds_alternative<DirectoryMembers>(valid));
    CHECK(std::get<DirectoryMembers>(valid).size() == 2);

    CHECK(failure(parse_directory_members(
              std::string{R"({"members":[)"} + kMember + "," + kMember + "]}"))
              .code == DirectoryErrorCode::malformed_response);
    CHECK(failure(parse_directory_members(
              R"({"members":[{"user_id":"user-2","display_name":"Member","role":"member"},{"user_id":"user-1","display_name":"Owner","role":"owner"}]})"))
              .code == DirectoryErrorCode::malformed_response);
    CHECK(failure(parse_directory_members(
              R"({"members":[{"user_id":"user-1","display_name":"Owner","role":"admin"}]})"))
              .code == DirectoryErrorCode::malformed_response);

    const std::string over_name(kMaxDisplayNameBytes + 1, 'x');
    CHECK(failure(parse_directory_members(
              std::string{R"({"members":[{"user_id":"user-1","display_name":")"} +
              over_name + R"(","role":"owner"}]})"))
              .code == DirectoryErrorCode::malformed_response);
}

TEST_CASE("directory member voice presence validates optional channel identities") {
    for (const auto& field : {std::string{}, std::string{R"(,"voice_channel_id":"")"},
                              std::string{R"(,"voice_channel_id":"voice-1")"}}) {
        const auto result = parse_directory_members(
            R"({"members":[{"user_id":"user-1","display_name":"Owner","role":"owner")" +
            field + "}]}");
        REQUIRE(std::holds_alternative<DirectoryMembers>(result));
        CHECK(std::get<DirectoryMembers>(result).front().voice_channel_id ==
              (field.find("voice-1") == std::string::npos ? "" : "voice-1"));
    }
    CHECK(failure(parse_directory_members(
              R"({"members":[{"user_id":"user-1","display_name":"Owner","role":"owner","voice_channel_id":17}]})"))
              .code == DirectoryErrorCode::malformed_response);
    CHECK(failure(parse_directory_members(
              R"({"members":[{"user_id":"user-1","display_name":"Owner","role":"owner","voice_channel_id":"invalid/channel"}]})"))
              .code == DirectoryErrorCode::malformed_response);
    CHECK(failure(parse_directory_members(
              R"({"members":[{"user_id":"user-1","display_name":"Owner","role":"owner","voice_channel_id":null}]})"))
              .code == DirectoryErrorCode::malformed_response);
}

TEST_CASE("directory lookup and join requests enforce codes roles bounds and uniqueness") {
    const auto lookup = parse_directory_server_lookup(
        R"({"public_code":"CAT-1234-5678-9ABC-DEF0-1234","name":"Catro","member_count":2,"relationship":"pending","request_id":"request-1"})");
    REQUIRE(std::holds_alternative<DirectoryServerLookup>(lookup));

    CHECK(failure(parse_directory_server_lookup(
              R"({"public_code":"CAT-1234-5678-9ABC-DEF0-1234","name":"Catro","member_count":2,"relationship":"admin","request_id":""})"))
              .code == DirectoryErrorCode::malformed_response);
    CHECK(failure(parse_directory_join_request(
              R"({"id":"request-1","server_name":"Catro","public_code":"CAT-1234-5678-9ABC-DEF0-1234","requester_display_name":"Guest","message":"","status":"waiting","created_at":100,"updated_at":100,"expires_at":200})"))
              .code == DirectoryErrorCode::malformed_response);

    const std::string over_message(kMaxJoinRequestMessageBytes + 1, 'x');
    CHECK(failure(parse_directory_join_request(
              std::string{
                  R"({"id":"request-1","server_name":"Catro","public_code":"CAT-1234-5678-9ABC-DEF0-1234","requester_display_name":"Guest","message":")"} +
              over_message +
              R"(","status":"pending","created_at":100,"updated_at":100,"expires_at":200})"))
              .code == DirectoryErrorCode::malformed_response);
    CHECK(failure(parse_directory_join_requests(
              std::string{R"({"requests":[)"} + kJoinRequest + "," + kJoinRequest + "]}"))
              .code == DirectoryErrorCode::malformed_response);
}

TEST_CASE("directory messages enforce content ordering identity and cursor bounds") {
    const auto message = parse_directory_message(kMessage);
    REQUIRE(std::holds_alternative<DirectoryMessage>(message));

    const std::string over_content(kMaxMessageContentBytes + 1, 'x');
    CHECK(failure(parse_directory_message(
              std::string{
                  R"({"id":"message-1","sequence":1,"server_id":"server-1","channel_id":"text-1","author_id":"user-1","author_display_name":"Owner","content":")"} +
              over_content + R"(","created_at":100})"))
              .code == DirectoryErrorCode::malformed_response);
    CHECK(failure(parse_directory_messages(
              std::string{R"({"messages":[)"} + kMessage + "," + kMessage +
                  R"(],"next_after":1})",
              "server-1",
              "text-1",
              0,
              100))
              .code == DirectoryErrorCode::malformed_response);
    CHECK(failure(parse_directory_messages(
              std::string{R"({"messages":[)"} + kMessage + R"(],"next_after":2})",
              "server-1",
              "text-1",
              0,
              100))
              .code == DirectoryErrorCode::malformed_response);
}

TEST_CASE("directory invite token and RTC payloads are strictly bounded") {
    const auto invite = parse_directory_invite(
        std::string{
            R"({"code":"CATRO-INVITE","expires":200,"server":)"} + kServer + "}");
    REQUIRE(std::holds_alternative<DirectoryInvite>(invite));

    const auto token = parse_directory_access_token(R"({"access_token":"token-1"})");
    REQUIRE(std::holds_alternative<std::string>(token));
    CHECK(std::get<std::string>(token) == "token-1");

    const auto rtc = parse_rtc_provisioning(
        R"({"token":"rtc-token","expires":200,"server_id":"server-1","channel_id":"voice-1","peer_id":"user-1","signaling_url":"wss://catro.example.com/v1/rtc","ice_servers":["stun:turn.example.com:3478","turns:turn.example.com:5349?transport=tls"],"max_room_peers":4,"allow_insecure_signaling":false,"allow_no_turn":false})");
    REQUIRE(std::holds_alternative<RtcProvisioning>(rtc));
    CHECK(std::get<RtcProvisioning>(rtc).max_room_peers == 4);

    CHECK(failure(parse_directory_access_token(R"({"access_token":""})")).code ==
          DirectoryErrorCode::malformed_response);
    CHECK(failure(parse_directory_invite(
              std::string{
                  R"({"code":"","expires":0,"server":)"} + kServer + "}"))
              .code == DirectoryErrorCode::malformed_response);
    CHECK(failure(parse_rtc_provisioning(
              R"({"token":"rtc-token","expires":200,"server_id":"server-1","channel_id":"voice-1","peer_id":"user-1","signaling_url":"ws://catro.example.com/v1/rtc","ice_servers":[],"max_room_peers":6})"))
              .code == DirectoryErrorCode::malformed_response);
}

TEST_CASE("directory HTTP errors keep actionable server status and messages") {
    const auto unauthorized = map_directory_http_error(401, R"({"error":"credential expired"})");
    CHECK(unauthorized.code == DirectoryErrorCode::unauthorized);
    CHECK(unauthorized.http_status == 401);
    CHECK(unauthorized.message == "credential expired");

    const auto rejected = map_directory_http_error(409, "{");
    CHECK(rejected.code == DirectoryErrorCode::rejected);
    CHECK(rejected.http_status == 409);
    CHECK(rejected.message == "directory request rejected");
}
