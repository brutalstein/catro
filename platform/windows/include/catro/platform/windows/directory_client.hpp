#pragma once

#include <catro/community/model.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace catro::platform::windows {

enum class DirectoryErrorCode : std::uint8_t {
    not_configured,
    invalid_config,
    credential_failure,
    network_failure,
    unauthorized,
    rejected,
    malformed_response,
};

struct DirectoryError {
    DirectoryErrorCode code = DirectoryErrorCode::network_failure;
    std::string message;
    std::int64_t native_code = 0;
    int http_status = 0;
};

struct DirectoryServiceConfig {
    std::string api_base_url;
    bool allow_insecure_http = false;
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
};

struct DirectoryInvite {
    std::string code;
    std::int64_t expires = 0;
    DirectoryServer server;
};

struct DirectoryMember {
    std::string user_id;
    std::string display_name;
    std::string role;
};

struct DirectoryServerLookup {
    std::string public_code;
    std::string name;
    std::size_t member_count = 0;
    std::string relationship;
    std::string request_id;
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
};

struct DirectoryMessage {
    std::string id;
    std::uint64_t sequence = 0;
    std::string server_id;
    std::string channel_id;
    std::string author_id;
    std::string author_display_name;
    std::string content;
    std::int64_t created_at = 0;
};

struct DirectoryMessagePage {
    std::vector<DirectoryMessage> messages;
    std::uint64_t next_after = 0;
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
};

using DirectoryConfigResult =
    std::variant<DirectoryServiceConfig, DirectoryError>;
using DirectoryStringResult =
    std::variant<std::string, DirectoryError>;
using DirectoryServerResult =
    std::variant<DirectoryServer, DirectoryError>;
using DirectoryServersResult =
    std::variant<std::vector<DirectoryServer>, DirectoryError>;
using DirectoryInviteResult =
    std::variant<DirectoryInvite, DirectoryError>;
using DirectoryMembersResult =
    std::variant<std::vector<DirectoryMember>, DirectoryError>;
using DirectoryServerLookupResult =
    std::variant<DirectoryServerLookup, DirectoryError>;
using DirectoryJoinRequestResult =
    std::variant<DirectoryJoinRequest, DirectoryError>;
using DirectoryJoinRequestsResult =
    std::variant<std::vector<DirectoryJoinRequest>, DirectoryError>;
using DirectoryMessageResult =
    std::variant<DirectoryMessage, DirectoryError>;
using DirectoryMessagesResult =
    std::variant<DirectoryMessagePage, DirectoryError>;
using RtcProvisioningResult =
    std::variant<RtcProvisioning, DirectoryError>;

// Reads deployment configuration from catro-network.json next to Catro.exe. CATRO_SERVICE_URL is an
// engineering override only. Production config rejects plaintext HTTP by default.
[[nodiscard]] DirectoryConfigResult load_directory_service_config() noexcept;

// Returns a stable 256-bit per-install credential encoded as base64url. The raw credential is stored
// only in a DPAPI-protected LocalAppData file and never written to plaintext JSON.
[[nodiscard]] DirectoryStringResult
load_or_create_directory_credential() noexcept;

[[nodiscard]] DirectoryStringResult register_directory_identity(
    const DirectoryServiceConfig& service,
    const community::Identity& identity,
    std::string_view credential) noexcept;

[[nodiscard]] DirectoryServerResult sync_personal_server(
    const DirectoryServiceConfig& service,
    std::string_view access_token,
    const community::PersonalServer& server) noexcept;

[[nodiscard]] DirectoryServersResult list_directory_servers(
    const DirectoryServiceConfig& service,
    std::string_view access_token) noexcept;

[[nodiscard]] DirectoryInviteResult create_directory_invite(
    const DirectoryServiceConfig& service,
    std::string_view access_token,
    std::string_view server_id) noexcept;

[[nodiscard]] DirectoryServerResult accept_directory_invite(
    const DirectoryServiceConfig& service,
    std::string_view access_token,
    std::string_view invite_code) noexcept;

[[nodiscard]] DirectoryMembersResult list_directory_members(
    const DirectoryServiceConfig& service,
    std::string_view access_token,
    std::string_view server_id) noexcept;

[[nodiscard]] DirectoryServerLookupResult lookup_directory_server(
    const DirectoryServiceConfig& service,
    std::string_view access_token,
    std::string_view server_code) noexcept;

[[nodiscard]] DirectoryJoinRequestResult create_directory_join_request(
    const DirectoryServiceConfig& service,
    std::string_view access_token,
    std::string_view server_code,
    std::string_view message) noexcept;

[[nodiscard]] DirectoryJoinRequestsResult list_outgoing_directory_join_requests(
    const DirectoryServiceConfig& service,
    std::string_view access_token) noexcept;

[[nodiscard]] DirectoryJoinRequestsResult list_pending_directory_join_requests(
    const DirectoryServiceConfig& service,
    std::string_view access_token,
    std::string_view server_id) noexcept;

[[nodiscard]] DirectoryJoinRequestResult decide_directory_join_request(
    const DirectoryServiceConfig& service,
    std::string_view access_token,
    std::string_view request_id,
    bool approve) noexcept;

[[nodiscard]] DirectoryJoinRequestResult cancel_directory_join_request(
    const DirectoryServiceConfig& service,
    std::string_view access_token,
    std::string_view request_id) noexcept;

[[nodiscard]] DirectoryMessagesResult list_directory_messages(
    const DirectoryServiceConfig& service,
    std::string_view access_token,
    std::string_view server_id,
    std::string_view channel_id,
    std::uint64_t after = 0,
    std::size_t limit = 100) noexcept;

[[nodiscard]] DirectoryMessageResult send_directory_message(
    const DirectoryServiceConfig& service,
    std::string_view access_token,
    std::string_view server_id,
    std::string_view channel_id,
    std::string_view content) noexcept;

[[nodiscard]] RtcProvisioningResult request_rtc_provisioning(
    const DirectoryServiceConfig& service,
    std::string_view access_token,
    std::string_view server_id,
    std::string_view channel_id) noexcept;

} // namespace catro::platform::windows
