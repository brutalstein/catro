#include <catro/macos_product_session.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <exception>
#include <mutex>
#include <set>
#include <string_view>
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

ProductMediaApi native_media_api() noexcept {
    ProductMediaApi api;
    api.room_create = catro_room_runtime_create;
    api.room_destroy = catro_room_runtime_destroy;
    api.room_start = catro_room_runtime_start;
    api.room_stop = catro_room_runtime_stop;
    api.room_claim_screen = catro_room_runtime_claim_screen;
    api.room_release_screen = catro_room_runtime_release_screen;
    api.screen = screen::RoomScreenApi{
        .snapshot = catro_room_runtime_snapshot,
        .send_video = catro_room_runtime_send_video,
        .receive_video = catro_room_runtime_receive_video,
        .send_stream_audio = catro_room_runtime_send_stream_audio,
        .receive_stream_audio = catro_room_runtime_receive_stream_audio,
        .request_keyframe = catro_room_runtime_request_keyframe,
        .keyframe_requests = catro_room_runtime_keyframe_requests,
    };
    api.voice_create = catro_voice_runtime_create;
    api.voice_destroy = catro_voice_runtime_destroy;
    api.voice_start = catro_voice_runtime_start;
    api.voice_stop = catro_voice_runtime_stop;
    api.voice_set_muted = catro_voice_runtime_set_muted;
    api.voice_set_deafened = catro_voice_runtime_set_deafened;
    api.voice_snapshot = catro_voice_runtime_snapshot;
    api.voice_set_user_volume = catro_voice_runtime_set_user_volume;
    api.voice_user_speaking = catro_voice_runtime_user_speaking;
    api.voice_set_processing = catro_voice_runtime_set_processing;
    api.voice_set_input_threshold = catro_voice_runtime_set_input_threshold;
    api.voice_set_transmit = catro_voice_runtime_set_transmit;
    api.voice_set_devices = catro_voice_runtime_set_devices;
    api.voice_add_echo_reference = catro_voice_runtime_add_echo_reference;
    return api;
}

const ServerItem* ProductSnapshot::active_server() const noexcept {
    const auto found = std::find_if(servers.begin(), servers.end(),
                                    [this](const ServerItem& server) { return server.id == active_server_id; });
    return found == servers.end() ? nullptr : &*found;
}

