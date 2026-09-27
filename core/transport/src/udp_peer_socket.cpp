#include <catro/transport/udp_peer_socket.hpp>

#include <algorithm>
#include <array>
#include <climits>
#include <limits>
#include <new>
#include <utility>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mstcpip.h>

// Some Windows SDK / toolset combinations do not expose this vendor IOCTL from mstcpip.h even
// though Winsock supports it. This is the value used by the Windows SDK and Microsoft samples.
#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>
#endif

namespace catro::transport {
namespace {

constexpr std::size_t kMaxUdpPayloadBytes = 65'507;
constexpr std::size_t kMaxSendSegments = 8;

#if defined(_WIN32)
using NativeSocket = SOCKET;
constexpr NativeSocket kInvalidSocket = INVALID_SOCKET;

int last_socket_error() noexcept {
    return WSAGetLastError();
}

bool would_block(int code) noexcept {
    return code == WSAEWOULDBLOCK;
}

bool peer_unreachable(int code) noexcept {
    return code == WSAECONNRESET || code == WSAECONNREFUSED ||
           code == WSAEHOSTUNREACH || code == WSAENETUNREACH;
}

void close_socket(NativeSocket socket) noexcept {
    if (socket != kInvalidSocket) {
        closesocket(socket);
    }
}

bool set_nonblocking(NativeSocket socket) noexcept {
    u_long enabled = 1;
    return ioctlsocket(socket, FIONBIO, &enabled) == 0;
}
#else
using NativeSocket = int;
constexpr NativeSocket kInvalidSocket = -1;

int last_socket_error() noexcept {
    return errno;
}

bool would_block(int code) noexcept {
    return code == EAGAIN || code == EWOULDBLOCK;
}

bool peer_unreachable(int code) noexcept {
    return code == ECONNREFUSED || code == EHOSTUNREACH || code == ENETUNREACH;
}

void close_socket(NativeSocket socket) noexcept {
    if (socket != kInvalidSocket) {
        close(socket);
    }
}

bool set_nonblocking(NativeSocket socket) noexcept {
    const auto flags = fcntl(socket, F_GETFL, 0);
    return flags >= 0 && fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0;
}
#endif

bool development_address_allowed(const in_addr& address) noexcept {
    const auto host = ntohl(address.s_addr);
    const auto loopback = (host & 0xff000000U) == 0x7f000000U;
    const auto private_10 = (host & 0xff000000U) == 0x0a000000U;
    const auto private_172 = (host & 0xfff00000U) == 0xac100000U;
    const auto private_192 = (host & 0xffff0000U) == 0xc0a80000U;
    const auto link_local = (host & 0xffff0000U) == 0xa9fe0000U;
    return loopback || private_10 || private_172 || private_192 || link_local;
}

bool make_address(const UdpEndpoint& endpoint, sockaddr_in& address, bool allow_zero_port) noexcept {
    if (endpoint.address.empty() || (!allow_zero_port && endpoint.port == 0)) {
        return false;
    }
    address = {};
    address.sin_family = AF_INET;
    address.sin_port = htons(endpoint.port);
    if (inet_pton(AF_INET, endpoint.address.c_str(), &address.sin_addr) != 1) {
        return false;
    }
    // This executable is deliberately not an Internet transport. Enforce the documented trust
    // boundary in code rather than relying on the operator to remember it.
    return development_address_allowed(address.sin_addr);
}

void tune_socket(NativeSocket socket) noexcept {
    const int receive_bytes = 256 * 1024;
    const int send_bytes = 128 * 1024;
#if defined(_WIN32)
    (void)setsockopt(socket, SOL_SOCKET, SO_RCVBUF,
                     reinterpret_cast<const char*>(&receive_bytes), static_cast<int>(sizeof(receive_bytes)));
    (void)setsockopt(socket, SOL_SOCKET, SO_SNDBUF,
                     reinterpret_cast<const char*>(&send_bytes), static_cast<int>(sizeof(send_bytes)));

    // Windows otherwise turns an ICMP port-unreachable into WSAECONNRESET on a later recv. A
    // development peer may legitimately start before its partner, so keep the UDP socket alive.
    BOOL behavior = FALSE;
    DWORD bytes = 0;
    (void)WSAIoctl(socket, SIO_UDP_CONNRESET, &behavior, static_cast<DWORD>(sizeof(behavior)), nullptr, 0, &bytes,
                   nullptr, nullptr);
#else
    (void)setsockopt(socket, SOL_SOCKET, SO_RCVBUF, &receive_bytes, sizeof(receive_bytes));
    (void)setsockopt(socket, SOL_SOCKET, SO_SNDBUF, &send_bytes, sizeof(send_bytes));
#endif
}

} // namespace

struct UdpPeerSocket::Impl {
    NativeSocket socket = kInvalidSocket;
#if defined(_WIN32)
    bool winsock_started = false;
#endif

