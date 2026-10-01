#include "PresentationState.hpp"

#include <utility>

namespace catro::app {

ActionState ActionState::ready() {
    return {};
}

ActionState ActionState::unavailable(std::string message) {
    return {Availability::unavailable, std::move(message)};
}

bool ActionState::available() const noexcept {
    return availability == Availability::ready;
}

bool ActionState::begin(std::string message) {
    if (!available()) {
        return false;
    }
    availability = Availability::busy;
    reason = std::move(message);
    return true;
}

void ActionState::enable() {
    availability = Availability::ready;
    reason.clear();
}

void ActionState::disable(std::string message) {
    availability = Availability::unavailable;
    reason = std::move(message);
}

void ActionState::fail(std::string message) {
    availability = Availability::failed;
    reason = std::move(message);
}

WorkspaceSnapshot WorkspaceSnapshot::connecting() {
    WorkspaceSnapshot snapshot;
    snapshot.connection = ConnectionState::connecting;
    snapshot.connection_message = "Connecting to online services…";
    snapshot.set_online_actions(Availability::busy, snapshot.connection_message);
    return snapshot;
}

void WorkspaceSnapshot::synchronize() {
    connection = ConnectionState::synchronized;
    connection_message = "Online services connected.";
    set_online_actions(Availability::ready, {});
}

void WorkspaceSnapshot::fail(std::string message) {
    connection = ConnectionState::failed;
    connection_message = std::move(message);
    set_online_actions(Availability::unavailable, connection_message);
}

void WorkspaceSnapshot::set_online_actions(
    Availability availability,
    std::string_view reason) {
    const auto apply = [availability, reason](ActionState& action) {
        action.availability = availability;
        action.reason = reason;
    };
    apply(join_server);
    apply(send_message);
    apply(join_voice);
    apply(share_screen);
}

} // namespace catro::app
