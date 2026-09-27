#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace catro::community {

inline constexpr std::size_t kIdBytes = 16;
inline constexpr std::size_t kInviteBytes = 16;
inline constexpr std::size_t kMaxStateBytes = 64U * 1024U;
inline constexpr std::size_t kMaxMembers = 256;
inline constexpr std::size_t kMaxChannels = 64;
inline constexpr std::size_t kMaxDisplayNameBytes = 64;
inline constexpr std::size_t kMaxServerNameBytes = 80;
inline constexpr std::size_t kMaxChannelNameBytes = 64;

template <class Tag>
struct Id {
    std::array<std::byte, kIdBytes> bytes{};

    [[nodiscard]] constexpr bool empty() const noexcept {
        for (const auto value : bytes) {
            if (value != std::byte{0}) {
                return false;
            }
        }
        return true;
    }

    friend constexpr bool operator==(const Id&, const Id&) = default;
};

struct UserIdTag;
struct ServerIdTag;
struct ChannelIdTag;

using UserId = Id<UserIdTag>;
using ServerId = Id<ServerIdTag>;
using ChannelId = Id<ChannelIdTag>;

enum class ChannelKind : std::uint8_t {
    text,
    voice,
};

enum class ServerRole : std::uint8_t {
    owner,
    member,
};

struct Identity {
    UserId id;
    std::string display_name;

    friend bool operator==(const Identity&, const Identity&) = default;
};

struct Channel {
    ChannelId id;
    std::string name;
    ChannelKind kind = ChannelKind::text;

    friend bool operator==(const Channel&, const Channel&) = default;
};

struct Member {
    UserId user_id;
    ServerRole role = ServerRole::member;

    friend bool operator==(const Member&, const Member&) = default;
};

struct PersonalServer {
    ServerId id;
    UserId owner_id;
    std::string name;
    std::vector<Channel> channels;
    std::vector<Member> members;

    friend bool operator==(const PersonalServer&, const PersonalServer&) = default;
};

struct LocalState {
    Identity identity;
    PersonalServer personal_server;

    friend bool operator==(const LocalState&, const LocalState&) = default;
};

struct ChannelBlueprint {
    std::string_view name;
    ChannelKind kind;
};

inline constexpr std::array<ChannelBlueprint, 2> kDefaultChannels{{
    {"general", ChannelKind::text},
    {"Voice", ChannelKind::voice},
}};

enum class StateErrorCode : std::uint8_t {
    entropy_failure,
    invalid_identifier,
    invalid_name,
    invalid_owner,
    permission_denied,
    duplicate_identifier,
    invalid_channel_layout,
    too_many_channels,
    too_many_members,
};

struct StateError {
    StateErrorCode code = StateErrorCode::invalid_identifier;
    std::string detail;

    friend bool operator==(const StateError&, const StateError&) = default;
};

class EntropySource {
public:
    virtual ~EntropySource() = default;
    virtual bool fill(std::span<std::byte> destination) noexcept = 0;
};

struct InviteCode {
    std::array<std::byte, kInviteBytes> bytes{};

    [[nodiscard]] constexpr bool empty() const noexcept {
        for (const auto value : bytes) {
            if (value != std::byte{0}) {
                return false;
            }
        }
        return true;
    }

    friend constexpr bool operator==(const InviteCode&, const InviteCode&) = default;
};

struct Invite {
    InviteCode code;
    ServerId server_id;
    UserId creator_id;

    friend bool operator==(const Invite&, const Invite&) = default;
};

[[nodiscard]] std::string to_hex(UserId id);
[[nodiscard]] std::string to_hex(ServerId id);
[[nodiscard]] std::string to_hex(ChannelId id);

[[nodiscard]] std::optional<UserId> user_id_from_hex(std::string_view value) noexcept;
[[nodiscard]] std::optional<ServerId> server_id_from_hex(std::string_view value) noexcept;
[[nodiscard]] std::optional<ChannelId> channel_id_from_hex(std::string_view value) noexcept;

[[nodiscard]] std::string format_invite_code(InviteCode code);
[[nodiscard]] std::optional<InviteCode> parse_invite_code(std::string_view value) noexcept;

[[nodiscard]] std::optional<StateError> validate(const LocalState& state);
[[nodiscard]] std::variant<LocalState, StateError> bootstrap_personal_state(EntropySource& entropy);
[[nodiscard]] std::variant<Invite, StateError> create_invite(
    EntropySource& entropy, const PersonalServer& server, UserId creator_id);

[[nodiscard]] constexpr bool can_manage_server(ServerRole role) noexcept {
    return role == ServerRole::owner;
}

} // namespace catro::community
