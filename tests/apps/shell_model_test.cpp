#include "ShellModel.hpp"

#include <catch2/catch_test_macros.hpp>

#include <set>
#include <string>

using namespace catro::app;

TEST_CASE("personal server contract stays one-owner and invite-code based") {
    CHECK(kPersonalServerContract.identity_owns_server);
    CHECK(kPersonalServerContract.owner_is_only_elevated_role);
    CHECK(kPersonalServerContract.invite_code_required_to_join);
    CHECK(kPersonalServerContract.default_text_channels == 1);
    CHECK(kPersonalServerContract.default_voice_channels == 1);
    CHECK(can_manage_server(ServerRole::owner));
    CHECK_FALSE(can_manage_server(ServerRole::member));
}

TEST_CASE("first-run server has exactly one text and one voice channel") {
    REQUIRE(kDefaultChannels.size() == 2);

    std::set<std::string> ids;
    std::size_t text = 0;
    std::size_t voice = 0;
    for (const auto& channel : kDefaultChannels) {
        CHECK_FALSE(channel.id.empty());
        CHECK_FALSE(channel.name.empty());
        CHECK(ids.insert(std::string(channel.id)).second);
        REQUIRE(channel_kind(channel.id));
        CHECK(*channel_kind(channel.id) == channel.kind);
        text += channel.kind == ChannelKind::text ? 1U : 0U;
        voice += channel.kind == ChannelKind::voice ? 1U : 0U;
    }
    CHECK(text == 1);
    CHECK(voice == 1);
    CHECK_FALSE(channel_kind("missing"));
}

TEST_CASE("shell opens personal server and switches channels without invalid state") {
    ShellState state;
    CHECK(state.destination() == AppDestination::server);
    CHECK(state.channel_id() == "general");
    CHECK(state.active_channel_kind() == ChannelKind::text);

    CHECK(state.select_channel("voice"));
    CHECK(state.channel_id() == "voice");
    CHECK(state.active_channel_kind() == ChannelKind::voice);
    CHECK_FALSE(state.select_channel("voice"));

    CHECK(state.open_settings());
    CHECK(state.destination() == AppDestination::settings);
    CHECK(state.select_channel("general"));
    CHECK(state.destination() == AppDestination::server);
    CHECK(state.channel_id() == "general");

    CHECK_FALSE(state.select_channel("not-a-channel"));
    CHECK(state.channel_id() == "general");

    CHECK(state.open_diagnostics());
    CHECK(state.open_server());
    CHECK(state.destination() == AppDestination::server);
}
