#include <catro/screen_transport_runtime.hpp>

#include "../helpers/fake_screen_room.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

using namespace catro;
using namespace catro::screen;
using namespace catro::test;
using namespace std::chrono_literals;

namespace {

// Annex-B access units the RFC 6184 packetizer accepts. Large payloads force FU-A fragmentation.
Datagram access_unit(bool keyframe, std::size_t payload_bytes = 64) {
    Datagram unit;
    const auto nal = [&](std::uint8_t header, std::size_t bytes) {
        for (const int value : {0, 0, 0, 1}) {
            unit.push_back(static_cast<std::byte>(value));
        }
        unit.push_back(std::byte{header});
        unit.insert(unit.end(), bytes, std::byte{0x22});
    };
    if (keyframe) {
        nal(0x67, 8);             // SPS
        nal(0x68, 4);             // PPS
        nal(0x65, payload_bytes); // IDR
    } else {
        nal(0x41, payload_bytes);
    }
    return unit;
}

struct FakeViewer final : RemoteVideoViewer {
    std::atomic<int> releases{0};
    std::atomic<int> decoder_starts{0};
    std::atomic<int> frames{0};
    std::atomic<int> keyframes{0};
    std::atomic<std::int64_t> last_pts{-1};
    std::atomic_bool fail_decode{false};
    ScreenTransportCounters* counters = nullptr;

    void release() noexcept override { releases.fetch_add(1); }

    std::optional<ScreenShareError> start_decoder() override {
        decoder_starts.fetch_add(1);
        return std::nullopt;
    }

    std::optional<ScreenShareError> decode_and_present(std::span<const std::byte> annex_b,
                                                       std::int64_t pts_100ns) override {
        if (fail_decode.load()) {
            return ScreenShareError{ScreenShareErrorCode::decoder_failed, "fake decode failure", 7};
        }
        if (annex_b.size() > 4 && (std::to_integer<int>(annex_b[4]) & 0x1F) == 7) {
            keyframes.fetch_add(1);
        }
        frames.fetch_add(1);
        last_pts.store(pts_100ns);
        counters->remote_last_frame_ns.store(steady_now_ns());
        return std::nullopt;
    }
};

// Runs the shared receive loop on a worker; the test thread keeps every Catch2 assertion.
class ReceiveWorker {
public:
    ReceiveWorker(FakeRoom& listener, ScreenTransportCounters& counters, FakeViewer& viewer) {
        context_.api = fake_api();
        context_.config.room_runtime = &listener;
        context_.counters = &counters;
        context_.stop_requested = &stop_;
        thread_ = std::thread([this, &viewer] { result_ = run_video_receive_loop(context_, viewer); });
    }

    ~ReceiveWorker() { (void)stop(); }

    std::optional<ScreenShareError> stop() {
        stop_.store(true);
        if (thread_.joinable()) {
            thread_.join();
        }
        return result_;
    }

private:
    VideoReceiveContext context_;
    std::atomic_bool stop_{false};
    std::optional<ScreenShareError> result_;
    std::thread thread_;
};

template <class Predicate>
bool eventually(Predicate predicate, std::chrono::milliseconds limit = 2s) {
    const auto deadline = Clock::now() + limit;
    while (Clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(2ms);
    }
    return predicate();
}

video::H264RtpConfig rtp(std::uint32_t ssrc) {
    return video::H264RtpConfig{ssrc, 96, 1200};
}

} // namespace

TEST_CASE("screen transport validates media bounds and transports") {
    CHECK(valid_media_bounds(96, 1200, 4U * 1024U * 1024U));
    CHECK_FALSE(valid_media_bounds(95, 1200, 4U * 1024U * 1024U));
    CHECK_FALSE(valid_media_bounds(96, 1500, 4U * 1024U * 1024U));
    CHECK_FALSE(valid_media_bounds(96, 1200, 1024));
    ScreenTransportConfig config;
    CHECK_FALSE(valid_transport(config));
    FakeRoom listener;
    config.room_runtime = &listener;
    CHECK(valid_transport(config));
    CHECK(frame_period(30) == 33'333'333ns);
}

