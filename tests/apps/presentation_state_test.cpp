#include <PresentationState.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace catro::app;

TEST_CASE("workspace starts local-only with actionable reasons") {
    WorkspaceSnapshot state;

    CHECK(state.connection == ConnectionState::local_only);
    CHECK(state.connection_message == "Online services are not configured.");
    CHECK(state.join_server.availability == Availability::unavailable);
    CHECK(state.join_server.reason == state.connection_message);
    CHECK(state.open_settings.available());
    CHECK(state.open_system.available());
}

TEST_CASE("connecting and synchronized snapshots expose deterministic actions") {
    auto state = WorkspaceSnapshot::connecting();
    CHECK(state.connection == ConnectionState::connecting);
    CHECK(state.join_server.availability == Availability::busy);
    CHECK(state.join_server.reason == "Connecting to online services…");

    state.synchronize();
    CHECK(state.connection == ConnectionState::synchronized);
    CHECK(state.connection_message == "Online services connected.");
    CHECK(state.join_server.available());
    CHECK(state.send_message.available());
    CHECK(state.join_voice.available());
}

TEST_CASE("failed snapshot preserves local navigation and explains recovery") {
    auto state = WorkspaceSnapshot::connecting();
    state.fail("Could not reach the directory service.");

    CHECK(state.connection == ConnectionState::failed);
    CHECK(state.connection_message == "Could not reach the directory service.");
    CHECK(state.join_server.availability == Availability::unavailable);
    CHECK(state.join_server.reason == state.connection_message);
    CHECK(state.send_message.availability == Availability::unavailable);
    CHECK(state.open_settings.available());
    CHECK(state.open_system.available());
}

TEST_CASE("reconnecting keeps online actions waiting and backs off to 30 seconds") {
    auto state = WorkspaceSnapshot::connecting();
    state.reconnect("Can't reach Catro online. Retrying in 2 s.");
    CHECK(state.connection == ConnectionState::connecting);
    CHECK(state.join_server.availability == Availability::busy);
    CHECK(state.join_server.reason == state.connection_message);
    CHECK(state.open_settings.available());

    using std::chrono::seconds;
    CHECK(reconnect_delay(0) == seconds{2});
    CHECK(reconnect_delay(1) == seconds{4});
    CHECK(reconnect_delay(3) == seconds{16});
    CHECK(reconnect_delay(4) == seconds{30});
    CHECK(reconnect_delay(1000) == seconds{30});

    state.synchronize();
    CHECK(state.join_server.available());
}

TEST_CASE("busy action rejects duplicates and can recover") {
    ActionState action = ActionState::ready();
    CHECK(action.begin("Joining voice…"));
    CHECK(action.availability == Availability::busy);
    CHECK(action.reason == "Joining voice…");
    CHECK_FALSE(action.begin("Duplicate"));

    action.fail("Microphone access is unavailable.");
    CHECK(action.availability == Availability::failed);
    CHECK(action.reason == "Microphone access is unavailable.");

    action.enable();
    CHECK(action.available());
    CHECK(action.reason.empty());
}
