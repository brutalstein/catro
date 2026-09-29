#include <catro/room_runtime.h>

#include <catro/rtc/room_mesh_transport.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <new>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr std::size_t kMaxDatagramBytes = 2048;
constexpr std::size_t kVoiceQueueCapacity = 256;
constexpr std::size_t kVideoQueueCapacity = 4096;
constexpr std::size_t kStreamAudioQueueCapacity = 512;

struct Datagram {
    std::array<std::byte, kMaxDatagramBytes> bytes{};
    std::size_t size = 0;
};

class DatagramQueue final {
public:
    explicit DatagramQueue(std::size_t capacity) : capacity_(capacity) {}

    void push(std::span<const std::byte> datagram) noexcept {
        if (datagram.empty() || datagram.size() > kMaxDatagramBytes) {
            return;
        }
        try {
            std::scoped_lock lock(mutex_);
            if (queue_.size() >= capacity_) {
                queue_.pop_front();
                drops_.fetch_add(1, std::memory_order_relaxed);
            }
            Datagram owned;
            owned.size = datagram.size();
            std::memcpy(owned.bytes.data(), datagram.data(), datagram.size());
            queue_.push_back(std::move(owned));
            cv_.notify_one();
        } catch (...) {
            drops_.fetch_add(1, std::memory_order_relaxed);
        }
    }

    std::ptrdiff_t pop(
        std::span<std::byte> destination,
        std::chrono::milliseconds timeout,
        const std::atomic_bool& stopping) noexcept {
        if (destination.empty()) {
            return -1;
        }
        std::unique_lock lock(mutex_);
        cv_.wait_for(lock, timeout, [&] {
            return !queue_.empty() ||
                   stopping.load(std::memory_order_acquire);
        });
        if (queue_.empty()) {
            return 0;
        }
        auto datagram = std::move(queue_.front());
        queue_.pop_front();
        if (datagram.size > destination.size()) {
            return -1;
        }
        std::memcpy(destination.data(), datagram.bytes.data(), datagram.size);
        return static_cast<std::ptrdiff_t>(datagram.size);
    }

    void clear() noexcept {
        std::scoped_lock lock(mutex_);
        queue_.clear();
        cv_.notify_all();
    }

    void wake() noexcept {
        cv_.notify_all();
    }

    [[nodiscard]] std::uint64_t drops() const noexcept {
        return drops_.load(std::memory_order_relaxed);
    }

private:
    const std::size_t capacity_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Datagram> queue_;
    std::atomic<std::uint64_t> drops_{0};
};

class RoomRuntime final {
public:
    RoomRuntime()
        : voice_(kVoiceQueueCapacity),
          video_(kVideoQueueCapacity),
          stream_audio_(kStreamAudioQueueCapacity) {}

    ~RoomRuntime() { stop(); }

