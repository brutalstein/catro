#pragma once

#include <catro/capabilities/model.hpp>

#include <algorithm>
#include <compare>
#include <cstddef>
#include <string>
#include <vector>

namespace catro::capabilities {

// Stable structural validation failures. Missing evidence and failed probes are not errors;
// they produce valid partial snapshots. These codes describe states that cannot be true.
enum class ValidationCode {
    unsupported_schema,
    invalid_generation,
    invalid_identifier,
    invalid_text,
    duplicate_id,
    duplicate_entry,
    missing_probe_reference,
    missing_gpu_reference,
    missing_display_reference,
    missing_encoder_reference,
    missing_capture_path_reference,
    missing_audio_endpoint_reference,
    unexpected_gpu_reference,
    missing_value,
    unexpected_value,
    missing_issue_code,
    contradictory_evidence,
    invalid_rational,
    invalid_quantity,
    invalid_range,
    codec_mismatch,
    platform_mismatch,
    inconsistent_color_format,
    invalid_transfer,
};

struct ValidationError {
    ValidationCode code = ValidationCode::unsupported_schema;
    // Location built from field names and typed IDs, e.g. "devices.encoders[mft:h264:0].gpu".
    std::string path;

    friend std::strong_ordering operator<=>(const ValidationError&, const ValidationError&) = default;
    friend bool operator==(const ValidationError&, const ValidationError&) = default;
};

struct ValidationReport {
    // Sorted by code, then path, independent of inventory enumeration order.
    std::vector<ValidationError> errors;

    [[nodiscard]] bool ok() const noexcept { return errors.empty(); }

    [[nodiscard]] bool contains(ValidationCode code) const noexcept {
        return std::ranges::any_of(errors, [code](const ValidationError& error) { return error.code == code; });
    }
};

// Identifiers are 1..128 bytes of printable ASCII without spaces; free text is at most 256 bytes.
inline constexpr std::size_t kMaxIdentifierBytes = 128;
inline constexpr std::size_t kMaxTextBytes = 256;

// Runs before policy evaluation, serialization, UI publication, and fragment merge.
// Never throws for malformed input; every problem becomes a stable error.
[[nodiscard]] ValidationReport validate(const CapabilitySnapshot& snapshot);

} // namespace catro::capabilities
