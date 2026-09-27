#pragma once

#include <catro/community/model.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <variant>

namespace catro::platform::windows {

enum class LocalStateErrorCode : std::uint8_t {
    path_failure,
    lock_failure,
    not_found,
    read_failure,
    invalid_state,
    entropy_failure,
    write_failure,
    commit_failure,
};

struct LocalStateError {
    LocalStateErrorCode code = LocalStateErrorCode::read_failure;
    std::string detail;
    std::uint32_t native_code = 0;

    friend bool operator==(const LocalStateError&, const LocalStateError&) = default;
};

class SystemEntropy final : public community::EntropySource {
public:
    bool fill(std::span<std::byte> destination) noexcept override;
};

[[nodiscard]] std::variant<std::filesystem::path, LocalStateError> default_local_state_path();

[[nodiscard]] std::variant<community::LocalState, LocalStateError> load_local_state(
    const std::filesystem::path& path);

[[nodiscard]] std::optional<LocalStateError> save_local_state_atomic(
    const std::filesystem::path& path, const community::LocalState& state);

[[nodiscard]] std::variant<community::LocalState, LocalStateError> load_or_create_local_state(
    const std::filesystem::path& path, community::EntropySource& entropy);

[[nodiscard]] std::variant<community::LocalState, LocalStateError> load_or_create_default_local_state();

} // namespace catro::platform::windows
