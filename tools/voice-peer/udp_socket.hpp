#pragma once

#include <catro/transport/udp_peer_socket.hpp>

// Compatibility surface for the existing engineering peers. Product/runtime code depends directly
// on Catro::Transport; the tools keep their established namespace until their next cleanup pass.
namespace catro::tools {

using transport::UdpEndpoint;
using transport::UdpError;
using transport::UdpErrorCode;
using transport::UdpPeerSocket;
using transport::name;

} // namespace catro::tools
