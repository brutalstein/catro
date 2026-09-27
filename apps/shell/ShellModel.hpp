#pragma once

#include <catro/community/model.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace catro::app {

enum class AppDestination : std::uint8_t {
    server,
    diagnostics,
    settings,
};

struct ChannelSpec {
    std::string_view id;
    std::string_view name;
    community::ChannelKind kind = community::ChannelKind::text;

    friend constexpr bool operator==(const ChannelSpec&, const ChannelSpec&) = default;
};

// Presentation ids stay stable while real ChannelId values come from LocalState. Names and kinds
// come from the community contract so UI defaults cannot drift from bootstrap state.
inline constexpr std::array<ChannelSpec, 2> kDefaultChannels{{
    {"general", community::kDefaultChannels[0].name, community::kDefaultChannels[0].kind},
    {"voice", community::kDefaultChannels[1].name, community::kDefaultChannels[1].kind},
}};

[[nodiscard]] const ChannelSpec& channel_spec(std::string_view id) noexcept;
[[nodiscard]] std::optional<community::ChannelKind> channel_kind(std::string_view id) noexcept;

class ShellState {
public:
    [[nodiscard]] AppDestination destination() const noexcept { return destination_; }
    [[nodiscard]] std::string_view channel_id() const noexcept { return channel_id_; }
    [[nodiscard]] community::ChannelKind active_channel_kind() const noexcept {
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