struct ProductSession::Impl {
    Impl(ProductSessionDependencies dependencies, Listener listener)
        : deps_(std::move(dependencies)), listener_(std::move(listener)),
          screen_(std::make_unique<screen::MacScreenShareRuntime>(deps_.media.screen)) {
        screen_->set_local_preview_enabled(false);
        if (!deps_.enumerate_sources) {
            deps_.enumerate_sources = [] { return platform::macos::MacScreenCapture{}.enumerate_sources(); };
        }
        // Stream audio feeds the voice echo canceller. Screen threads stop before voice does, and
        // the voice runtime outlives every call.
        screen_->set_echo_sink([this](std::span<const float> pcm) {
            const auto voice = live_voice_.load(std::memory_order_acquire);
            if (voice != nullptr && deps_.media.voice_add_echo_reference != nullptr) {
                deps_.media.voice_add_echo_reference(voice, pcm.data(), pcm.size());
            }
        });
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
        // The worker is gone, so media teardown runs here without racing a command.
        leave_voice();
        live_voice_.store(nullptr, std::memory_order_release);
        if (voice_ != nullptr) {
            deps_.media.voice_destroy(voice_);
            voice_ = nullptr;
        }
        if (room_ != nullptr) {
            deps_.media.room_destroy(room_);
            room_ = nullptr;
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
        credential_ = std::get<std::string>(std::move(credential));
        connect();
    }

    // Startup often races the network (Wi-Fi joining, VPN, wake from sleep), so a failed sign-in
    // or sync is retried with backoff from poll() instead of leaving the session offline.
    void connect() {
        const auto& local = *deps_.local_state;
        const auto retry = [this](const std::string& reason) {
            const auto delay = app::reconnect_delay(reconnect_attempt_++);
            next_reconnect_ = std::chrono::steady_clock::now() + delay;
            state_.workspace.reconnect("Can't reach Catro online (" + reason + "). Retrying in " +
                                       std::to_string(delay.count()) + " s\u2026");
        };
        auto access = client_->register_identity(local.identity, credential_, token());
        if (const auto* error = error_of(access)) {
            retry(error->message);
            return;
        }
        access_token_ = std::get<std::string>(access);
        auto synced = client_->sync_personal_server(access_token_, local.personal_server, token());
        if (const auto* error = error_of(synced)) {
            access_token_.clear();
            retry(error->message);
            return;
        }
        reconnect_attempt_ = 0;
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
            if (!state_.media.voice_server_id.empty() && state_.media.voice_server_id != server_id) {
                leave_voice(); // voice belongs to the server being left
            }
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

    void rename_profile(const std::string& raw) {
        if (!deps_.local_state) {
            notify("Local profile is unavailable.");
            return;
        }
        const auto first = raw.find_first_not_of(" \t\r\n");
        const auto name = first == std::string::npos
            ? std::string{} : raw.substr(first, raw.find_last_not_of(" \t\r\n") - first + 1);
        if (name.empty() || name.size() > community::kMaxDisplayNameBytes ||
            std::any_of(name.begin(), name.end(),
                        [](unsigned char ch) { return ch < 0x20 || ch == 0x7f; })) {
            notify("Enter a name of 1 to 64 bytes without line breaks.");
            return;
        }
        auto updated = *deps_.local_state;
        updated.identity.display_name = name;
        const auto failure = deps_.save_local_state
            ? deps_.save_local_state(updated) : std::optional<std::string>{"storage is unavailable"};
        if (failure) {
            notify("Profile not saved: " + *failure);
            return;
        }
        deps_.local_state = updated;
        state_.identity_name = name;
        if (!online()) {
            notify("Profile saved on this Mac. It will sync when you are online.");
            return;
        }
        auto credential = deps_.load_credential();
        if (error_of(credential) == nullptr) {
            auto access = client_->register_identity(updated.identity, std::get<std::string>(credential), token());
            if (error_of(access) == nullptr) {
                access_token_ = std::get<std::string>(access);
                refresh_members();
                notify("Profile saved and synced.");
                return;
            }
        }
        notify("Profile saved on this Mac, but online sync failed. Save again to retry.");
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
            if (client_ && state_.workspace.connection == app::ConnectionState::connecting &&
                std::chrono::steady_clock::now() >= next_reconnect_) {
                connect();
            }
            return;
        }
        refresh_media();
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

    // Mirrors the Windows shell so both platforms derive the same RTP stream ids.
    [[nodiscard]] std::uint32_t local_stream_id() const noexcept {
        std::uint32_t value = 0x4354524fU; // "CTRO"
        if (deps_.local_state) {
            for (std::size_t index = 0; index < 4; ++index) {
                value = (value << 5U) ^ (value >> 27U) ^
                        std::to_integer<std::uint8_t>(deps_.local_state->identity.id.bytes[index]);
            }
        }
        return value == 0 ? 1U : value;
    }

    void fail_voice(std::string status) {
        state_.media.phase = VoicePhase::failed;
        state_.media.status = std::move(status);
    }

    // Provisioning, room, voice, then screen listening, in the Windows order.
    void join_voice() {
        auto& media = state_.media;
        if (media.phase == VoicePhase::joining || media.phase == VoicePhase::joined) {
            return;
        }
        if (media.phase == VoicePhase::failed) {
            leave_voice();
        }
        const auto* server = state_.active_server();
        if (server == nullptr || server->voice_channel_id.empty()) {
            media.status = "This server has no voice channel.";
            return;
        }
        const auto server_id = server->id;
        const auto channel_id = server->voice_channel_id;
        media.phase = VoicePhase::joining;
        media.status = "Authorizing room…";
        publish();

        auto provisioned = client_->request_rtc_provisioning(access_token_, server_id, channel_id, token());
        if (const auto* error = error_of(provisioned)) {
            fail_voice("Room authorization failed: " + error->message);
            return;
        }
        // The parser already rejects incomplete provisioning (peers 2..5, ICE, signaling URL).
        const auto& rtc = std::get<community::RtcProvisioning>(provisioned);
        if (room_ == nullptr) {
            room_ = deps_.media.room_create();
        }
        if (voice_ == nullptr) {
            voice_ = deps_.media.voice_create();
            live_voice_.store(voice_, std::memory_order_release);
        }
        if (room_ == nullptr || voice_ == nullptr) {
            fail_voice("Voice runtime is unavailable.");
            return;
        }
        std::vector<const char*> ice_urls;
        for (const auto& url : rtc.ice_servers) {
            ice_urls.push_back(url.c_str());
        }
        const CatroRoomRuntimeConfig room_config{
            .signaling_url = rtc.signaling_url.c_str(),
            .access_token = rtc.token.c_str(),
            .server_id = rtc.server_id.c_str(),
            .channel_id = rtc.channel_id.c_str(),
            .user_id = rtc.peer_id.c_str(),
            .ice_server_urls = ice_urls.data(),
            .ice_server_count = ice_urls.size(),
            .max_remote_peers = static_cast<std::uint32_t>(rtc.max_room_peers - 1),
            .allow_insecure_signaling = rtc.allow_insecure_signaling ? std::uint8_t{1} : std::uint8_t{0},
            .allow_no_turn = rtc.allow_no_turn ? std::uint8_t{1} : std::uint8_t{0},
        };
        if (deps_.media.room_start(room_, &room_config) != 0) {
            const auto room = deps_.media.screen.snapshot(room_);
            fail_voice(room.error[0] == '\0' ? std::string{"Room connection error"}
                                             : std::string{"Room connection error: "} + room.error);
            return;
        }
        std::string input_device;
        std::string output_device;
        {
            std::scoped_lock lock(devices_mutex_);
            input_device = input_device_;
            output_device = output_device_;
        }
        const CatroVoiceRuntimeConfig voice_config{
            .room_runtime = room_,
            .bind_address = nullptr,
            .bind_port = 0,
            .peer_address = nullptr,
            .peer_port = 0,
            .stream_id = local_stream_id(),
            .jitter_packets = 3,
            .bitrate = 48'000,
            .input_endpoint = input_device.c_str(),
            .output_endpoint = output_device.c_str(),
        };
        if (deps_.media.voice_start(voice_, &voice_config) != 0) {
            const auto voice = deps_.media.voice_snapshot(voice_);
            deps_.media.room_stop(room_);
            fail_voice(voice.error[0] == '\0' ? std::string{"Voice could not start."}
                                              : std::string{"Voice could not start: "} + voice.error);
            return;
        }
        peer_id_ = rtc.peer_id;
        media.phase = VoicePhase::joined;
        media.voice_server_id = server_id;
        media.status = "Voice connected";
        screen::ScreenTransportConfig transport;
        transport.room_runtime = room_;
        if (const auto failure = screen_->start_listening(transport)) {
            media.status = "Stream viewing unavailable: " + failure->message;
        }
        refresh_media();
    }

    void leave_voice() {
        stop_share();
        screen_->set_remote_viewing_enabled(false);
        screen_->stop();
        if (voice_ != nullptr) {
            deps_.media.voice_stop(voice_);
        }
        if (room_ != nullptr) {
            deps_.media.room_stop(room_);
        }
        peer_id_.clear();
        auto sources = std::move(state_.media.sources);
        state_.media = MediaSnapshot{};
        state_.media.sources = std::move(sources);
    }

    // ponytail: waits on the serial worker for up to 3 s like the Windows shell; other commands
    // queue behind it. Move to a timer-driven state if that delay becomes visible.
    [[nodiscard]] bool await_screen_ownership() {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (std::chrono::steady_clock::now() < deadline && !stopping_.load(std::memory_order_acquire)) {
            const auto room = deps_.media.screen.snapshot(room_);
            const std::string_view owner{room.screen_owner};
            if (owner == peer_id_) {
                return true;
            }
            if (!owner.empty()) {
                state_.media.status = "Another participant is sharing";
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        deps_.media.room_release_screen(room_);
        state_.media.status = "Screen ownership request timed out";
        return false;
    }

    void start_share(const ShareRequest& request) {
        auto& media = state_.media;
        if (media.phase != VoicePhase::joined) {
            media.status = "Join voice before sharing.";
            return;
        }
        if (media.sharing) {
            return;
        }
        screen::MacScreenShareConfig config;
        config.source = request.source;
        config.room_runtime = room_;
        config.max_width = request.max_width;
        config.max_height = request.max_height;
        config.fps = request.fps;
        config.bitrate = request.bitrate;
        config.share_audio = request.share_audio;
        config.ssrc = local_stream_id() ^ 0x56494430U;
        if (config.ssrc == 0) {
            config.ssrc = 1;
        }
        // Rejected before claiming, so bad settings never take the room's single share slot.
        if (!screen::valid_share(config)) {
            media.status = "Invalid screen-share settings";
            return;
        }
        const auto room = deps_.media.screen.snapshot(room_);
        const std::string_view owner{room.screen_owner};
        if (!owner.empty() && owner != peer_id_) {
            media.status = "Another participant is sharing";
            return;
        }
        if (owner != peer_id_) {
            if (deps_.media.room_claim_screen(room_) != 0) {
                media.status = "Screen ownership request failed";
                return;
            }
            if (!await_screen_ownership()) {
                return;
            }
        }
        if (const auto failure = screen_->start(config)) {
            deps_.media.room_release_screen(room_);
            media.status = "Screen share failed: " + failure->message;
            refresh_media();
            return;
        }
        media.sharing = true;
        media.status = "Sharing " + request.source.title;
        refresh_media();
    }

    void stop_share() {
        if (!state_.media.sharing) {
            return;
        }
        screen_->stop_sharing();
        deps_.media.room_release_screen(room_);
        state_.media.sharing = false;
        state_.media.status = "Voice connected";
    }

    void refresh_media() {
        auto& media = state_.media;
        if (media.phase != VoicePhase::joined) {
            return;
        }
        const auto room = deps_.media.screen.snapshot(room_);
        media.peer_count = room.peer_count;
        media.screen_owner = room.screen_owner;
        const auto voice = deps_.media.voice_snapshot(voice_);
        media.muted = voice.muted != 0;
        media.deafened = voice.deafened != 0;
        const auto share = screen_->snapshot();
        media.share_source_title = share.source_title;
        media.encoded_width = share.encoded_width;
        media.encoded_height = share.encoded_height;
        media.frames_sent = share.frames_sent;
        media.stream_audio_active = share.stream_audio_active;
        media.remote_available = share.remote_available;
        if (media.sharing && share.state == screen::ScreenShareState::failed) {
            stop_share();
            media.status = "Screen share stopped: " + share.error;
        } else if (media.sharing && !share.stream_audio_error.empty()) {
            media.status = "Sharing without audio: " + share.stream_audio_error;
        }
        if (room.state == CATRO_ROOM_FAILED) {
            stop_share();
            fail_voice(std::string{"Room connection lost: "} + room.error);
        } else if (voice.state == CATRO_VOICE_FAILED) {
            stop_share();
            fail_voice(std::string{"Voice stopped: "} + voice.error);
        } else if (room.state == CATRO_ROOM_CONNECTING) {
            // The room transport is rejoining after a dropped signaling connection.
            media.status = kReconnecting;
        } else if (media.status == kReconnecting) {
            media.status = "Voice connected";
        }
    }

    static constexpr std::string_view kReconnecting = "Reconnecting...";

    void load_sources() {
        auto result = deps_.enumerate_sources();
        // Cameras still list when Screen Recording is denied.
        state_.media.sources = std::move(result.sources);
        if (result.error) {
            state_.media.status = platform::macos::name(result.error->code);
        }
    }

    ProductSessionDependencies deps_;
    Listener listener_;
    std::unique_ptr<screen::MacScreenShareRuntime> screen_;
    CatroRoomRuntimeHandle room_ = nullptr;
    CatroVoiceRuntimeHandle voice_ = nullptr;
    // Published for main-thread speaking and volume calls; created once, cleared before destroy.
    std::atomic<CatroVoiceRuntimeHandle> live_voice_{nullptr};
    // Chosen in Settings on the main thread, read by the worker when voice starts.
    std::mutex devices_mutex_;
    std::string input_device_;
    std::string output_device_;
    std::string peer_id_;
    ProductSnapshot state_;
    std::unique_ptr<platform::macos::DirectoryHttpTransport> transport_;
    std::optional<platform::macos::DirectoryClient> client_;
    std::string access_token_;
    std::string credential_;
    unsigned reconnect_attempt_ = 0;
    std::chrono::steady_clock::time_point next_reconnect_{};
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

void ProductSession::rename_profile(std::string name) {
    impl_->enqueue([impl = impl_.get(), name = std::move(name)] { impl->rename_profile(name); });
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

void ProductSession::join_voice() {
    impl_->enqueue([impl = impl_.get()] { impl->online_command([&] { impl->join_voice(); }); });
}

void ProductSession::leave_voice() {
    impl_->enqueue([impl = impl_.get()] { impl->leave_voice(); });
}

void ProductSession::set_muted(bool muted) {
    impl_->enqueue([impl = impl_.get(), muted] {
        if (impl->state_.media.phase == VoicePhase::joined) {
            impl->deps_.media.voice_set_muted(impl->voice_, muted ? 1U : 0U);
            impl->state_.media.muted = muted;
        }
    });
}

void ProductSession::set_deafened(bool deafened) {
    impl_->enqueue([impl = impl_.get(), deafened] {
        if (impl->state_.media.phase == VoicePhase::joined) {
            impl->deps_.media.voice_set_deafened(impl->voice_, deafened ? 1U : 0U);
            impl->state_.media.deafened = deafened;
        }
    });
}

void ProductSession::load_sources() {
    impl_->enqueue([impl = impl_.get()] { impl->load_sources(); });
}

void ProductSession::start_share(ShareRequest request) {
    impl_->enqueue([impl = impl_.get(), request = std::move(request)] { impl->start_share(request); });
}

void ProductSession::stop_share() {
    impl_->enqueue([impl = impl_.get()] { impl->stop_share(); });
}

void ProductSession::set_watching(bool watching) {
    impl_->enqueue([impl = impl_.get(), watching] {
        if (impl->state_.media.phase == VoicePhase::joined) {
            impl->screen_->set_remote_viewing_enabled(watching);
            impl->state_.media.watching = watching;
        }
    });
}

void ProductSession::set_stream_volume(float volume) {
    impl_->screen_->set_stream_volume(volume);
}

void ProductSession::set_local_preview_enabled(bool enabled) noexcept {
    impl_->screen_->set_local_preview_enabled(enabled);
}

std::optional<screen::ScreenShareError> ProductSession::attach_preview_surface(void* host_layer) {
    return impl_->screen_->attach_preview_surface(host_layer);
}

std::optional<screen::ScreenShareError> ProductSession::attach_remote_surface(void* host_layer) {
    return impl_->screen_->attach_remote_surface(host_layer);
}

void ProductSession::stop() noexcept {
    impl_->stop();
}

ProductSnapshot ProductSession::snapshot() const {
    std::scoped_lock lock(impl_->published_mutex_);
    return impl_->published_;
}

std::vector<std::string> ProductSession::speaking_members() const {
    std::vector<std::string> speaking;
    const auto voice = impl_->live_voice_.load(std::memory_order_acquire);
    const auto& media = impl_->deps_.media;
    if (voice == nullptr || media.voice_user_speaking == nullptr) {
        return speaking;
    }
    std::scoped_lock lock(impl_->published_mutex_);
    if (impl_->published_.media.phase != VoicePhase::joined) {
        return speaking;
    }
    for (const auto& member : impl_->published_.members) {
        const bool talking = member.is_self ? media.voice_snapshot(voice).speaking != 0
                                            : media.voice_user_speaking(voice, member.user_id.c_str()) != 0;
        if (talking) {
            speaking.push_back(member.user_id);
        }
    }
    return speaking;
}

void ProductSession::set_voice_processing(bool echo_cancellation, bool noise_suppression, bool automatic_gain) {
    const auto voice = impl_->live_voice_.load(std::memory_order_acquire);
    if (voice != nullptr && impl_->deps_.media.voice_set_processing != nullptr) {
        impl_->deps_.media.voice_set_processing(voice, echo_cancellation ? 1U : 0U, noise_suppression ? 1U : 0U,
                                                automatic_gain ? 1U : 0U);
    }
}

void ProductSession::set_input_threshold(float dbfs) {
    const auto voice = impl_->live_voice_.load(std::memory_order_acquire);
    if (voice != nullptr && impl_->deps_.media.voice_set_input_threshold != nullptr) {
        impl_->deps_.media.voice_set_input_threshold(voice, dbfs);
    }
}

void ProductSession::set_transmit(bool transmit) {
    const auto voice = impl_->live_voice_.load(std::memory_order_acquire);
    if (voice != nullptr && impl_->deps_.media.voice_set_transmit != nullptr) {
        impl_->deps_.media.voice_set_transmit(voice, transmit ? 1U : 0U);
    }
}

void ProductSession::set_audio_devices(const std::string& input, const std::string& output) {
    {
        std::scoped_lock lock(impl_->devices_mutex_);
        impl_->input_device_ = input;
        impl_->output_device_ = output;
    }
    const auto voice = impl_->live_voice_.load(std::memory_order_acquire);
    if (voice != nullptr && impl_->deps_.media.voice_set_devices != nullptr) {
        impl_->deps_.media.voice_set_devices(voice, input.c_str(), output.c_str());
    }
}

float ProductSession::input_level() const {
    const auto voice = impl_->live_voice_.load(std::memory_order_acquire);
    if (voice == nullptr || impl_->deps_.media.voice_snapshot == nullptr) {
        return -100.0F;
    }
    return impl_->deps_.media.voice_snapshot(voice).input_level;
}

void ProductSession::set_member_volume(const std::string& user_id, float volume) {
    const auto voice = impl_->live_voice_.load(std::memory_order_acquire);
    if (voice != nullptr && impl_->deps_.media.voice_set_user_volume != nullptr) {
        impl_->deps_.media.voice_set_user_volume(voice, user_id.c_str(), volume);
    }
}

} // namespace catro::product
