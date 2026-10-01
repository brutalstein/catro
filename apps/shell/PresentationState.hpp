#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace catro::app {

enum class Availability : std::uint8_t {
    ready,
    busy,
    unavailable,
    failed,
};

struct ActionState {
    Availability availability = Availability::ready;
    std::string reason;

    [[nodiscard]] static ActionState ready();
    [[nodiscard]] static ActionState unavailable(std::string message);
    [[nodiscard]] bool available() const noexcept;
    bool begin(std::string message);
    void enable();
    void disable(std::string message);
    void fail(std::string message);
};

enum class ConnectionState : std::uint8_t {
    local_only,
    connecting,
    synchronized,
    failed,
};

struct WorkspaceSnapshot {
    ConnectionState connection = ConnectionState::local_only;
    std::string connection_message = "Online services are not configured.";

    ActionState open_settings = ActionState::ready();
    ActionState open_system = ActionState::ready();
    ActionState join_server = ActionState::unavailable(connection_message);
    ActionState send_message = ActionState::unavailable(connection_message);
    ActionState join_voice = ActionState::unavailable(connection_message);
    ActionState share_screen = ActionState::unavailable(connection_message);

    [[nodiscard]] static WorkspaceSnapshot connecting();
    void synchronize();
    void fail(std::string message);

private:
    void set_online_actions(Availability availability, std::string_view reason);
};

} // namespace catro::app
