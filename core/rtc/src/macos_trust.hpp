#pragma once

#include <string>

namespace catro::rtc {

struct MacosTrustBundle {
    std::string path;
    std::string error;
};

// libdatachannel's Mbed TLS WebSocket backend cannot read the macOS system trust store itself.
// Export the native root anchors once per process and give Mbed TLS that PEM file.
[[nodiscard]] MacosTrustBundle macos_system_trust_bundle() noexcept;

} // namespace catro::rtc
