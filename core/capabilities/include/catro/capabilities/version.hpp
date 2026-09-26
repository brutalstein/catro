#pragma once

#include <compare>
#include <cstdint>
#include <string_view>

namespace catro::capabilities {

struct SchemaVersion {
    std::uint16_t major = 0;
    std::uint16_t minor = 0;

    friend constexpr auto operator<=>(const SchemaVersion&, const SchemaVersion&) = default;
};

// Policy revisions are versioned independently from the capability schema.
struct PolicyVersion {
    std::uint16_t major = 0;
    std::uint16_t minor = 0;
    std::uint16_t patch = 0;

    friend constexpr auto operator<=>(const PolicyVersion&, const PolicyVersion&) = default;
};

inline constexpr std::string_view kSchemaId = "catro.capabilities";
inline constexpr SchemaVersion kSchemaVersion{1, 0};
inline constexpr PolicyVersion kPolicyVersion{1, 0, 0};

// Minor schema revisions are additive: a reader understands its own major version and any
// minor revision it was built with or older. Newer minors may carry facts it cannot interpret.
constexpr bool is_supported(SchemaVersion version) noexcept {
    return version.major == kSchemaVersion.major && version.minor <= kSchemaVersion.minor;
}

// A policy revision changes selections, so only the exact implemented revision is supported.
constexpr bool is_supported(PolicyVersion version) noexcept {
    return version == kPolicyVersion;
}

} // namespace catro::capabilities
