#pragma once

#include <catro/community/model.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>

namespace catro::community {

inline constexpr std::string_view kLocalStateSchema = "catro.local-state";
inline constexpr std::uint32_t kLocalStateMajor = 1;
inline constexpr std::uint32_t kLocalStateMinor = 0;

enum class CodecErrorCode : std::uint8_t {
    too_large,
    invalid_json,
    wrong_schema,
    unsupported_version,
    invalid_field,
    invalid_state,
};

struct CodecError {
    CodecErrorCode code = CodecErrorCode::invalid_json;
    std::string detail;

    friend bool operator==(const CodecError&, const CodecError&) = default;
};

[[nodiscard]] std::string encode_local_state(const LocalState& state);
[[nodiscard]] std::variant<LocalState, CodecError> decode_local_state(std::string_view payload);

} // namespace catro::community