    std::int32_t start(const CatroRoomRuntimeConfig& raw) noexcept {
        stop();

        if (raw.signaling_url == nullptr ||
            raw.access_token == nullptr ||
            raw.server_id == nullptr ||
            raw.channel_id == nullptr ||
            raw.user_id == nullptr ||
            raw.ice_server_urls == nullptr ||
            raw.ice_server_count == 0 ||
            raw.max_remote_peers < 1 ||
            raw.max_remote_peers > 4) {
            set_error("invalid room runtime configuration");
            state_.store(CATRO_ROOM_FAILED, std::memory_order_release);
            return -1;
        }

        try {
            catro::rtc::RoomMeshConfig config;
            config.signaling_url = raw.signaling_url;
            config.access_token = raw.access_token;
            config.server_id = raw.server_id;
            config.channel_id = raw.channel_id;
            config.user_id = raw.user_id;
            config.max_peers =
                raw.max_remote_peers;
            config.allow_insecure_signaling =
                raw.allow_insecure_signaling != 0;
            config.allow_no_turn = raw.allow_no_turn != 0;
            config.ice_server_urls.reserve(raw.ice_server_count);
            for (std::size_t i = 0; i < raw.ice_server_count; ++i) {
                if (raw.ice_server_urls[i] == nullptr ||
                    raw.ice_server_urls[i][0] == '\0') {
                    set_error("invalid ICE server URL");
                    state_.store(CATRO_ROOM_FAILED, std::memory_order_release);
                    return -1;
                }
                config.ice_server_urls.emplace_back(raw.ice_server_urls[i]);
            }

            stopping_.store(false, std::memory_order_release);
            state_.store(CATRO_ROOM_CONNECTING, std::memory_order_release);
            set_error({});

            catro::rtc::RoomTransportCallbacks callbacks;
            callbacks.on_voice_datagram =
                [this](std::string_view, std::span<const std::byte> data) {
                    voice_.push(data);
                    voice_received_.fetch_add(1, std::memory_order_relaxed);
                };
            callbacks.on_video_datagram =
                [this](std::string_view, std::span<const std::byte> data) {
                    video_.push(data);
                    video_received_.fetch_add(1, std::memory_order_relaxed);
                };
            callbacks.on_stream_audio_datagram =
                [this](std::string_view, std::span<const std::byte> data) {
                    stream_audio_.push(data);
                    stream_audio_received_.fetch_add(
                        1, std::memory_order_relaxed);
                };
            callbacks.on_screen_owner =
                [this](std::string_view owner) {
                    set_screen_owner(owner);
                };
            callbacks.on_state =
                [this](catro::rtc::RoomTransportState next) {
                    switch (next) {
                    case catro::rtc::RoomTransportState::idle:
                        state_.store(CATRO_ROOM_IDLE, std::memory_order_release);
                        break;
                    case catro::rtc::RoomTransportState::connecting:
                        state_.store(CATRO_ROOM_CONNECTING, std::memory_order_release);
                        break;
                    case catro::rtc::RoomTransportState::joined:
                        state_.store(CATRO_ROOM_JOINED, std::memory_order_release);
                        break;
                    case catro::rtc::RoomTransportState::failed:
                        state_.store(CATRO_ROOM_FAILED, std::memory_order_release);
                        break;
                    }
                };
            callbacks.on_error =
                [this](std::string_view message) {
                    set_error(message);
                };

            if (const auto failure =
                    transport_.start(std::move(config), std::move(callbacks))) {
                set_error(failure->message);
                state_.store(CATRO_ROOM_FAILED, std::memory_order_release);
                stopping_.store(true, std::memory_order_release);
                return -1;
            }
        } catch (...) {
            set_error("room runtime startup failed");
            state_.store(CATRO_ROOM_FAILED, std::memory_order_release);
            stopping_.store(true, std::memory_order_release);
            return -1;
        }

        return 0;
    }

    void stop() noexcept {
        stopping_.store(true, std::memory_order_release);
        voice_.wake();
        video_.wake();
        stream_audio_.wake();
        transport_.stop();
        voice_.clear();
        video_.clear();
        stream_audio_.clear();
        set_screen_owner({});
        state_.store(CATRO_ROOM_IDLE, std::memory_order_release);
    }

    std::int32_t claim_screen() noexcept {
        if (state_.load(std::memory_order_acquire) !=
            CATRO_ROOM_JOINED) {
            return -1;
        }
        return transport_.claim_screen() ? 0 : -1;
    }

    void release_screen() noexcept {
        if (state_.load(std::memory_order_acquire) ==
            CATRO_ROOM_JOINED) {
            transport_.release_screen();
        }
    }

    std::size_t send_voice(std::span<const std::byte> data) noexcept {
        const auto peers = transport_.send_voice(data);
        if (peers != 0) {
            voice_sent_.fetch_add(1, std::memory_order_relaxed);
        }
        return peers;
    }

    std::size_t send_video(std::span<const std::byte> data) noexcept {
        const auto peers = transport_.send_video(data);
        if (peers != 0) {
            video_sent_.fetch_add(1, std::memory_order_relaxed);
        }
        return peers;
    }