    ~Impl() {
        close_socket(socket);
#if defined(_WIN32)
        if (winsock_started) {
            WSACleanup();
        }
#endif
    }
};

UdpPeerSocket::UdpPeerSocket(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}

UdpPeerSocket::~UdpPeerSocket() = default;

UdpPeerSocket::OpenResult UdpPeerSocket::bind(const UdpEndpoint& local) noexcept {
    sockaddr_in address{};
    if (!make_address(local, address, true)) {
        return UdpError{UdpErrorCode::invalid_endpoint};
    }

    auto impl = std::unique_ptr<Impl>(new (std::nothrow) Impl());
    if (!impl) {
        return UdpError{UdpErrorCode::socket_create_failed};
    }

#if defined(_WIN32)
    WSADATA data{};
    const auto startup = WSAStartup(MAKEWORD(2, 2), &data);
    if (startup != 0) {
        return UdpError{UdpErrorCode::network_startup_failed, startup};
    }
    impl->winsock_started = true;
#endif

    impl->socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (impl->socket == kInvalidSocket) {
        return UdpError{UdpErrorCode::socket_create_failed, last_socket_error()};
    }

    tune_socket(impl->socket);
    if (!set_nonblocking(impl->socket)) {
        return UdpError{UdpErrorCode::socket_create_failed, last_socket_error()};
    }

    if (::bind(impl->socket, reinterpret_cast<const sockaddr*>(&address), static_cast<int>(sizeof(address))) != 0) {
        return UdpError{UdpErrorCode::bind_failed, last_socket_error()};
    }

    auto result = std::unique_ptr<UdpPeerSocket>(new (std::nothrow) UdpPeerSocket(std::move(impl)));
    if (!result) {
        return UdpError{UdpErrorCode::socket_create_failed};
    }
    return std::move(result);
}

UdpPeerSocket::StatusResult UdpPeerSocket::connect_peer(const UdpEndpoint& peer) noexcept {
    sockaddr_in address{};
    if (!make_address(peer, address, false)) {
        return UdpError{UdpErrorCode::invalid_endpoint};
    }
    if (::connect(impl_->socket, reinterpret_cast<const sockaddr*>(&address), static_cast<int>(sizeof(address))) != 0) {
        return UdpError{UdpErrorCode::connect_failed, last_socket_error()};
    }
    return std::monostate{};
}

UdpPeerSocket::SizeResult UdpPeerSocket::send(std::span<const std::byte> datagram) noexcept {
    const std::array segments{datagram};
    return send_segments(segments);
}

UdpPeerSocket::SizeResult UdpPeerSocket::send_segments(
    std::span<const std::span<const std::byte>> segments) noexcept {
    if (segments.empty()) {
        return std::size_t{0};
    }
    if (segments.size() > kMaxSendSegments) {
        return UdpError{UdpErrorCode::datagram_too_large};
    }

    std::size_t total = 0;
    for (const auto segment : segments) {
        if (segment.size() > kMaxUdpPayloadBytes - std::min(total, kMaxUdpPayloadBytes)) {
            return UdpError{UdpErrorCode::datagram_too_large};
        }
        total += segment.size();
    }
    if (total == 0) {
        return std::size_t{0};
    }

#if defined(_WIN32)
    std::array<WSABUF, kMaxSendSegments> buffers{};
    for (std::size_t index = 0; index < segments.size(); ++index) {
        // WSASend predates const-correct buffer declarations. It does not modify send buffers.
        buffers[index].buf = reinterpret_cast<char*>(
            const_cast<std::byte*>(segments[index].data()));
        buffers[index].len = static_cast<ULONG>(segments[index].size());
    }

    DWORD sent = 0;
    const auto result = WSASend(
        impl_->socket,
        buffers.data(),
        static_cast<DWORD>(segments.size()),
        &sent,
        0,
        nullptr,
        nullptr);
    if (result == SOCKET_ERROR) {
#else
    std::array<iovec, kMaxSendSegments> vectors{};
    for (std::size_t index = 0; index < segments.size(); ++index) {
        vectors[index].iov_base = const_cast<std::byte*>(segments[index].data());
        vectors[index].iov_len = segments[index].size();
    }

    msghdr message{};
    message.msg_iov = vectors.data();
    message.msg_iovlen = segments.size();
    const auto sent = ::sendmsg(impl_->socket, &message, 0);
    if (sent < 0) {
#endif
        const auto code = last_socket_error();
        if (would_block(code)) {
            return UdpError{UdpErrorCode::would_block, code};
        }
        if (peer_unreachable(code)) {
            return UdpError{UdpErrorCode::peer_unreachable, code};
        }
        return UdpError{UdpErrorCode::send_failed, code};
    }

    const auto sent_size = static_cast<std::size_t>(sent);
    if (sent_size != total) {
        return UdpError{UdpErrorCode::send_failed};
    }
    return sent_size;
}

UdpPeerSocket::WaitResult UdpPeerSocket::wait_readable(std::chrono::microseconds timeout) noexcept {
    const auto clamped = std::max(timeout, std::chrono::microseconds::zero());

    fd_set read_set;
    FD_ZERO(&read_set);
    FD_SET(impl_->socket, &read_set);

    timeval wait{};
    wait.tv_sec = static_cast<long>(clamped.count() / 1'000'000);
    wait.tv_usec = static_cast<long>(clamped.count() % 1'000'000);

#if defined(_WIN32)
    const auto result = select(0, &read_set, nullptr, nullptr, &wait);
#else
    const auto result = select(impl_->socket + 1, &read_set, nullptr, nullptr, &wait);
#endif
    if (result < 0) {
#if !defined(_WIN32)
        if (last_socket_error() == EINTR) {
            return false;
        }
#endif
        return UdpError{UdpErrorCode::wait_failed, last_socket_error()};
    }
    return result > 0;
}

UdpPeerSocket::SizeResult UdpPeerSocket::receive(std::span<std::byte> buffer) noexcept {
    if (buffer.empty()) {
        return std::size_t{0};
    }
#if defined(_WIN32)
    const auto size = static_cast<int>(std::min<std::size_t>(buffer.size(), static_cast<std::size_t>(INT_MAX)));
    const auto received = ::recv(impl_->socket, reinterpret_cast<char*>(buffer.data()), size, 0);
    if (received == SOCKET_ERROR) {
        const auto code = last_socket_error();
        if (would_block(code)) {
            return std::size_t{0};
        }
        if (code == WSAEMSGSIZE) {
            return UdpError{UdpErrorCode::datagram_too_large, code};
        }
        if (peer_unreachable(code)) {
            return UdpError{UdpErrorCode::peer_unreachable, code};
        }
        return UdpError{UdpErrorCode::receive_failed, code};
    }
    return static_cast<std::size_t>(received);
#else
    iovec io{};
    io.iov_base = buffer.data();
    io.iov_len = buffer.size();
    msghdr message{};
    message.msg_iov = &io;
    message.msg_iovlen = 1;
    const auto received = ::recvmsg(impl_->socket, &message, 0);
    if (received < 0) {
        const auto code = last_socket_error();
        if (would_block(code)) {
            return std::size_t{0};
        }
        if (peer_unreachable(code)) {
            return UdpError{UdpErrorCode::peer_unreachable, code};
        }
        return UdpError{UdpErrorCode::receive_failed, code};
    }
    if ((message.msg_flags & MSG_TRUNC) != 0) {
        return UdpError{UdpErrorCode::datagram_too_large};
    }
    return static_cast<std::size_t>(received);
#endif
}

std::uint16_t UdpPeerSocket::local_port() const noexcept {
    sockaddr_in address{};
#if defined(_WIN32)
    int size = static_cast<int>(sizeof(address));
#else
    socklen_t size = sizeof(address);
#endif
    if (getsockname(impl_->socket, reinterpret_cast<sockaddr*>(&address), &size) != 0) {
        return 0;
    }
    return ntohs(address.sin_port);
}

} // namespace catro::transport
