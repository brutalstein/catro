#include <catro/room_voice_runtime.hpp>

#include "helpers/fake_audio_platform.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

using namespace catro;
using namespace std::chrono_literals;
using test::FakeAudioPlatform;
using voice_runtime::VoiceRuntimeHost;

namespace {

constexpr std::size_t kInboxLimit = 256;

class FakeRoomBus;

// One participant of an in-memory room with a bounded latest-edge inbox, like the RTC room runtime.
struct FakeRoom {
    FakeRoomBus* bus = nullptr;
    std::atomic<std::int32_t> state{CATRO_ROOM_JOINED};
    std::atomic_bool stalled{false};
    std::string error;

    std::mutex mutex;
    std::condition_variable ready;
    std::deque<std::vector<std::byte>> inbox;

    void deliver(const std::byte* data, std::size_t size) {
        {
            std::scoped_lock lock(mutex);
            if (inbox.size() == kInboxLimit) {
                inbox.pop_front();
            }
            inbox.emplace_back(data, data + size);
        }
        ready.notify_one();
    }
};

class FakeRoomBus {
public:
    FakeRoom& join() {
        std::scoped_lock lock(mutex_);
        rooms_.push_back(std::make_unique<FakeRoom>());
        rooms_.back()->bus = this;
        return *rooms_.back();
    }

    std::size_t broadcast(const FakeRoom& sender, const std::byte* data, std::size_t size) {
        std::scoped_lock lock(mutex_);
        std::size_t delivered = 0;
        for (const auto& room : rooms_) {
            if (room.get() != &sender) {
                room->deliver(data, size);
                ++delivered;
            }
        }
        return delivered;
    }

private:
    std::mutex mutex_;
    std::vector<std::unique_ptr<FakeRoom>> rooms_;
};

FakeRoom& room_of(CatroRoomRuntimeHandle handle) {
    return *static_cast<FakeRoom*>(handle);
}

CatroRoomRuntimeSnapshot fake_snapshot(CatroRoomRuntimeHandle handle) noexcept {
    auto& room = room_of(handle);
    CatroRoomRuntimeSnapshot result{};
    result.state = room.state.load(std::memory_order_acquire);
    if (result.state == CATRO_ROOM_FAILED) {
        std::snprintf(result.error, sizeof(result.error), "%s", room.error.c_str());
    }
    return result;
}

std::size_t fake_send(CatroRoomRuntimeHandle handle, const std::byte* data, std::size_t size) noexcept {
    auto& room = room_of(handle);
    return room.bus->broadcast(room, data, size);
}

std::ptrdiff_t fake_receive(CatroRoomRuntimeHandle handle,
                            std::byte* destination,
                            std::size_t capacity,
                            std::uint32_t timeout_ms) noexcept {
    auto& room = room_of(handle);
    std::unique_lock lock(room.mutex);
    const auto has_data = [&] {
        return !room.stalled.load(std::memory_order_acquire) && !room.inbox.empty();
    };
    if (!room.ready.wait_for(lock, std::chrono::milliseconds(timeout_ms), has_data)) {
        return 0;
    }
    const auto datagram = std::move(room.inbox.front());
    room.inbox.pop_front();
    if (datagram.size() > capacity) {
        return -1;
    }
    std::copy(datagram.begin(), datagram.end(), destination);
    return static_cast<std::ptrdiff_t>(datagram.size());
}

constexpr voice_runtime::RoomVoiceApi kFakeRoomApi{&fake_snapshot, &fake_send, &fake_receive};

CatroVoiceRuntimeConfig room_config(FakeRoom& room, std::uint32_t stream_id) {
    CatroVoiceRuntimeConfig config{};
    config.room_runtime = &room;
    config.stream_id = stream_id;
    config.jitter_packets = 2;
    config.bitrate = 48'000;
    return config;
}

bool wait_until(const std::function<bool()>& condition, std::chrono::milliseconds limit = 3s) {
    const auto deadline = std::chrono::steady_clock::now() + limit;
    while (std::chrono::steady_clock::now() < deadline) {
        if (condition()) {
            return true;
        }
        std::this_thread::sleep_for(10ms);
    }
    return condition();
}

std::uint64_t audible(const FakeAudioPlatform& audio) {
    return audio.rendered_nonzero_samples.load(std::memory_order_relaxed);
}

} // namespace

TEST_CASE("room voice runtime exchanges audible duplex media through injected room operations") {
    FakeRoomBus bus;
    auto& first_room = bus.join();
    auto& second_room = bus.join();
    FakeAudioPlatform first_audio(0.20F);
    FakeAudioPlatform second_audio(-0.20F);
    VoiceRuntimeHost first(first_audio, kFakeRoomApi);
    VoiceRuntimeHost second(second_audio, kFakeRoomApi);

    REQUIRE(first.start(room_config(first_room, 101)) == 0);
    REQUIRE(second.start(room_config(second_room, 202)) == 0);

    CHECK(wait_until([&] {
        return first.snapshot().state == CATRO_VOICE_JOINED &&
               second.snapshot().state == CATRO_VOICE_JOINED &&
               audible(first_audio) > 4'800 && audible(second_audio) > 4'800;
    }));
    const auto snapshot = first.snapshot();
    CHECK(snapshot.sent_packets > 0);
    CHECK(snapshot.received_packets > 0);
    CHECK(snapshot.peer_seen == 1);
    CHECK(snapshot.error[0] == '\0');
}

