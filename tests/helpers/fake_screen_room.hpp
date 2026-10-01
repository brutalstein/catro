#pragma once

#include <catro/screen_transport_runtime.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <mutex>
#include <vector>

// In-memory two-peer room behind the RoomScreenApi table, shared by the portable and macOS screen tests.
namespace catro::test {

using screen::RoomScreenApi;

using Datagram = std::vector<std::byte>;

// One bounded in-memory lane of a fake room: send() lands in the peer's inbox, like the RTC runtime.
class Lane {
public:
    void push(const std::byte* data, std::size_t size) {
        {
            std::scoped_lock lock(mutex_);
            if (inbox_.size() == 4096) {
                inbox_.pop_front();
            }
            inbox_.emplace_back(data, data + size);
        }
        ready_.notify_all();
    }

    std::ptrdiff_t pop(std::byte* destination, std::size_t capacity, std::uint32_t timeout_ms) {
        std::unique_lock lock(mutex_);
        if (flood_ != nullptr) {
            // Endless traffic: every receive returns a datagram immediately.
            const auto size = std::min(capacity, flood_->size());
            std::memcpy(destination, flood_->data(), size);
            return static_cast<std::ptrdiff_t>(size);
        }
        ready_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [&] { return !inbox_.empty(); });
        if (inbox_.empty()) {
            return 0;
        }
        auto datagram = std::move(inbox_.front());
        inbox_.pop_front();
        if (datagram.size() > capacity) {
            return -1;
        }
        std::memcpy(destination, datagram.data(), datagram.size());
        return static_cast<std::ptrdiff_t>(datagram.size());
    }

    void flood(const Datagram* datagram) {
        std::scoped_lock lock(mutex_);
        flood_ = datagram;
    }

    std::size_t size() {
        std::scoped_lock lock(mutex_);
        return inbox_.size();
    }

private:
    std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<Datagram> inbox_;
    const Datagram* flood_ = nullptr;
};

struct FakeRoom {
    std::atomic<std::int32_t> state{CATRO_ROOM_JOINED};
    std::atomic<std::size_t> peers{1};
    FakeRoom* peer = nullptr;
    Lane video;
    Lane audio;
};

inline FakeRoom& room(CatroRoomRuntimeHandle handle) {
    return *static_cast<FakeRoom*>(handle);
}

inline RoomScreenApi fake_api() {
    return RoomScreenApi{
        .snapshot = [](CatroRoomRuntimeHandle handle) noexcept {
            CatroRoomRuntimeSnapshot snapshot{};
            snapshot.state = room(handle).state.load();
            if (snapshot.state == CATRO_ROOM_FAILED) {
                constexpr char kError[] = "fake room failed";
                std::memcpy(snapshot.error, kError, sizeof(kError));
            }
            return snapshot;
        },
        .send_video = [](CatroRoomRuntimeHandle handle, const std::byte* data, std::size_t size) noexcept {
            auto& self = room(handle);
            if (self.peer != nullptr) {
                self.peer->video.push(data, size);
            }
            return self.peers.load();
        },
        .receive_video = [](CatroRoomRuntimeHandle handle, std::byte* destination, std::size_t capacity,
                            std::uint32_t timeout_ms) noexcept {
            return room(handle).video.pop(destination, capacity, timeout_ms);
        },
        .send_stream_audio = [](CatroRoomRuntimeHandle handle, const std::byte* data, std::size_t size) noexcept {
            auto& self = room(handle);
            if (self.peer != nullptr) {
                self.peer->audio.push(data, size);
            }
            return self.peers.load();
        },
        .receive_stream_audio = [](CatroRoomRuntimeHandle handle, std::byte* destination, std::size_t capacity,
                                   std::uint32_t timeout_ms) noexcept {
            return room(handle).audio.pop(destination, capacity, timeout_ms);
        },
    };
}

} // namespace catro::test
