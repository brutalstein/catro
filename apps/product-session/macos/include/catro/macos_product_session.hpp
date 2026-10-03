#pragma once

#include <PresentationState.hpp>
#include <catro/community/directory.hpp>
#include <catro/community/model.hpp>
#include <catro/macos_screen_runtime.hpp>
#include <catro/platform/macos/directory_client.hpp>
#include <catro/voice_runtime.h>

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
    bool is_self = false;
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

enum class VoicePhase : std::uint8_t {
    idle,
    joining,
    joined,
    failed,
};

struct MediaSnapshot {
    VoicePhase phase = VoicePhase::idle;
    // Server whose voice channel is joined; leaving that server leaves the room.
    std::string voice_server_id;
    // User-facing voice/share status line (ownership busy, permission denied, room error, ...).
    std::string status;
    bool muted = false;
    bool deafened = false;
    std::uint32_t peer_count = 0;
    // This client is sharing its screen.
    bool sharing = false;
    // Room peer id of the current sharer; empty when nobody shares.
    std::string screen_owner;
    bool remote_available = false;
    bool watching = false;
    std::vector<platform::macos::CaptureSource> sources;
};

struct ShareRequest {
    platform::macos::CaptureSource source;
    std::uint32_t max_width = 1920;
    std::uint32_t max_height = 1080;
    std::uint32_t fps = 30;
    std::uint32_t bitrate = 6'000'000;
    bool share_audio = false;
};

// Room and voice C ABI entry points. Tests swap in a deterministic fake room; production uses
// native_media_api(). The screen runtime receives the same room functions.
struct ProductMediaApi {
    CatroRoomRuntimeHandle (*room_create)() noexcept = nullptr;
    void (*room_destroy)(CatroRoomRuntimeHandle) noexcept = nullptr;
    std::int32_t (*room_start)(CatroRoomRuntimeHandle, const CatroRoomRuntimeConfig*) noexcept = nullptr;
    void (*room_stop)(CatroRoomRuntimeHandle) noexcept = nullptr;
    std::int32_t (*room_claim_screen)(CatroRoomRuntimeHandle) noexcept = nullptr;
    void (*room_release_screen)(CatroRoomRuntimeHandle) noexcept = nullptr;
    screen::RoomScreenApi screen;
    CatroVoiceRuntimeHandle (*voice_create)() noexcept = nullptr;
    void (*voice_destroy)(CatroVoiceRuntimeHandle) noexcept = nullptr;
    std::int32_t (*voice_start)(CatroVoiceRuntimeHandle, const CatroVoiceRuntimeConfig*) noexcept = nullptr;
    void (*voice_stop)(CatroVoiceRuntimeHandle) noexcept = nullptr;
    void (*voice_set_muted)(CatroVoiceRuntimeHandle, std::uint8_t) noexcept = nullptr;
    void (*voice_set_deafened)(CatroVoiceRuntimeHandle, std::uint8_t) noexcept = nullptr;
    CatroVoiceRuntimeSnapshot (*voice_snapshot)(CatroVoiceRuntimeHandle) noexcept = nullptr;
    void (*voice_set_user_volume)(CatroVoiceRuntimeHandle, const char*, float) noexcept = nullptr;
    std::uint8_t (*voice_user_speaking)(CatroVoiceRuntimeHandle, const char*) noexcept = nullptr;
    void (*voice_set_processing)(CatroVoiceRuntimeHandle, std::uint8_t, std::uint8_t, std::uint8_t) noexcept = nullptr;
    void (*voice_set_input_threshold)(CatroVoiceRuntimeHandle, float) noexcept = nullptr;
    void (*voice_set_transmit)(CatroVoiceRuntimeHandle, std::uint8_t) noexcept = nullptr;
    void (*voice_set_devices)(CatroVoiceRuntimeHandle, const char*, const char*) noexcept = nullptr;
};

[[nodiscard]] ProductMediaApi native_media_api() noexcept;

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
    MediaSnapshot media;
    // One-shot user-facing outcome of the last command (request sent, invite rejected, ...).
    std::string notice;

    [[nodiscard]] const ServerItem* active_server() const noexcept;
};

struct ProductSessionDependencies {
    // Absent when the local profile could not be loaded; the session then stays failed.
    std::optional<community::LocalState> local_state;
    std::function<community::DirectoryConfigResult()> load_config;
    std::function<community::DirectoryStringResult()> load_credential;
    // Persists a renamed profile; returns a user-facing reason on failure.
    std::function<std::optional<std::string>(const community::LocalState&)> save_local_state;
    std::function<std::unique_ptr<platform::macos::DirectoryHttpTransport>(
        const community::DirectoryServiceConfig&)>
        make_transport;
    ProductMediaApi media = native_media_api();
    // Runs on the session worker; ScreenCaptureKit enumeration must stay off the main thread.
    std::function<platform::macos::CaptureEnumerationResult()> enumerate_sources;
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
    // Saves the new display name on this Mac, then re-registers it online when connected.
    void rename_profile(std::string name);

    // Joins the active server's voice channel: RTC provisioning, room, voice, then screen listening.
    void join_voice();
    void leave_voice();
    void set_muted(bool muted);
    void set_deafened(bool deafened);
    void load_sources();
    // Claims room screen ownership (busy/timeout fail closed), then starts capture; released on failure.
    void start_share(ShareRequest request);
    void stop_share();
    void set_watching(bool watching);
    // Main thread only; forwards a caller-owned CALayer* (nullptr detaches) to the screen runtime.
    [[nodiscard]] std::optional<screen::ScreenShareError> attach_preview_surface(void* host_layer);
    [[nodiscard]] std::optional<screen::ScreenShareError> attach_remote_surface(void* host_layer);

    // Cancels in-flight network work, joins the worker and leaves voice; no listener call follows.
    void stop() noexcept;

    [[nodiscard]] ProductSnapshot snapshot() const;

    // Main thread. Read straight from the voice runtime's atomics, so speaking indicators stay
    // live even while the worker waits on the network.
    [[nodiscard]] std::vector<std::string> speaking_members() const;
    // 0 silences, 1 is unchanged, 2 doubles; kept until the session stops.
    void set_member_volume(const std::string& user_id, float volume);
    // Main thread, applied live by the voice worker. Threshold in dBFS; NaN is automatic.
    void set_voice_processing(bool echo_cancellation, bool noise_suppression, bool automatic_gain);
    void set_input_threshold(float dbfs);
    // Push-to-talk: false keeps the microphone closed.
    void set_transmit(bool transmit);
    // Main thread. Microphone and output endpoint ids, empty for the system default. Used by the
    // next join and applied live to a running call.
    void set_audio_devices(const std::string& input, const std::string& output);
    // Main thread. Microphone level in dBFS after processing; -100 outside voice.
    [[nodiscard]] float input_level() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace catro::product