TEST_CASE("room voice runtime mute silences the remote side and deafen silences local playout") {
    FakeRoomBus bus;
    auto& talker_room = bus.join();
    auto& listener_room = bus.join();
    FakeAudioPlatform talker_audio(0.25F);
    FakeAudioPlatform listener_audio(0.0F);
    VoiceRuntimeHost talker(talker_audio, kFakeRoomApi);
    VoiceRuntimeHost listener(listener_audio, kFakeRoomApi);
    REQUIRE(talker.start(room_config(talker_room, 11)) == 0);
    REQUIRE(listener.start(room_config(listener_room, 22)) == 0);
    REQUIRE(wait_until([&] { return audible(listener_audio) > 4'800; }));

    talker.set_muted(true);
    CHECK(talker.snapshot().muted == 1);
    // Loaded CI runners can stall playout and then drain audio queued before the mute, so a
    // fixed-delay comparison is unreliable. An unmuted talker never leaves the listener quiet for
    // half a second; a muted one must, once the pre-mute queue has played out.
    REQUIRE(wait_until([&] {
        const auto start = audible(listener_audio);
        std::this_thread::sleep_for(500ms);
        return audible(listener_audio) == start;
    }, 10s));
    auto before = audible(listener_audio);

    talker.set_muted(false);
    REQUIRE(wait_until([&] { return audible(listener_audio) > before + 4'800; }));

    listener.set_deafened(true);
    CHECK(listener.snapshot().deafened == 1);
    std::this_thread::sleep_for(150ms);
    before = audible(listener_audio);
    std::this_thread::sleep_for(300ms);
    CHECK(audible(listener_audio) == before);
    CHECK(listener.snapshot().state == CATRO_VOICE_JOINED);
}

TEST_CASE("room voice runtime mixes four remote talkers for a five-participant room") {
    FakeRoomBus bus;
    auto& listener_room = bus.join();
    FakeAudioPlatform listener_audio(0.0F);
    VoiceRuntimeHost listener(listener_audio, kFakeRoomApi);
    REQUIRE(listener.start(room_config(listener_room, 1)) == 0);

    std::vector<std::unique_ptr<FakeAudioPlatform>> talker_audio;
    std::vector<std::unique_ptr<VoiceRuntimeHost>> talkers;
    for (std::uint32_t index = 0; index < 4; ++index) {
        auto& room = bus.join();
        talker_audio.push_back(std::make_unique<FakeAudioPlatform>(0.08F + 0.02F * static_cast<float>(index)));
        talkers.push_back(std::make_unique<VoiceRuntimeHost>(*talker_audio.back(), kFakeRoomApi));
        REQUIRE(talkers.back()->start(room_config(room, 100 + index)) == 0);
    }

    CHECK(wait_until([&] {
        return audible(listener_audio) > 9'600 &&
               std::all_of(talkers.begin(), talkers.end(),
                           [](const auto& talker) { return talker->snapshot().sent_packets > 10; });
    }));
    // Each talker's 20 ms packets reach the listener; four streams arrive at roughly four times one.
    CHECK(listener.snapshot().received_packets > 40);
    CHECK(listener.snapshot().state == CATRO_VOICE_JOINED);
}

TEST_CASE("room voice runtime reports an audio device failure without touching the room") {
    FakeRoomBus bus;
    auto& room = bus.join();
    FakeAudioPlatform audio(0.1F);
    audio.capture_error = audio::AudioError{audio::AudioErrorCode::permission_denied};
    VoiceRuntimeHost host(audio, kFakeRoomApi);

    REQUIRE(host.start(room_config(room, 7)) == 0);
    REQUIRE(wait_until([&] { return host.snapshot().state == CATRO_VOICE_FAILED; }));
    const auto snapshot = host.snapshot();
    CHECK(snapshot.exit_code == 5);
    CHECK(std::string_view{snapshot.error}.find("audio") != std::string_view::npos);
    CHECK(snapshot.sent_packets == 0);
}

TEST_CASE("room voice runtime follows a new default microphone and survives a lost device") {
    FakeRoomBus bus;
    auto& room = bus.join();
    FakeAudioPlatform audio(0.1F);
    VoiceRuntimeHost host(audio, kFakeRoomApi);

    REQUIRE(host.start(room_config(room, 9)) == 0);
    REQUIRE(wait_until([&] { return host.snapshot().state == CATRO_VOICE_JOINED; }));
    CHECK(audio.capture_opens == 1);

    // A headset becomes the system default: audio moves to it within the 1 s device check.
    audio.set_default_capture("headset");
    REQUIRE(wait_until([&] { return audio.capture_opens == 2; }));
    CHECK(host.snapshot().audio_restarts == 1);

    // The headset is unplugged: audio reopens instead of ending the call.
    audio.lose_capture();
    REQUIRE(wait_until([&] { return audio.capture_opens == 3; }));
    const auto snapshot = host.snapshot();
    CHECK(snapshot.state == CATRO_VOICE_JOINED);
    CHECK(snapshot.audio_restarts == 2);
    CHECK(snapshot.error[0] == '\0');

    // Picking a microphone in Settings moves the running call to it.
    host.set_devices("usb-mic", nullptr);
    REQUIRE(wait_until([&] { return audio.capture_opens == 4; }));
    CHECK(audio.requested_capture() == "usb-mic");
    CHECK(host.snapshot().state == CATRO_VOICE_JOINED);
}

TEST_CASE("room voice runtime fails with the room error when the RTC room fails") {
    FakeRoomBus bus;
    auto& room = bus.join();
    FakeAudioPlatform audio(0.1F);
    VoiceRuntimeHost host(audio, kFakeRoomApi);
    REQUIRE(host.start(room_config(room, 7)) == 0);
    REQUIRE(wait_until([&] { return host.snapshot().state == CATRO_VOICE_JOINED; }));

    room.error = "ICE connection failed";
    room.state.store(CATRO_ROOM_FAILED, std::memory_order_release);

    REQUIRE(wait_until([&] { return host.snapshot().state == CATRO_VOICE_FAILED; }));
    const auto snapshot = host.snapshot();
    CHECK(snapshot.exit_code == 6);
    CHECK(std::string_view{snapshot.error} == "ICE connection failed");
}

TEST_CASE("room voice runtime rejects invalid configuration before opening audio") {
    FakeRoomBus bus;
    auto& room = bus.join();
    FakeAudioPlatform audio(0.1F);
    VoiceRuntimeHost host(audio, kFakeRoomApi);

    auto config = room_config(room, 0);
    CHECK(host.start(config) == 2);
    CHECK(host.snapshot().state == CATRO_VOICE_FAILED);

    config = room_config(room, 1);
    config.jitter_packets = 11;
    CHECK(host.start(config) == 2);

    config = room_config(room, 1);
    config.bitrate = 8'000;
    CHECK(host.start(config) == 2);

    // Without an engineering direct-peer runner, only the production room path exists.
    CatroVoiceRuntimeConfig direct{};
    direct.bind_address = "127.0.0.1";
    direct.bind_port = 50000;
    direct.peer_address = "127.0.0.1";
    direct.peer_port = 50001;
    direct.stream_id = 1;
    direct.jitter_packets = 3;
    direct.bitrate = 48'000;
    CHECK(host.start(direct) == 2);
    CHECK(host.snapshot().error[0] != '\0');
    CHECK(audible(audio) == 0);
}

TEST_CASE("room voice runtime stops promptly and restarts idempotently") {
    FakeRoomBus bus;
    auto& room = bus.join();
    auto& peer_room = bus.join();
    FakeAudioPlatform audio(0.1F);
    FakeAudioPlatform peer_audio(0.1F);
    VoiceRuntimeHost host(audio, kFakeRoomApi);
    VoiceRuntimeHost peer(peer_audio, kFakeRoomApi);
    REQUIRE(peer.start(room_config(peer_room, 2)) == 0);

    for (int attempt = 0; attempt < 3; ++attempt) {
        REQUIRE(host.start(room_config(room, 1)) == 0);
        REQUIRE(wait_until([&] { return host.snapshot().state == CATRO_VOICE_JOINED; }));

        const auto started = std::chrono::steady_clock::now();
        host.stop();
        CHECK(std::chrono::steady_clock::now() - started < 250ms);
        CHECK(host.snapshot().state == CATRO_VOICE_IDLE);
        host.stop();
        CHECK(host.snapshot().state == CATRO_VOICE_IDLE);
    }
}

TEST_CASE("room voice runtime drains a stalled backlog and resumes live playout") {
    FakeRoomBus bus;
    auto& talker_room = bus.join();
    auto& listener_room = bus.join();
    FakeAudioPlatform talker_audio(0.2F);
    FakeAudioPlatform listener_audio(0.0F);
    VoiceRuntimeHost talker(talker_audio, kFakeRoomApi);
    VoiceRuntimeHost listener(listener_audio, kFakeRoomApi);
    REQUIRE(talker.start(room_config(talker_room, 31)) == 0);
    REQUIRE(listener.start(room_config(listener_room, 32)) == 0);
    REQUIRE(wait_until([&] { return audible(listener_audio) > 4'800; }));

    // Stall delivery for a second so a backlog of late packets builds up, then release it at once.
    listener_room.stalled.store(true, std::memory_order_release);
    std::this_thread::sleep_for(1s);
    listener_room.stalled.store(false, std::memory_order_release);
    listener_room.ready.notify_all();
    const auto resumed_from = audible(listener_audio);

    CHECK(wait_until([&] {
        std::scoped_lock lock(listener_room.mutex);
        return listener_room.inbox.size() < 8;
    }, 1s));
    CHECK(wait_until([&] { return audible(listener_audio) > resumed_from + 9'600; }));
    CHECK(listener.snapshot().state == CATRO_VOICE_JOINED);
    CHECK(listener.snapshot().error[0] == '\0');
}
