#include <catro/macos_product_session.hpp>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <exception>
#include <mutex>
#include <set>
#include <thread>
#include <utility>
#include <variant>

namespace catro::product {
namespace {

using community::DirectoryError;
using community::DirectoryErrorCode;

constexpr std::string_view kLocalOnlyMessage = "Online services are not configured. Local mode remains available.";

template <class T>
[[nodiscard]] const DirectoryError* error_of(const std::variant<T, DirectoryError>& result) noexcept {
    return std::get_if<DirectoryError>(&result);
}

[[nodiscard]] ServerItem to_item(const community::DirectoryServer& server) {
    return ServerItem{server.id,
                      server.name,
                      server.public_code,
                      server.text_channel_id,
                      server.voice_channel_id,
                      server.role == "owner",
                      server.member_count};
}

[[nodiscard]] ServerItem personal_item(const community::PersonalServer& server) {
    ServerItem item{to_hex(server.id), server.name, {}, {}, {}, true, server.members.size()};
    for (const auto& channel : server.channels) {
        auto& target = channel.kind == community::ChannelKind::text ? item.text_channel_id : item.voice_channel_id;
        if (target.empty()) {
            target = to_hex(channel.id);
        }
    }
    return item;
}

} // namespace

const ServerItem* ProductSnapshot::active_server() const noexcept {
    const auto found = std::find_if(servers.begin(), servers.end(),
                                    [this](const ServerItem& server) { return server.id == active_server_id; });
    return found == servers.end() ? nullptr : &*found;
}

struct ProductSession::Impl {
    Impl(ProductSessionDependencies dependencies, Listener listener)
        : deps_(std::move(dependencies)), listener_(std::move(listener)) {
        worker_ = std::thread([this] { run(); });
    }

    void enqueue(std::function<void()> command) {
        {
            std::scoped_lock lock(queue_mutex_);
            if (stopping_.load(std::memory_order_acquire)) {
                return;
            }
            queue_.push_back(std::move(command));
        }
        queue_ready_.notify_one();
    }

