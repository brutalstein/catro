#pragma once

#include <catro/community/directory.hpp>

#include <string_view>

namespace catro::platform::windows {

using community::DirectoryConfigResult;
using community::DirectoryError;
using community::DirectoryErrorCode;
using community::DirectoryInvite;
using community::DirectoryInviteResult;
using community::DirectoryJoinRequest;
using community::DirectoryJoinRequestResult;
using community::DirectoryJoinRequests;
using community::DirectoryJoinRequestsResult;
using community::DirectoryMember;
using community::DirectoryMembers;
using community::DirectoryMembersResult;
using community::DirectoryMessage;
using community::DirectoryMessagePage;
using community::DirectoryMessageResult;
using community::DirectoryMessagesResult;
using community::DirectoryServer;
using community::DirectoryServerLookup;
using community::DirectoryServerLookupResult;
using community::DirectoryServerResult;
using community::DirectoryServers;
using community::DirectoryServersResult;
using community::DirectoryServiceConfig;
using community::DirectoryStringResult;
using community::RtcProvisioning;
using community::RtcProvisioningResult;

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

[[nodiscard]] DirectoryServerResult remove_directory_member(
    const DirectoryServiceConfig& service,
    std::string_view access_token,
    std::string_view server_id,
    std::string_view user_id) noexcept;

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

[[nodiscard]] DirectoryMessageResult delete_directory_message(
    const DirectoryServiceConfig& service,
    std::string_view access_token,
    std::string_view server_id,
    std::string_view channel_id,
    std::string_view message_id) noexcept;

[[nodiscard]] RtcProvisioningResult request_rtc_provisioning(
    const DirectoryServiceConfig& service,
    std::string_view access_token,
    std::string_view server_id,
    std::string_view channel_id) noexcept;

} // namespace catro::platform::windows
