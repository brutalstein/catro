#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace catro::app {

enum class AppDestination : std::uint8_t {
    server,
    diagnostics,
    settings,
};

enum class ChannelKind : std::uint8_t {
    text,
    voice,
};

enum class ServerRole : std::uint8_t {
    owner,
    member,
};

struct ChannelSpec {
    std::string_view id;
    std::string_view name;
    ChannelKind kind = ChannelKind::text;

    friend constexpr bool operator==(const ChannelSpec&, const ChannelSpec&) = default;
};

// Every identity starts with exactly one personal server. The persistence/account layer will replace
// names and opaque ids, but these two channels are the stable first-run contract.
inline constexpr std::array<ChannelSpec, 2> kDefaultChannels{{
    {"general", "general", ChannelKind::text},
    {"voice", "Voice", ChannelKind::voice},
}};

struct PersonalServerContract {
    bool identity_owns_server = true;
    bool owner_is_only_elevated_role = true;
    bool invite_code_required_to_join = true;
    std::size_t default_text_channels = 1;
    std::size_t default_voice_channels = 1;

    friend constexpr bool operator==(const PersonalServerContract&, const PersonalServerContract&) = default;
};

inline constexpr PersonalServerContract kPersonalServerContract{};

[[nodiscard]] const ChannelSpec& channel_spec(std::string_view id) noexcept;
[[nodiscard]] std::optional<ChannelKind> channel_kind(std::string_view id) noexcept;
[[nodiscard]] constexpr bool can_manage_server(ServerRole role) noexcept {
    return role == ServerRole::owner;
}

class ShellState {
public:
    [[nodiscard]] AppDestination destination() const noexcept { return destination_; }
    [[nodiscard]] std::string_view channel_id() const noexcept { return channel_id_; }
    [[nodiscard]] ChannelKind active_channel_kind() const noexcept {
        return channel_spec(channel_id_).kind;
    }

    bool open_server() noexcept;
    bool open_diagnostics() noexcept;
    bool open_settings() noexcept;
    bool select_channel(std::string_view id) noexcept;

private:
    AppDestination destination_ = AppDestination::server;
    std::string_view channel_id_ = kDefaultChannels.front().id;
};

} // namespace catro::app