    std::size_t send_stream_audio(
        std::span<const std::byte> data) noexcept {
        const auto peers =
            transport_.send_stream_audio(data);
        if (peers != 0) {
            stream_audio_sent_.fetch_add(
                1, std::memory_order_relaxed);
        }
        return peers;
    }

    std::ptrdiff_t receive_voice(
        std::span<std::byte> destination,
        std::uint32_t timeout_ms) noexcept {
        return voice_.pop(
            destination,
            std::chrono::milliseconds{timeout_ms},
            stopping_);
    }

    std::ptrdiff_t receive_video(
        std::span<std::byte> destination,
        std::uint32_t timeout_ms) noexcept {
        return video_.pop(
            destination,
            std::chrono::milliseconds{timeout_ms},
            stopping_);
    }

    std::ptrdiff_t receive_stream_audio(
        std::span<std::byte> destination,
        std::uint32_t timeout_ms) noexcept {
        return stream_audio_.pop(
            destination,
            std::chrono::milliseconds{timeout_ms},
            stopping_);
    }

    CatroRoomRuntimeSnapshot snapshot() const noexcept {
        CatroRoomRuntimeSnapshot result{};
        result.state = state_.load(std::memory_order_acquire);
        result.peer_count =
            static_cast<std::uint32_t>(
                std::min<std::size_t>(
                    transport_.peer_count(),
                    static_cast<std::size_t>(UINT32_MAX)));
        result.voice_sent_datagrams =
            voice_sent_.load(std::memory_order_relaxed);
        result.voice_received_datagrams =
            voice_received_.load(std::memory_order_relaxed);
        result.video_sent_datagrams =
            video_sent_.load(std::memory_order_relaxed);
        result.video_received_datagrams =
            video_received_.load(std::memory_order_relaxed);
        result.stream_audio_sent_datagrams =
            stream_audio_sent_.load(
                std::memory_order_relaxed);
        result.stream_audio_received_datagrams =
            stream_audio_received_.load(
                std::memory_order_relaxed);
        result.voice_queue_drops = voice_.drops();
        result.video_queue_drops = video_.drops();
        result.stream_audio_queue_drops =
            stream_audio_.drops();

        {
            std::scoped_lock lock(screen_owner_mutex_);
            if (!screen_owner_.empty()) {
                std::snprintf(
                    result.screen_owner,
                    sizeof(result.screen_owner),
                    "%.*s",
                    static_cast<int>(
                        sizeof(result.screen_owner) - 1),
                    screen_owner_.c_str());
            }
        }

        std::scoped_lock lock(error_mutex_);
        if (!error_.empty()) {
            std::snprintf(
                result.error,
                sizeof(result.error),
                "%.*s",
                static_cast<int>(sizeof(result.error) - 1),
                error_.c_str());
        }
        return result;
    }

private:
    void set_screen_owner(
        std::string_view owner) noexcept {
        try {
            std::scoped_lock lock(
                screen_owner_mutex_);
            screen_owner_.assign(owner);
            if (screen_owner_.size() > 128) {
                screen_owner_.resize(128);
            }
        } catch (...) {
        }
    }

    void set_error(std::string_view message) noexcept {
        try {
            std::scoped_lock lock(error_mutex_);
            error_.assign(message);
            if (error_.size() > 1024) {
                error_.resize(1024);
            }
        } catch (...) {
        }
    }

    catro::rtc::RoomMeshTransport transport_;
    DatagramQueue voice_;
    DatagramQueue video_;
    DatagramQueue stream_audio_;

    std::atomic_bool stopping_{true};
    std::atomic<std::int32_t> state_{CATRO_ROOM_IDLE};
    std::atomic<std::uint64_t> voice_sent_{0};
    std::atomic<std::uint64_t> voice_received_{0};
    std::atomic<std::uint64_t> video_sent_{0};
    std::atomic<std::uint64_t> video_received_{0};
    std::atomic<std::uint64_t> stream_audio_sent_{0};
    std::atomic<std::uint64_t> stream_audio_received_{0};

    mutable std::mutex screen_owner_mutex_;
    std::string screen_owner_;
    mutable std::mutex error_mutex_;
    std::string error_;
};

