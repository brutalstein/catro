#include <catro/community/model.hpp>
#include <catro/community/state_codec.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>

using namespace catro::community;

namespace {

class SequenceEntropy final : public EntropySource {
public:
    explicit SequenceEntropy(std::uint8_t seed = 1) : next_(seed) {}

    bool fill(std::span<std::byte> destination) noexcept override {
        for (auto& value : destination) {
            value = static_cast<std::byte>(next_++);
            if (next_ == 0) {
                next_ = 1;
            }
        }
        return true;
    }

private:
    std::uint8_t next_;
};

class FailingEntropy final : public EntropySource {
public:
    bool fill(std::span<std::byte>) noexcept override {
        return false;
    }
};

LocalState state() {
    SequenceEntropy entropy;
    auto created = bootstrap_personal_state(entropy);
    REQUIRE(std::holds_alternative<LocalState>(created));
    return std::get<LocalState>(std::move(created));
}

} // namespace

TEST_CASE("bootstrap creates stable personal server invariants from injected entropy") {
    SequenceEntropy first_entropy;
    SequenceEntropy second_entropy;

    const auto first = bootstrap_personal_state(first_entropy);
    const auto second = bootstrap_personal_state(second_entropy);
    REQUIRE(std::holds_alternative<LocalState>(first));
    REQUIRE(std::holds_alternative<LocalState>(second));

    const auto& local = std::get<LocalState>(first);
    CHECK(local == std::get<LocalState>(second));
    CHECK_FALSE(local.identity.id.empty());
    CHECK_FALSE(local.personal_server.id.empty());
    CHECK(local.personal_server.owner_id == local.identity.id);
    REQUIRE(local.personal_server.channels.size() == 2);
    CHECK(local.personal_server.channels[0].name == "general");
    CHECK(local.personal_server.channels[0].kind == ChannelKind::text);
    CHECK(local.personal_server.channels[1].name == "Voice");
    CHECK(local.personal_server.channels[1].kind == ChannelKind::voice);
    REQUIRE(local.personal_server.members.size() == 1);
    CHECK(local.personal_server.members[0].user_id == local.identity.id);
    CHECK(local.personal_server.members[0].role == ServerRole::owner);
    CHECK_FALSE(validate(local));
}

TEST_CASE("bootstrap reports secure entropy failure instead of fabricating identity") {
    FailingEntropy entropy;
    const auto created = bootstrap_personal_state(entropy);
    REQUIRE(std::holds_alternative<StateError>(created));
    CHECK(std::get<StateError>(created).code == StateErrorCode::entropy_failure);
}

TEST_CASE("typed identifiers round trip and reject zero or malformed text") {
    const auto local = state();

    const auto user = to_hex(local.identity.id);
    const auto server = to_hex(local.personal_server.id);
    const auto channel = to_hex(local.personal_server.channels.front().id);
    CHECK(user.size() == 32);
    CHECK(server.size() == 32);
    CHECK(channel.size() == 32);
    REQUIRE(user_id_from_hex(user));
    REQUIRE(server_id_from_hex(server));
    REQUIRE(channel_id_from_hex(channel));
    CHECK(*user_id_from_hex(user) == local.identity.id);
    CHECK(*server_id_from_hex(server) == local.personal_server.id);
    CHECK(*channel_id_from_hex(channel) == local.personal_server.channels.front().id);

    CHECK_FALSE(user_id_from_hex("00"));
    CHECK_FALSE(user_id_from_hex(std::string(32, '0')));
    CHECK_FALSE(user_id_from_hex(std::string(32, 'z')));
}

TEST_CASE("invite code preserves all 128 random bits in canonical Crockford form") {
    SequenceEntropy entropy(77);
    const auto local = state();
    const auto created = create_invite(entropy, local.personal_server.id, local.identity.id);
    REQUIRE(std::holds_alternative<Invite>(created));

    const auto invite = std::get<Invite>(created);
    const auto text = format_invite_code(invite.code);
    CHECK(text.size() == 26);
    REQUIRE(parse_invite_code(text));
    CHECK(*parse_invite_code(text) == invite.code);

    auto lower = text;
    std::ranges::transform(lower, lower.begin(), [](char value) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
    });
    REQUIRE(parse_invite_code(lower));
    CHECK(*parse_invite_code(lower) == invite.code);

    CHECK_FALSE(parse_invite_code("short"));
    auto impossible = text;
    impossible[0] = 'Z';
    CHECK_FALSE(parse_invite_code(impossible));
}

TEST_CASE("only the owner receives elevated server authority") {
    CHECK(can_manage_server(ServerRole::owner));
    CHECK_FALSE(can_manage_server(ServerRole::member));

    auto local = state();
    local.personal_server.members.push_back(Member{
        .user_id = UserId{{std::byte{0x7f}, std::byte{1}}},
        .role = ServerRole::owner,
    });
    REQUIRE(validate(local));
    CHECK(validate(local)->code == StateErrorCode::invalid_owner);
}

TEST_CASE("state validation allows future channels while retaining text and voice roots") {
    auto local = state();
    auto extra = local.personal_server.channels.front();
    extra.id.bytes[0] = std::byte{0xee};
    extra.name = "games";
    local.personal_server.channels.push_back(extra);
    CHECK_FALSE(validate(local));

    local.personal_server.channels.erase(
        std::remove_if(local.personal_server.channels.begin(), local.personal_server.channels.end(),
                       [](const Channel& channel) { return channel.kind == ChannelKind::voice; }),
        local.personal_server.channels.end());
    REQUIRE(validate(local));
    CHECK(validate(local)->code == StateErrorCode::invalid_channel_layout);
}

TEST_CASE("local state JSON is deterministic versioned and round trips") {
    const auto local = state();
    const auto first = encode_local_state(local);
    const auto second = encode_local_state(local);
    REQUIRE(std::holds_alternative<std::string>(first));
    REQUIRE(std::holds_alternative<std::string>(second));
    CHECK(std::get<std::string>(first) == std::get<std::string>(second));
    CHECK(std::get<std::string>(first).find("catro.local-state") != std::string::npos);

    const auto decoded = decode_local_state(std::get<std::string>(first));
    REQUIRE(std::holds_alternative<LocalState>(decoded));
    CHECK(std::get<LocalState>(decoded) == local);
}

TEST_CASE("local state codec rejects malformed oversized and incompatible payloads") {
    const auto malformed = decode_local_state("{");
    REQUIRE(std::holds_alternative<CodecError>(malformed));
    CHECK(std::get<CodecError>(malformed).code == CodecErrorCode::invalid_json);

    const auto oversized = decode_local_state(std::string(kMaxStateBytes + 1, 'x'));
    REQUIRE(std::holds_alternative<CodecError>(oversized));
    CHECK(std::get<CodecError>(oversized).code == CodecErrorCode::too_large);

    const auto incompatible = decode_local_state(
        R"({"schema":{"id":"catro.local-state","major":99,"minor":0},"identity":{},"personal_server":{}})");
    REQUIRE(std::holds_alternative<CodecError>(incompatible));
    CHECK(std::get<CodecError>(incompatible).code == CodecErrorCode::unsupported_version);
}