    void stop() noexcept {
        {
            std::scoped_lock lock(queue_mutex_);
            if (stopping_.exchange(true, std::memory_order_acq_rel)) {
                return;
            }
            queue_.clear();
        }
        (void)cancel_.request_stop();
        queue_ready_.notify_all();
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    void run() noexcept {
        while (true) {
            std::function<void()> command;
            {
                std::unique_lock lock(queue_mutex_);
                queue_ready_.wait(lock, [this] { return stopping_.load() || !queue_.empty(); });
                if (stopping_.load(std::memory_order_acquire)) {
                    return;
                }
                command = std::move(queue_.front());
                queue_.pop_front();
            }
            try {
                command();
            } catch (const std::exception& error) {
                state_.notice = error.what();
            } catch (...) {
                state_.notice = "Unexpected session failure.";
            }
            publish();
        }
    }

    // Listener runs on the worker without any session lock held, so it may issue new commands.
    void publish() {
        ++state_.revision;
        {
            std::scoped_lock lock(published_mutex_);
            published_ = state_;
        }
        if (!stopping_.load(std::memory_order_acquire) && listener_) {
            listener_(state_);
        }
        state_.notice.clear();
    }

    [[nodiscard]] platform::macos::DirectoryCancellationToken token() const noexcept {
        return cancel_.get_token();
    }

    [[nodiscard]] bool online() const noexcept {
        return client_ && !access_token_.empty() &&
               state_.workspace.connection == app::ConnectionState::synchronized;
    }

    void bootstrap() {
        if (bootstrapped_) {
            return;
        }
        bootstrapped_ = true;
        if (!deps_.local_state) {
            state_.workspace.fail("Local profile is unavailable.");
            return;
        }
        const auto& local = *deps_.local_state;
        state_.identity_id = to_hex(local.identity.id);
        state_.identity_name = local.identity.display_name;
        state_.servers = {personal_item(local.personal_server)};
        state_.active_server_id = state_.servers.front().id;
        state_.workspace = app::WorkspaceSnapshot::connecting();
        publish();

        auto config = deps_.load_config();
        if (const auto* error = error_of(config)) {
            state_.workspace.fail(error->code == DirectoryErrorCode::not_configured ? std::string{kLocalOnlyMessage}
                                                                                    : error->message);
            return;
        }
        const auto service = std::get<community::DirectoryServiceConfig>(config);
        auto credential = deps_.load_credential();
        if (const auto* error = error_of(credential)) {
            state_.workspace.fail("Directory credential unavailable: " + error->message);
            return;
        }
        transport_ = deps_.make_transport(service);
        if (!transport_) {
            state_.workspace.fail("Online transport could not be created.");
            return;
        }
        client_.emplace(service, *transport_);
        auto access = client_->register_identity(local.identity, std::get<std::string>(credential), token());
        if (const auto* error = error_of(access)) {
            state_.workspace.fail("Online sign-in failed: " + error->message);
            return;
        }
        access_token_ = std::get<std::string>(access);
        auto synced = client_->sync_personal_server(access_token_, local.personal_server, token());
        if (const auto* error = error_of(synced)) {
            access_token_.clear();
            state_.workspace.fail("Server sync failed: " + error->message);
            return;
        }
        state_.workspace.synchronize();
        if (!refresh_servers()) {
            state_.servers = {to_item(std::get<community::DirectoryServer>(synced))};
            state_.active_server_id = state_.servers.front().id;
        }
        activate(state_.active_server_id);
    }

    // Resets per-server state only when the server actually changes, then refreshes it.
    void activate(const std::string& server_id) {
        if (std::none_of(state_.servers.begin(), state_.servers.end(),
                         [&](const ServerItem& server) { return server.id == server_id; })) {
            return;
        }
        if (server_id != activated_id_) {
            state_.active_server_id = server_id;
            state_.members.clear();
            state_.messages.clear();
            state_.pending_requests.clear();
            state_.invite_code.clear();
            message_cursor_ = 0;
            activated_id_ = server_id;
        }
        if (online()) {
            refresh_members();
            refresh_messages();
            refresh_requests();
        }
    }

    // Keeps the active server when it still exists; otherwise falls back to the first server.
    [[nodiscard]] bool refresh_servers() {
        auto servers = client_->list_servers(access_token_, token());
        if (error_of(servers) != nullptr) {
            return false;
        }
        std::vector<ServerItem> items;
        for (const auto& server : std::get<std::vector<community::DirectoryServer>>(servers)) {
            items.push_back(to_item(server));
        }
        if (items.empty()) {
            return false;
        }
        state_.servers = std::move(items);
        if (state_.active_server() == nullptr) {
            state_.active_server_id = state_.servers.front().id;
        }
        return true;
    }

    void refresh_members() {
        auto members = client_->list_members(access_token_, state_.active_server_id, token());
        if (error_of(members) != nullptr) {
            return; // keep the last valid roster
        }
        state_.members.clear();
        for (const auto& member : std::get<std::vector<community::DirectoryMember>>(members)) {
            state_.members.push_back(MemberItem{member.user_id, member.display_name, member.role == "owner",
                                                member.user_id == state_.identity_id});
        } // the roster parser already guarantees the single owner comes first
    }

    void refresh_messages() {
        const auto* server = state_.active_server();
        if (server == nullptr || server->text_channel_id.empty()) {
            return;
        }
        auto page = client_->list_messages(access_token_, server->id, server->text_channel_id, message_cursor_,
                                           community::kMaxMessagePage, token());
        if (const auto* error = error_of(page)) {
            notify("Messages unavailable: " + error->message);
            return;
        }
        for (const auto& message : std::get<community::DirectoryMessagePage>(page).messages) {
            if (message.sequence <= message_cursor_) {
                continue;
            }
            message_cursor_ = message.sequence;
            state_.messages.push_back(
                MessageItem{message.sequence, message.author_display_name, message.content, message.created_at});
        }
        if (state_.messages.size() > kMaxRetainedMessages) {
            state_.messages.erase(state_.messages.begin(),
                                  state_.messages.end() - static_cast<std::ptrdiff_t>(kMaxRetainedMessages));
        }
    }

    void refresh_requests() {
        const auto* server = state_.active_server();
        if (server == nullptr || !server->owner) {
            state_.pending_requests.clear();
            return;
        }
        auto requests = client_->list_pending_join_requests(access_token_, server->id, token());
        if (error_of(requests) != nullptr) {
            return;
        }
        state_.pending_requests.clear();
        for (const auto& request : std::get<std::vector<community::DirectoryJoinRequest>>(requests)) {
            if (request.status == "pending") {
                state_.pending_requests.push_back(
                    JoinRequestItem{request.id, request.requester_display_name, request.message});
            }
        }
    }

    // Several outcomes can land in one command; each gets its own snapshot so none is overwritten.
    void notify(std::string notice) {
        if (!state_.notice.empty()) {
            publish();
        }
        state_.notice = std::move(notice);
    }

    // Approved requests are consumed only after the server list refresh that shows the new server.
    void refresh_outgoing() {
        auto result = client_->list_outgoing_join_requests(access_token_, token());
        if (error_of(result) != nullptr) {
            return;
        }
        const auto& requests = std::get<std::vector<community::DirectoryJoinRequest>>(result);
        const auto unseen = [this](const community::DirectoryJoinRequest& request) {
            return request.status != "pending" && !observed_requests_.contains(request.id);
        };
        const bool servers_ready =
            std::any_of(requests.begin(), requests.end(),
                        [&](const auto& request) { return unseen(request) && request.status == "approved"; }) &&
            refresh_servers();
        for (const auto& request : requests) {
            if (!unseen(request) || (request.status == "approved" && !servers_ready)) {
                continue;
            }
            observed_requests_.insert(request.id);
            notify(request.status == "approved"
                       ? "Request to " + request.server_name + " approved."
                       : "Request to " + request.server_name + " was " + request.status + ".");
        }
        if (servers_ready) {
            activate(state_.active_server_id);
        }
    }

    void poll() {
        poll_queued_.store(false, std::memory_order_release);
        if (!online()) {
            return;
        }
        refresh_messages();
        if (++poll_ticks_ % kSlowPollTicks == 0) {
            refresh_members();
            refresh_requests();
            refresh_outgoing();
        }
    }

    // Runs body only while online; otherwise records why the command cannot run.
    template <class Body>
    void online_command(Body body) {
        if (!online()) {
            state_.notice = state_.workspace.connection_message;
            return;
        }
        body();
    }

    ProductSessionDependencies deps_;
    Listener listener_;
    ProductSnapshot state_;
    std::unique_ptr<platform::macos::DirectoryHttpTransport> transport_;
    std::optional<platform::macos::DirectoryClient> client_;
    std::string access_token_;
    std::string activated_id_;
    std::uint64_t message_cursor_ = 0;
    std::uint32_t poll_ticks_ = 0;
    std::set<std::string> observed_requests_;
    bool bootstrapped_ = false;
    std::atomic_bool poll_queued_{false};

    platform::macos::DirectoryCancellationSource cancel_;
    mutable std::mutex published_mutex_;
    ProductSnapshot published_;
    std::mutex queue_mutex_;
    std::condition_variable queue_ready_;
    std::deque<std::function<void()>> queue_;
    std::atomic_bool stopping_{false};
    std::thread worker_;
};

ProductSession::ProductSession(ProductSessionDependencies dependencies, Listener listener)
    : impl_(std::make_unique<Impl>(std::move(dependencies), std::move(listener))) {}

ProductSession::~ProductSession() {
    stop();
}

void ProductSession::start() {
    impl_->enqueue([impl = impl_.get()] { impl->bootstrap(); });
}

void ProductSession::select_server(std::string server_id) {
    impl_->enqueue([impl = impl_.get(), id = std::move(server_id)] { impl->activate(id); });
}

void ProductSession::poll() {
    if (impl_->poll_queued_.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
    impl_->enqueue([impl = impl_.get()] { impl->poll(); });
}

void ProductSession::send_message(std::string content) {
    impl_->enqueue([impl = impl_.get(), content = std::move(content)] {
        impl->online_command([&] {
            const auto* server = impl->state_.active_server();
            if (server == nullptr || server->text_channel_id.empty()) {
                impl->state_.notice = "This server has no text channel.";
                return;
            }
            if (content.empty() || content.size() > community::kMaxMessageContentBytes) {
                impl->state_.notice = "Messages must be 1 to 2000 bytes.";
                return;
            }
            auto sent = impl->client_->send_message(impl->access_token_, server->id, server->text_channel_id, content,
                                                    impl->token());
            if (const auto* error = error_of(sent)) {
                impl->state_.notice = "Message not sent: " + error->message;
                return;
            }
            impl->refresh_messages();
        });
    });
}

void ProductSession::create_invite() {
    impl_->enqueue([impl = impl_.get()] {
        impl->online_command([&] {
            auto invite =
                impl->client_->create_invite(impl->access_token_, impl->state_.active_server_id, impl->token());
            if (const auto* error = error_of(invite)) {
                impl->state_.notice = "Invite unavailable: " + error->message;
                return;
            }
            impl->state_.invite_code = std::get<community::DirectoryInvite>(invite).code;
        });
    });
}

void ProductSession::accept_invite(std::string code) {
    impl_->enqueue([impl = impl_.get(), code = std::move(code)] {
        impl->online_command([&] {
            auto joined = impl->client_->accept_invite(impl->access_token_, code, impl->token());
            if (const auto* error = error_of(joined)) {
                impl->state_.notice = "Invite not accepted: " + error->message;
                return;
            }
            const auto server = std::get<community::DirectoryServer>(joined);
            if (!impl->refresh_servers()) {
                impl->state_.servers.push_back(to_item(server));
            }
            impl->activate(server.id);
            impl->state_.notice = "Joined " + server.name + ".";
        });
    });
}

void ProductSession::lookup_server(std::string server_code) {
    impl_->enqueue([impl = impl_.get(), code = std::move(server_code)] {
        impl->online_command([&] {
            impl->state_.lookup.reset();
            auto found = impl->client_->lookup_server(impl->access_token_, code, impl->token());
            if (const auto* error = error_of(found)) {
                impl->state_.notice = "Server not found: " + error->message;
                return;
            }
            const auto& lookup = std::get<community::DirectoryServerLookup>(found);
            impl->state_.lookup =
                ServerLookupItem{lookup.public_code, lookup.name, lookup.member_count, lookup.relationship};
        });
    });
}

void ProductSession::request_join(std::string server_code, std::string note) {
    impl_->enqueue([impl = impl_.get(), code = std::move(server_code), note = std::move(note)] {
        impl->online_command([&] {
            if (note.size() > community::kMaxJoinRequestMessageBytes) {
                impl->state_.notice = "Request notes must be at most 280 bytes.";
                return;
            }
            auto request = impl->client_->create_join_request(impl->access_token_, code, note, impl->token());
            if (const auto* error = error_of(request)) {
                impl->state_.notice = "Request not sent: " + error->message;
                return;
            }
            impl->state_.lookup.reset();
            impl->state_.notice =
                "Request sent to " + std::get<community::DirectoryJoinRequest>(request).server_name + ".";
        });
    });
}

void ProductSession::decide_request(std::string request_id, bool approve) {
    impl_->enqueue([impl = impl_.get(), id = std::move(request_id), approve] {
        impl->online_command([&] {
            auto decided = impl->client_->decide_join_request(impl->access_token_, id, approve, impl->token());
            if (const auto* error = error_of(decided)) {
                impl->state_.notice = "Decision not saved: " + error->message;
                return;
            }
            impl->refresh_requests();
            impl->refresh_members();
        });
    });
}

void ProductSession::stop() noexcept {
    impl_->stop();
}

ProductSnapshot ProductSession::snapshot() const {
    std::scoped_lock lock(impl_->published_mutex_);
    return impl_->published_;
}

} // namespace catro::product