RoomRuntime* runtime(CatroRoomRuntimeHandle handle) noexcept {
    return static_cast<RoomRuntime*>(handle);
}

} // namespace

extern "C" {

CatroRoomRuntimeHandle catro_room_runtime_create() noexcept {
    return new (std::nothrow) RoomRuntime();
}

void catro_room_runtime_destroy(CatroRoomRuntimeHandle handle) noexcept {
    delete runtime(handle);
}

std::int32_t catro_room_runtime_start(
    CatroRoomRuntimeHandle handle,
    const CatroRoomRuntimeConfig* config) noexcept {
    if (handle == nullptr || config == nullptr) {
        return -1;
    }
    return runtime(handle)->start(*config);
}

void catro_room_runtime_stop(CatroRoomRuntimeHandle handle) noexcept {
    if (handle != nullptr) {
        runtime(handle)->stop();
    }
}

std::int32_t catro_room_runtime_claim_screen(
    CatroRoomRuntimeHandle handle) noexcept {
    return handle != nullptr
               ? runtime(handle)->claim_screen()
               : -1;
}

void catro_room_runtime_release_screen(
    CatroRoomRuntimeHandle handle) noexcept {
    if (handle != nullptr) {
        runtime(handle)->release_screen();
    }
}

std::size_t catro_room_runtime_send_voice(
    CatroRoomRuntimeHandle handle,
    const std::byte* data,
    std::size_t size) noexcept {
    if (handle == nullptr || data == nullptr || size == 0) {
        return 0;
    }
    return runtime(handle)->send_voice(
        std::span<const std::byte>(data, size));
}

std::size_t catro_room_runtime_send_video(
    CatroRoomRuntimeHandle handle,
    const std::byte* data,
    std::size_t size) noexcept {
    if (handle == nullptr || data == nullptr || size == 0) {
        return 0;
    }
    return runtime(handle)->send_video(
        std::span<const std::byte>(data, size));
}

std::size_t catro_room_runtime_send_stream_audio(
    CatroRoomRuntimeHandle handle,
    const std::byte* data,
    std::size_t size) noexcept {
    if (handle == nullptr || data == nullptr || size == 0) {
        return 0;
    }
    return runtime(handle)->send_stream_audio(
        std::span<const std::byte>(data, size));
}

std::ptrdiff_t catro_room_runtime_receive_voice(
    CatroRoomRuntimeHandle handle,
    std::byte* destination,
    std::size_t capacity,
    std::uint32_t timeout_ms) noexcept {
    if (handle == nullptr || destination == nullptr || capacity == 0) {
        return -1;
    }
    return runtime(handle)->receive_voice(
        std::span<std::byte>(destination, capacity),
        timeout_ms);
}

std::ptrdiff_t catro_room_runtime_receive_video(
    CatroRoomRuntimeHandle handle,
    std::byte* destination,
    std::size_t capacity,
    std::uint32_t timeout_ms) noexcept {
    if (handle == nullptr || destination == nullptr || capacity == 0) {
        return -1;
    }
    return runtime(handle)->receive_video(
        std::span<std::byte>(destination, capacity),
        timeout_ms);
}

std::ptrdiff_t catro_room_runtime_receive_stream_audio(
    CatroRoomRuntimeHandle handle,
    std::byte* destination,
    std::size_t capacity,
    std::uint32_t timeout_ms) noexcept {
    if (handle == nullptr || destination == nullptr ||
        capacity == 0) {
        return -1;
    }
    return runtime(handle)->receive_stream_audio(
        std::span<std::byte>(destination, capacity),
        timeout_ms);
}

CatroRoomRuntimeSnapshot catro_room_runtime_snapshot(
    CatroRoomRuntimeHandle handle) noexcept {
    if (handle == nullptr) {
        CatroRoomRuntimeSnapshot result{};
        result.state = CATRO_ROOM_FAILED;
        std::snprintf(
            result.error,
            sizeof(result.error),
            "%s",
            "room runtime unavailable");
        return result;
    }
    return runtime(handle)->snapshot();
}

} // extern "C"
