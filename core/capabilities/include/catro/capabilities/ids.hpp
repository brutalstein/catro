#pragma once

#include <compare>
#include <string>

namespace catro::capabilities {

// The widest lifetime over which an identifier is promised to denote the same device.
// Nothing wider than the underlying platform API documents may be claimed.
enum class IdentityScope {
    snapshot,
    service_lifetime,
    os_session,
    persistent,
};

// Distinct tag types keep GPU, encoder, display, capture, and audio identifiers from mixing.
template <class Tag>
struct ScopedId {
    std::string value;
    IdentityScope scope = IdentityScope::snapshot;

    // Byte-wise ordering; never locale-, pointer-, or enumeration-order dependent.
    friend std::strong_ordering operator<=>(const ScopedId&, const ScopedId&) = default;
    friend bool operator==(const ScopedId&, const ScopedId&) = default;
};

using GpuId = ScopedId<struct GpuIdTag>;
using EncoderId = ScopedId<struct EncoderIdTag>;
using CapturePathId = ScopedId<struct CapturePathIdTag>;
using DisplayId = ScopedId<struct DisplayIdTag>;
using AudioEndpointId = ScopedId<struct AudioEndpointIdTag>;

} // namespace catro::capabilities