TEST_CASE("screen video sender packetizes into room datagrams and counts fan-out") {
    FakeRoom sender_room;
    FakeRoom listener;
    sender_room.peer = &listener;
    sender_room.peers = 3;
    ScreenTransportCounters counters;
    VideoSender sender(fake_api(), &sender_room, nullptr, rtp(7), counters);

    const auto big = access_unit(true, 5000);
    REQUIRE(sender.send(big, 9000).status == VideoSendStatus::sent);
    const auto packets = listener.video.size();
    CHECK(packets >= 5); // FU-A fragmentation at the 1200-byte MTU
    CHECK(counters.packets_sent.load() == packets);
    CHECK(counters.wire_bytes.load() > big.size() * 3);
    CHECK(counters.frames_sent.load() == 1);

    // Zero viewers is not a failure and does not count wire traffic.
    sender_room.peers = 0;
    REQUIRE(sender.send(access_unit(false), 12000).status == VideoSendStatus::sent);
    CHECK(counters.packets_sent.load() == packets);
    CHECK(counters.frames_sent.load() == 2);

    const Datagram garbage{std::byte{1}, std::byte{2}, std::byte{3}};
    CHECK(sender.send(garbage, 15000).status == VideoSendStatus::not_packetizable);

    sender_room.state = CATRO_ROOM_FAILED;
    CHECK(sender.send(access_unit(false), 18000).status == VideoSendStatus::room_failed);
    CHECK(room_error_text(fake_api(), &sender_room, "fallback") == "fake room failed");
    CHECK(room_error_text(fake_api(), nullptr, "fallback") == "fallback");
}

TEST_CASE("screen receive counts streams without decoding while nobody watches") {
    FakeRoom sender_room;
    FakeRoom listener;
    sender_room.peer = &listener;
    ScreenTransportCounters counters;
    FakeViewer viewer;
    viewer.counters = &counters;
    ReceiveWorker worker(listener, counters, viewer);
    VideoSender sender(fake_api(), &sender_room, nullptr, rtp(11), counters);

    REQUIRE(sender.send(access_unit(true), 0).status == VideoSendStatus::sent);
    REQUIRE(sender.send(access_unit(false), 3000).status == VideoSendStatus::sent);
    REQUIRE(eventually([&] { return counters.remote_frames.load() == 2; }));
    CHECK(viewer.frames.load() == 0);
    CHECK(viewer.decoder_starts.load() == 0);
    ScreenShareSnapshot snapshot;
    counters.fill(snapshot);
    CHECK(snapshot.remote_available);
    CHECK_FALSE(snapshot.remote_viewing);
    CHECK_FALSE(worker.stop().has_value());
}

TEST_CASE("screen receive discards deltas until a keyframe after every viewer start") {
    FakeRoom sender_room;
    FakeRoom listener;
    sender_room.peer = &listener;
    ScreenTransportCounters counters;
    counters.remote_viewing_enabled = true;
    FakeViewer viewer;
    viewer.counters = &counters;
    ReceiveWorker worker(listener, counters, viewer);
    VideoSender sender(fake_api(), &sender_room, nullptr, rtp(21), counters);

    // A viewer that joins mid-stream must wait for the next keyframe.
    REQUIRE(sender.send(access_unit(false), 0).status == VideoSendStatus::sent);
    REQUIRE(sender.send(access_unit(false), 3000).status == VideoSendStatus::sent);
    REQUIRE(eventually([&] { return counters.remote_frames.load() == 2; }));
    CHECK(viewer.frames.load() == 0);

    REQUIRE(sender.send(access_unit(true), 6000).status == VideoSendStatus::sent);
    REQUIRE(sender.send(access_unit(false), 9000).status == VideoSendStatus::sent);
    REQUIRE(eventually([&] { return viewer.frames.load() == 2; }));
    CHECK(viewer.decoder_starts.load() == 1);
    CHECK(viewer.keyframes.load() == 1);
    // 90 kHz RTP ticks become 100 ns presentation units (9000 * 1000 / 9).
    CHECK(viewer.last_pts.load() == 1'000'000);

    // Stop watching releases the decoder; watching again requires a fresh keyframe.
    counters.remote_viewing_enabled = false;
    REQUIRE(eventually([&] { return viewer.releases.load() >= 1; }));
    counters.remote_viewing_enabled = true;
    REQUIRE(sender.send(access_unit(false), 12000).status == VideoSendStatus::sent);
    REQUIRE(eventually([&] { return counters.remote_frames.load() == 5; }));
    std::this_thread::sleep_for(30ms);
    CHECK(viewer.frames.load() == 2);
    REQUIRE(sender.send(access_unit(true), 15000).status == VideoSendStatus::sent);
    REQUIRE(eventually([&] { return viewer.frames.load() == 3; }));
    CHECK(viewer.decoder_starts.load() == 2);
    CHECK_FALSE(worker.stop().has_value());
}

