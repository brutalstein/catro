#pragma once

#include <catro/community/model.hpp>

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
    std::string voice_channel_id;
    std::string role;
};

struct DirectoryInvite {
    std::string code;
    std::int64_t expires = 0;
    DirectoryServer server;
};

struct RtcProvisioning {
    std::string token;
    std::int64_t expires = 0;
    std::string server_id;
    std::string channel_id;
    std::string peer_id;
    std::string signaling_url;
    std::vector<std::string> ice_servers;
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

[[nodiscard]] RtcProvisioningResult request_rtc_provisioning(
    const DirectoryServiceConfig& service,
    std::string_view access_token,
    std::string_view server_id,
    std::string_view channel_id) noexcept;

} // namespace catro::platform::windows
