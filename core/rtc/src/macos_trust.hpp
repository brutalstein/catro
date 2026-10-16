#pragma once

#include <string>

namespace catro::rtc {

// libdatachannel's Mbed TLS WebSocket backend cannot read the macOS system trust store itself.
// Returns PEM content (libdatachannel accepts it in place of a file path): the native root anchors,
// exported once per process, followed by the Let's Encrypt roots the signaling server chains to,
// so verification works even when the system export fails or the Mac's store is outdated.
[[nodiscard]] const std::string& macos_trust_pem();

// The embedded Let's Encrypt roots alone; tests verify them against the live server.
[[nodiscard]] const char* letsencrypt_root_pem() noexcept;

} // namespace catro::rtc