TEST_CASE("screen receive rejects malformed packets and a second stream while the owner is active") {
    FakeRoom owner_room;
    FakeRoom intruder_room;
    FakeRoom listener;
    owner_room.peer = &listener;
    intruder_room.peer = &listener;
    ScreenTransportCounters counters;
    FakeViewer viewer;
    viewer.counters = &counters;
    ReceiveWorker worker(listener, counters, viewer);
    VideoSender owner(fake_api(), &owner_room, nullptr, rtp(31), counters);
    VideoSender intruder(fake_api(), &intruder_room, nullptr, rtp(32), counters);

    const Datagram malformed{std::byte{0x80}, std::byte{0x60}};
    listener.video.push(malformed.data(), malformed.size());
    REQUIRE(eventually([&] { return counters.remote_packet_rejects.load() == 1; }));

    REQUIRE(owner.send(access_unit(true), 0).status == VideoSendStatus::sent);
    REQUIRE(eventually([&] { return counters.remote_frames.load() == 1; }));
    REQUIRE(intruder.send(access_unit(true), 0).status == VideoSendStatus::sent);
    REQUIRE(eventually([&] { return counters.remote_packet_rejects.load() >= 2; }));
    std::this_thread::sleep_for(30ms);
    CHECK(counters.remote_frames.load() == 1);
    CHECK(counters.remote_stream_resets.load() == 0);
    CHECK_FALSE(worker.stop().has_value());
}

TEST_CASE("screen receive accepts a restarted sender only after the old stream goes quiet") {
    FakeRoom first_room;
    FakeRoom second_room;
    FakeRoom listener;
    first_room.peer = &listener;
    second_room.peer = &listener;
    ScreenTransportCounters counters;
    FakeViewer viewer;
    viewer.counters = &counters;
    ReceiveWorker worker(listener, counters, viewer);
    VideoSender first(fake_api(), &first_room, nullptr, rtp(41), counters);
    VideoSender second(fake_api(), &second_room, nullptr, rtp(42), counters);

    REQUIRE(first.send(access_unit(true), 0).status == VideoSendStatus::sent);
    REQUIRE(eventually([&] { return counters.remote_frames.load() == 1; }));
    std::this_thread::sleep_for(kRemoteInactiveTimeout + 100ms);
    REQUIRE(second.send(access_unit(true), 0).status == VideoSendStatus::sent);
    REQUIRE(eventually([&] { return counters.remote_frames.load() == 2; }));
    CHECK(counters.remote_stream_resets.load() == 1);
    CHECK_FALSE(worker.stop().has_value());
}

TEST_CASE("screen receive stays responsive to stop under an endless datagram flood") {
    FakeRoom listener;
    ScreenTransportCounters counters;
    FakeViewer viewer;
    viewer.counters = &counters;
    const Datagram junk(200, std::byte{0x55});
    listener.video.flood(&junk);
    ReceiveWorker worker(listener, counters, viewer);
    REQUIRE(eventually([&] { return counters.remote_packets.load() > 2000; }));
    const auto started = Clock::now();
    CHECK_FALSE(worker.stop().has_value());
    CHECK(Clock::now() - started < 250ms);
    CHECK(counters.remote_packet_rejects.load() == counters.remote_packets.load());
}

TEST_CASE("screen receive reports decoder and missing-transport failures as fatal") {
    FakeRoom sender_room;
    FakeRoom listener;
    sender_room.peer = &listener;
    ScreenTransportCounters counters;
    counters.remote_viewing_enabled = true;
    FakeViewer viewer;
    viewer.counters = &counters;
    viewer.fail_decode = true;
    {
        ReceiveWorker worker(listener, counters, viewer);
        VideoSender sender(fake_api(), &sender_room, nullptr, rtp(51), counters);
        REQUIRE(sender.send(access_unit(true), 0).status == VideoSendStatus::sent);
        REQUIRE(eventually([&] { return counters.remote_decode_failures.load() == 1; }));
        const auto failure = worker.stop();
        REQUIRE(failure.has_value());
        CHECK(failure->code == ScreenShareErrorCode::decoder_failed);
        CHECK(failure->native_code == 7);
    }
    // A restart on reset counters behaves like a fresh session and stops cleanly.
    counters.reset_remote();
    viewer.fail_decode = false;
    {
        ReceiveWorker worker(listener, counters, viewer);
        CHECK_FALSE(worker.stop().has_value());
    }
    ScreenTransportCounters missing_room_counters;
    VideoReceiveContext context;
    context.api = fake_api();
    context.counters = &missing_room_counters;
    std::atomic_bool stop{false};
    context.stop_requested = &stop;
    const auto failure = run_video_receive_loop(context, viewer);
    REQUIRE(failure.has_value());
    CHECK(failure->code == ScreenShareErrorCode::network_failed);
}

