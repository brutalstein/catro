#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <variant>

namespace catro::tools {

struct UdpEndpoint {
    std::string address;
    std::uint16_t port = 0;

    friend bool operator==(const UdpEndpoint&, const UdpEndpoint&) = default;
};

enum class UdpErrorCode {
    invalid_endpoint,
    network_startup_failed,
    socket_create_failed,
    bind_failed,
    connect_failed,
    wait_failed,
    send_failed,
    receive_failed,
    datagram_too_large,
    peer_unreachable,
    would_block,
};

struct UdpError {
    UdpErrorCode code = UdpErrorCode::socket_create_failed;
    int native_code = 0;

    friend bool operator==(const UdpError&, const UdpError&) = default;
};

[[nodiscard]] constexpr std::string_view name(UdpErrorCode code) noexcept {
    switch (code) {
    case UdpErrorCode::invalid_endpoint:
        return "invalid endpoint";
    case UdpErrorCode::network_startup_failed:
        return "network startup failed";
    case UdpErrorCode::socket_create_failed:
        return "socket creation failed";
    case UdpErrorCode::bind_failed:
        return "bind failed";
    case UdpErrorCode::connect_failed:
        return "connect failed";
    case UdpErrorCode::wait_failed:
        return "wait failed";
    case UdpErrorCode::send_failed:
        return "send failed";
    case UdpErrorCode::receive_failed:
        return "receive failed";
    case UdpErrorCode::datagram_too_large:
        return "datagram too large";
    case UdpErrorCode::peer_unreachable:
        return "peer unreachable";
    case UdpErrorCode::would_block:
        return "would block";
    }
    return "UDP error";
}

// Connected UDP socket shared by the engineering media peers. Numeric IPv4 keeps resolution and
// DNS off the real-time validation path. The socket is non-blocking; wait_readable sleeps in select.
// send_segments() maps directly to WSASend/sendmsg so RTP headers and encoded payload can leave the
// process as one UDP datagram without first being concatenated into a temporary heap buffer.
class UdpPeerSocket {
public:
    using OpenResult = std::variant<std::unique_ptr<UdpPeerSocket>, UdpError>;
    using StatusResult = std::variant<std::monostate, UdpError>;
    using SizeResult = std::variant<std::size_t, UdpError>;
    using WaitResult = std::variant<bool, UdpError>;

    [[nodiscard]] static OpenResult bind(const UdpEndpoint& local) noexcept;

    ~UdpPeerSocket();
    UdpPeerSocket(const UdpPeerSocket&) = delete;
    UdpPeerSocket& operator=(const UdpPeerSocket&) = delete;

    [[nodiscard]] StatusResult connect_peer(const UdpEndpoint& peer) noexcept;
    [[nodiscard]] SizeResult send(std::span<const std::byte> datagram) noexcept;
    [[nodiscard]] SizeResult send_segments(
        std::span<const std::span<const std::byte>> segments) noexcept;
    // true = readable, false = timeout.
    [[nodiscard]] WaitResult wait_readable(std::chrono::microseconds timeout) noexcept;
    // 0 means the non-blocking socket had no datagram available.
    [[nodiscard]] SizeResult receive(std::span<std::byte> buffer) noexcept;

    [[nodiscard]] std::uint16_t local_port() const noexcept;

private:
    struct Impl;
    explicit UdpPeerSocket(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> impl_;
};

} // namespace catro::tools
