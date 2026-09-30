#pragma once

#include <catro/community/directory.hpp>

#include <atomic>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace catro::platform::macos {

class DirectoryCancellationToken {
public:
    DirectoryCancellationToken() noexcept = default;

    [[nodiscard]] bool stop_requested() const noexcept {
        return state_ != nullptr &&
               state_->load(std::memory_order_acquire);
    }

private:
    explicit DirectoryCancellationToken(
        std::shared_ptr<std::atomic_bool> state) noexcept
        : state_(std::move(state)) {}

    std::shared_ptr<std::atomic_bool> state_;

    friend class DirectoryCancellationSource;
};

class DirectoryCancellationSource {
public:
    DirectoryCancellationSource()
        : state_(std::make_shared<std::atomic_bool>(false)) {}

    [[nodiscard]] DirectoryCancellationToken get_token() const noexcept {
        return DirectoryCancellationToken{state_};
    }

    [[nodiscard]] bool request_stop() noexcept {
        return state_ != nullptr &&
               !state_->exchange(true, std::memory_order_acq_rel);
    }

private:
    std::shared_ptr<std::atomic_bool> state_;
};

struct DirectoryHttpRequest {
    std::string method;
    std::string endpoint;
    std::string access_token;
    std::string body;

    friend bool operator==(const DirectoryHttpRequest&, const DirectoryHttpRequest&) = default;
};

struct DirectoryHttpResponse {
    int status = 0;
    std::string body;

    friend bool operator==(const DirectoryHttpResponse&, const DirectoryHttpResponse&) = default;
};

using DirectoryHttpResult =
    std::variant<DirectoryHttpResponse, community::DirectoryError>;

class DirectoryHttpTransport {
public:
    virtual ~DirectoryHttpTransport() = default;

    [[nodiscard]] virtual DirectoryHttpResult request(
        const DirectoryHttpRequest& request,
        DirectoryCancellationToken stop) noexcept = 0;
};

// Synchronous control-plane facade for background workers. Calls made from the macOS main thread
// are rejected; native networking remains cancellable through the supplied stop token.
class DirectoryClient {
public:
    DirectoryClient(
        community::DirectoryServiceConfig service,
        DirectoryHttpTransport& transport) noexcept;

    [[nodiscard]] community::DirectoryStringResult register_identity(
        const community::Identity& identity,
        std::string_view credential,
        DirectoryCancellationToken stop = {}) noexcept;
    [[nodiscard]] community::DirectoryServerResult sync_personal_server(
        std::string_view access_token,
        const community::PersonalServer& server,
        DirectoryCancellationToken stop = {}) noexcept;
    [[nodiscard]] community::DirectoryServersResult list_servers(
        std::string_view access_token,
        DirectoryCancellationToken stop = {}) noexcept;
    [[nodiscard]] community::DirectoryInviteResult create_invite(
        std::string_view access_token,
        std::string_view server_id,
        DirectoryCancellationToken stop = {}) noexcept;
    [[nodiscard]] community::DirectoryServerResult accept_invite(
        std::string_view access_token,
        std::string_view invite_code,
        DirectoryCancellationToken stop = {}) noexcept;
    [[nodiscard]] community::DirectoryMembersResult list_members(
        std::string_view access_token,
        std::string_view server_id,
        DirectoryCancellationToken stop = {}) noexcept;
    [[nodiscard]] community::DirectoryServerLookupResult lookup_server(
        std::string_view access_token,
        std::string_view server_code,
        DirectoryCancellationToken stop = {}) noexcept;
    [[nodiscard]] community::DirectoryJoinRequestResult create_join_request(
        std::string_view access_token,
        std::string_view server_code,
        std::string_view message,
        DirectoryCancellationToken stop = {}) noexcept;
    [[nodiscard]] community::DirectoryJoinRequestsResult list_outgoing_join_requests(
        std::string_view access_token,
        DirectoryCancellationToken stop = {}) noexcept;
    [[nodiscard]] community::DirectoryJoinRequestsResult list_pending_join_requests(
        std::string_view access_token,
        std::string_view server_id,
        DirectoryCancellationToken stop = {}) noexcept;
    [[nodiscard]] community::DirectoryJoinRequestResult decide_join_request(
        std::string_view access_token,
        std::string_view request_id,
        bool approve,
        DirectoryCancellationToken stop = {}) noexcept;
    [[nodiscard]] community::DirectoryJoinRequestResult cancel_join_request(
        std::string_view access_token,
        std::string_view request_id,
        DirectoryCancellationToken stop = {}) noexcept;
    [[nodiscard]] community::DirectoryMessagesResult list_messages(
        std::string_view access_token,
        std::string_view server_id,
        std::string_view channel_id,
        std::uint64_t after = 0,
        std::size_t limit = community::kMaxMessagePage,
        DirectoryCancellationToken stop = {}) noexcept;
    [[nodiscard]] community::DirectoryMessageResult send_message(
        std::string_view access_token,
        std::string_view server_id,
        std::string_view channel_id,
        std::string_view content,
        DirectoryCancellationToken stop = {}) noexcept;
    [[nodiscard]] community::RtcProvisioningResult request_rtc_provisioning(
        std::string_view access_token,
        std::string_view server_id,
        std::string_view channel_id,
        DirectoryCancellationToken stop = {}) noexcept;

private:
    community::DirectoryServiceConfig service_;
    DirectoryHttpTransport* transport_ = nullptr;
};

[[nodiscard]] community::DirectoryConfigResult
load_directory_service_config() noexcept;

[[nodiscard]] community::DirectoryStringResult load_or_create_directory_credential(
    std::string_view keychain_service = "com.brutalstein.catro.directory",
    std::string_view keychain_account = "install-v1") noexcept;

[[nodiscard]] std::unique_ptr<DirectoryHttpTransport>
make_foundation_directory_http_transport(
    community::DirectoryServiceConfig service) noexcept;

} // namespace catro::platform::macos