namespace {

struct FakeOutput final : StreamAudioOutput {
    std::atomic<int> starts{0};
    std::atomic<int> stops{0};

    bool start(StreamAudioRenderBridge&) override {
        starts.fetch_add(1);
        return true;
    }

    void stop() noexcept override { stops.fetch_add(1); }
};

StreamAudioPcmFrame tone(float phase) {
    StreamAudioPcmFrame frame{};
    for (std::size_t index = 0; index < frame.size(); index += 2) {
        const auto value = 0.25F * std::sin(phase + static_cast<float>(index) * 0.01F);
        frame[index] = value;
        frame[index + 1] = value;
    }
    return frame;
}

} // namespace

TEST_CASE("stream audio flows through Opus framing only while the stream is watched") {
    FakeRoom sender_room;
    FakeRoom listener;
    sender_room.peer = &listener;
    ScreenTransportCounters counters;
    StreamAudioSender sender(fake_api(), &sender_room, counters);
    REQUIRE_FALSE(sender.start(128'000, 99).has_value());
    FakeOutput output;
    std::atomic_bool stop{false};
    std::thread receiver([&] { run_stream_audio_receive_loop(fake_api(), &listener, counters, stop, output); });

    for (int index = 0; index < 5; ++index) {
        sender.send(tone(static_cast<float>(index)));
    }
    REQUIRE(eventually([&] { return counters.remote_stream_audio_packets.load() == 5; }));
    CHECK(output.starts.load() == 0);
    CHECK(counters.stream_audio_frames_encoded.load() == 5);
    CHECK(counters.stream_audio_packets_sent.load() == 5);

    counters.remote_viewing_enabled = true;
    const auto deadline = Clock::now() + 2s;
    for (int index = 5; Clock::now() < deadline && counters.remote_stream_audio_frames.load() < 10; ++index) {
        sender.send(tone(static_cast<float>(index)));
        std::this_thread::sleep_for(kStreamAudioFramePeriod);
    }
    CHECK(counters.remote_stream_audio_frames.load() >= 10);
    CHECK(output.starts.load() == 1);
    CHECK(counters.remote_stream_audio_active.load());

    const Datagram malformed{std::byte{0xFF}};
    listener.audio.push(malformed.data(), malformed.size());
    REQUIRE(eventually([&] { return counters.remote_stream_audio_decode_failures.load() == 1; }));

    counters.remote_viewing_enabled = false;
    REQUIRE(eventually([&] { return output.stops.load() == 1; }));
    const auto started = Clock::now();
    stop = true;
    receiver.join();
    CHECK(Clock::now() - started < 250ms);
    CHECK_FALSE(counters.remote_stream_audio_active.load());
}

TEST_CASE("stream audio capture bridge keeps latency bounded") {
    StreamAudioCaptureBridge bridge;
    const StreamAudioPcmFrame frame = tone(0.0F);
    for (int index = 0; index < 6; ++index) {
        bridge.on_captured(frame);
    }
    StreamAudioPcmFrame popped{};
    int frames = 0;
    while (bridge.try_pop(popped)) {
        ++frames;
    }
    // Stale backlog beyond three frames is discarded before reading.
    CHECK(frames == 3);

    // Overflowing the ring requests a resync instead of growing latency.
    for (std::size_t index = 0; index < kStreamAudioQueueFrames + 2; ++index) {
        bridge.on_captured(frame);
    }
    CHECK(bridge.dropped_callbacks() >= 1);
    CHECK_FALSE(bridge.try_pop(popped));
    CHECK_FALSE(bridge.try_pop(popped));
}
