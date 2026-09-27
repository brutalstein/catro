#include "ShellModel.hpp"

#include <algorithm>

namespace catro::app {

const ChannelSpec& channel_spec(std::string_view id) noexcept {
    const auto found = std::ranges::find(kDefaultChannels, id, &ChannelSpec::id);
    return found != kDefaultChannels.end() ? *found : kDefaultChannels.front();
}

std::optional<ChannelKind> channel_kind(std::string_view id) noexcept {
    const auto found = std::ranges::find(kDefaultChannels, id, &ChannelSpec::id);
    return found != kDefaultChannels.end() ? std::optional{found->kind} : std::nullopt;
}

bool ShellState::open_server() noexcept {
    if (destination_ == AppDestination::server) {
        return false;
    }
    destination_ = AppDestination::server;
    return true;
}

bool ShellState::open_diagnostics() noexcept {
    if (destination_ == AppDestination::diagnostics) {
        return false;
    }
    destination_ = AppDestination::diagnostics;
    return true;
}

bool ShellState::open_settings() noexcept {
    if (destination_ == AppDestination::settings) {
        return false;
    }
    destination_ = AppDestination::settings;
    return true;
}

bool ShellState::select_channel(std::string_view id) noexcept {
    const auto kind = channel_kind(id);
    if (!kind) {
        return false;
    }
    const bool changed = channel_id_ != id || destination_ != AppDestination::server;
    channel_id_ = channel_spec(id).id;
    destination_ = AppDestination::server;
    return changed;
}

} // namespace catro::app
