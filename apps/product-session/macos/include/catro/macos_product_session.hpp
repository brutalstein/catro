#pragma once

#include <PresentationState.hpp>
#include <catro/community/directory.hpp>
#include <catro/community/model.hpp>
#include <catro/platform/macos/directory_client.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace catro::product {

inline constexpr std::size_t kMaxRetainedMessages = 512;
// The shell ticks poll() once per second; members and join requests refresh every fifth tick,
// matching the Windows 1 s message / 5 s member and request cadence.
inline constexpr std::uint32_t kSlowPollTicks = 5;

struct ServerItem {
    std::string id;
    std::string name;
    std::string public_code;
    std::string text_channel_id;
    std::string voice_channel_id;
    bool owner = false;
    std::size_t member_count = 0;

    friend bool operator==(const ServerItem&, const ServerItem&) = default;
};

struct MemberItem {
    std::string user_id;
    std::string display_name;
    bool owner = false;
    bool self = false;
};

struct MessageItem {
    std::uint64_t sequence = 0;
    std::string author;
    std::string content;
    std::int64_t created_at_ms = 0;
};

struct JoinRequestItem {
    std::string id;
    std::string requester;
    std::string message;
};

struct ServerLookupItem {
    std::string public_code;
    std::string name;
    std::size_t member_count = 0;
    std::string relationship;
};

// Immutable copy published after every completed command; the shell never sees live state.
struct ProductSnapshot {
    std::uint64_t revision = 0;
    app::WorkspaceSnapshot workspace;
    std::string identity_id;
    std::string identity_name;
    std::vector<ServerItem> servers;
    std::string active_server_id;
    std::vector<MemberItem> members;
    std::vector<MessageItem> messages;
    std::vector<JoinRequestItem> pending_requests;
    std::string invite_code;
    std::optional<ServerLookupItem> lookup;
    // One-shot user-facing outcome of the last command (request sent, invite rejected, ...).
    std::string notice;

    [[nodiscard]] const ServerItem* active_server() const noexcept;
};

struct ProductSessionDependencies {
    // Absent when the local profile could not be loaded; the session then stays failed.
    std::optional<community::LocalState> local_state;
    std::function<community::DirectoryConfigResult()> load_config;
    std::function<community::DirectoryStringResult()> load_credential;
    std::function<std::unique_ptr<platform::macos::DirectoryHttpTransport>(
        const community::DirectoryServiceConfig&)>
        make_transport;
};

// Directory session of the macOS product shell. Every command runs on one serial worker thread,
// so a result can only apply to the server that is active when it runs; listener receives a fresh
// snapshot on that worker after each command.
// ponytail: one serial worker, a slow request delays later commands; split read/write workers if
// interaction latency matters.
class ProductSession final {
public:
    using Listener = std::function<void(const ProductSnapshot&)>;

    ProductSession(ProductSessionDependencies dependencies, Listener listener);
    ~ProductSession();

    ProductSession(const ProductSession&) = delete;
    ProductSession& operator=(const ProductSession&) = delete;

    void start();
    void select_server(std::string server_id);
    // Coalesced: a tick is dropped while the previous one is still queued.
    void poll();
    void send_message(std::string content);
    void create_invite();
    void accept_invite(std::string code);
    void lookup_server(std::string server_code);
    void request_join(std::string server_code, std::string note);
    void decide_request(std::string request_id, bool approve);
    // Cancels in-flight network work and joins the worker; no listener call follows.
    void stop() noexcept;

    [[nodiscard]] ProductSnapshot snapshot() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace catro::product
