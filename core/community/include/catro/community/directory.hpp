#pragma once

#include <catro/community/model.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace catro::community {

inline constexpr std::size_t kMaxDirectoryUrlBytes = 2048;
inline constexpr std::size_t kMaxDirectoryTokenBytes = 16U * 1024U;
inline constexpr std::size_t kMaxDirectoryResponseBytes = 256U * 1024U;
inline constexpr std::size_t kMaxDirectoryServers = 256;
inline constexpr std::size_t kMaxJoinRequestMessageBytes = 280;
inline constexpr std::size_t kMaxJoinRequestPage = 64;
inline constexpr std::size_t kMaxMessageContentBytes = 2000;
inline constexpr std::size_t kMaxMessagePage = 100;
inline constexpr std::size_t kMaxIceServers = 16;

enum class DirectoryErrorCode : std::uint8_t {
    not_configured,
    invalid_config,
    credential_failure,
    network_failure,
    unauthorized,
    rejected,
    malformed_response,
    cancelled,
    wrong_thread,
};

struct DirectoryError {
    DirectoryErrorCode code = DirectoryErrorCode::network_failure;
    std::string message;
    std::int64_t native_code = 0;
    int http_status = 0;

    friend bool operator==(const DirectoryError&, const DirectoryError&) = default;
};

struct DirectoryServiceConfig {
    std::string api_base_url;
    bool allow_insecure_http = false;

    friend bool operator==(const DirectoryServiceConfig&, const DirectoryServiceConfig&) = default;
};

struct DirectoryServer {
    std::string id;
    std::string owner_id;
    std::string name;
    std::string public_code;
    std::string text_channel_id;
    std::string voice_channel_id;
    std::string role;
    std::size_t member_count = 0;

    friend bool operator==(const DirectoryServer&, const DirectoryServer&) = default;
};

using DirectoryServers = std::vector<DirectoryServer>;

struct DirectoryInvite {
    std::string code;
    std::int64_t expires = 0;
    DirectoryServer server;

    friend bool operator==(const DirectoryInvite&, const DirectoryInvite&) = default;
};

struct DirectoryMember {
    std::string user_id;
    std::string display_name;
    std::string role;
    std::string voice_channel_id{};

    friend bool operator==(const DirectoryMember&, const DirectoryMember&) = default;
};

using DirectoryMembers = std::vector<DirectoryMember>;

struct DirectoryServerLookup {
    std::string public_code;
    std::string name;
    std::size_t member_count = 0;
    std::string relationship;
    std::string request_id;

    friend bool operator==(const DirectoryServerLookup&, const DirectoryServerLookup&) = default;
};

struct DirectoryJoinRequest {
    std::string id;
    std::string server_name;
    std::string public_code;
    std::string requester_display_name;
    std::string message;
    std::string status;
    std::int64_t created_at = 0;
    std::int64_t updated_at = 0;
    std::int64_t expires_at = 0;

    friend bool operator==(const DirectoryJoinRequest&, const DirectoryJoinRequest&) = default;
};

using DirectoryJoinRequests = std::vector<DirectoryJoinRequest>;

struct DirectoryMessage {
    std::string id;
    std::uint64_t sequence = 0;
    std::string server_id;
    std::string channel_id;
    std::string author_id;
    std::string author_display_name;
    std::string content;
    std::int64_t created_at = 0;

    friend bool operator==(const DirectoryMessage&, const DirectoryMessage&) = default;
};

struct DirectoryMessagePage {
    std::vector<DirectoryMessage> messages;
    std::uint64_t next_after = 0;
    // Monotonic service-side message mutation revision. It also advances on owner deletion.
    std::uint64_t revision = 0;

    friend bool operator==(const DirectoryMessagePage&, const DirectoryMessagePage&) = default;
};

struct RtcProvisioning {
    std::string token;
    std::int64_t expires = 0;
    std::string server_id;
    std::string channel_id;
    std::string peer_id;
    std::string signaling_url;
    std::vector<std::string> ice_servers;
    std::size_t max_room_peers = 0;
    bool allow_insecure_signaling = false;
    bool allow_no_turn = false;

    friend bool operator==(const RtcProvisioning&, const RtcProvisioning&) = default;
};

using DirectoryConfigResult = std::variant<DirectoryServiceConfig, DirectoryError>;
using DirectoryStringResult = std::variant<std::string, DirectoryError>;
using DirectoryServerResult = std::variant<DirectoryServer, DirectoryError>;
using DirectoryServersResult = std::variant<DirectoryServers, DirectoryError>;
using DirectoryInviteResult = std::variant<DirectoryInvite, DirectoryError>;
using DirectoryMembersResult = std::variant<DirectoryMembers, DirectoryError>;
using DirectoryServerLookupResult = std::variant<DirectoryServerLookup, DirectoryError>;
using DirectoryJoinRequestResult = std::variant<DirectoryJoinRequest, DirectoryError>;
using DirectoryJoinRequestsResult = std::variant<DirectoryJoinRequests, DirectoryError>;
using DirectoryMessageResult = std::variant<DirectoryMessage, DirectoryError>;
using DirectoryMessagesResult = std::variant<DirectoryMessagePage, DirectoryError>;
using RtcProvisioningResult = std::variant<RtcProvisioning, DirectoryError>;

[[nodiscard]] bool valid_remote_directory_id(std::string_view value) noexcept;
[[nodiscard]] bool valid_directory_server_code(std::string_view value) noexcept;
[[nodiscard]] std::optional<DirectoryError>
validate_directory_service_config(const DirectoryServiceConfig& config) noexcept;

[[nodiscard]] DirectoryError
map_directory_http_error(int status, std::string_view body) noexcept;
[[nodiscard]] DirectoryStringResult parse_directory_access_token(std::string_view json) noexcept;
[[nodiscard]] DirectoryServerResult parse_directory_server(std::string_view json) noexcept;
[[nodiscard]] DirectoryServersResult parse_directory_servers(std::string_view json) noexcept;
[[nodiscard]] DirectoryInviteResult parse_directory_invite(std::string_view json) noexcept;
[[nodiscard]] DirectoryMembersResult parse_directory_members(std::string_view json) noexcept;
[[nodiscard]] DirectoryServerLookupResult
parse_directory_server_lookup(std::string_view json) noexcept;
[[nodiscard]] DirectoryJoinRequestResult
parse_directory_join_request(std::string_view json) noexcept;
[[nodiscard]] DirectoryJoinRequestsResult
parse_directory_join_requests(std::string_view json) noexcept;
[[nodiscard]] DirectoryMessageResult parse_directory_message(std::string_view json) noexcept;
[[nodiscard]] DirectoryMessagesResult parse_directory_messages(
    std::string_view json,
    std::string_view expected_server_id,
    std::string_view expected_channel_id,
    std::uint64_t after,
    std::size_t limit) noexcept;
[[nodiscard]] RtcProvisioningResult parse_rtc_provisioning(std::string_view json) noexcept;

} // namespace catro::community
