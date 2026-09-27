#include <catro/community/model.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <limits>
#include <set>
#include <utility>

namespace catro::community {
namespace {

constexpr char kHex[] = "0123456789abcdef";
constexpr std::string_view kCrockford = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

template <class IdType>
std::string id_to_hex(IdType id) {
    std::string output;
    output.resize(kIdBytes * 2);
    for (std::size_t index = 0; index < id.bytes.size(); ++index) {
        const auto value = std::to_integer<unsigned int>(id.bytes[index]);
        output[index * 2] = kHex[(value >> 4U) & 0x0fU];
        output[index * 2 + 1] = kHex[value & 0x0fU];
    }
    return output;
}

int hex_value(char value) noexcept {
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

template <class IdType>
std::optional<IdType> id_from_hex(std::string_view value) noexcept {
    if (value.size() != kIdBytes * 2) {
        return std::nullopt;
    }

    IdType id;
    for (std::size_t index = 0; index < kIdBytes; ++index) {
        const auto high = hex_value(value[index * 2]);
        const auto low = hex_value(value[index * 2 + 1]);
        if (high < 0 || low < 0) {
            return std::nullopt;
        }
        id.bytes[index] = static_cast<std::byte>((high << 4) | low);
    }
    return id.empty() ? std::nullopt : std::optional{id};
}

template <class IdType>
std::optional<IdType> generate_id(EntropySource& entropy) noexcept {
    for (int attempt = 0; attempt < 4; ++attempt) {
        IdType id;
        if (!entropy.fill(id.bytes)) {
            return std::nullopt;
        }
        if (!id.empty()) {
            return id;
        }
    }
    return std::nullopt;
}

bool valid_name(std::string_view value, std::size_t maximum) noexcept {
    return !value.empty() && value.size() <= maximum &&
           value.find('\0') == std::string_view::npos;
}

template <class IdType>
std::string id_key(const IdType& id) {
    return id_to_hex(id);
}

int crockford_value(char value) noexcept {
    const auto upper = static_cast<char>(std::toupper(static_cast<unsigned char>(value)));
    const auto found = kCrockford.find(upper);
    return found == std::string_view::npos ? -1 : static_cast<int>(found);
}

} // namespace

std::string to_hex(UserId id) {
    return id_to_hex(id);
}

std::string to_hex(ServerId id) {
    return id_to_hex(id);
}

std::string to_hex(ChannelId id) {
    return id_to_hex(id);
}

std::optional<UserId> user_id_from_hex(std::string_view value) noexcept {
    return id_from_hex<UserId>(value);
}

std::optional<ServerId> server_id_from_hex(std::string_view value) noexcept {
    return id_from_hex<ServerId>(value);
}

std::optional<ChannelId> channel_id_from_hex(std::string_view value) noexcept {
    return id_from_hex<ChannelId>(value);
}

std::string format_invite_code(InviteCode code) {
    // 128 bits become 26 Crockford Base32 digits. The first digit contains only three significant
    // bits, leaving the representation canonical and copy/paste friendly.
    std::string output;
    output.reserve(26);
    std::uint32_t accumulator = 0;
    int bits = 2; // two zero high bits make a 130-bit stream

    for (const auto byte : code.bytes) {
        accumulator = (accumulator << 8U) | std::to_integer<std::uint8_t>(byte);
        bits += 8;
        while (bits >= 5) {
            bits -= 5;
            output.push_back(kCrockford[(accumulator >> bits) & 0x1fU]);
            if (bits == 0) {
                accumulator = 0;
            } else {
                accumulator &= (1U << bits) - 1U;
            }
        }
    }
    return output;
}

std::optional<InviteCode> parse_invite_code(std::string_view value) noexcept {
    if (value.size() != 26) {
        return std::nullopt;
    }

    std::array<std::byte, kInviteBytes> bytes{};
    std::uint32_t accumulator = 0;
    int bits = 0;
    std::size_t output = 0;

    for (std::size_t index = 0; index < value.size(); ++index) {
        const auto digit = crockford_value(value[index]);
        if (digit < 0 || (index == 0 && digit > 7)) {
            return std::nullopt;
        }

        // The first Crockford digit carries only three data bits; the two canonical high bits are
        // implicit zeros and must not be fed into the decoded byte stream.
        const auto digit_bits = index == 0 ? 3 : 5;
        accumulator = (accumulator << digit_bits) | static_cast<std::uint32_t>(digit);
        bits += digit_bits;
        if (bits >= 8) {
            bits -= 8;
            if (output >= bytes.size()) {
                return std::nullopt;
            }
            bytes[output++] = static_cast<std::byte>((accumulator >> bits) & 0xffU);
            accumulator = bits == 0 ? 0U : accumulator & ((1U << bits) - 1U);
        }
    }

    if (output != bytes.size() || bits != 0 || accumulator != 0) {
        return std::nullopt;
    }
    InviteCode code{bytes};
    return code.empty() ? std::nullopt : std::optional{code};
}

std::optional<StateError> validate(const LocalState& state) {
    if (state.identity.id.empty() || state.personal_server.id.empty() ||
        state.personal_server.owner_id.empty()) {
        return StateError{StateErrorCode::invalid_identifier, "zero identifiers are not valid"};
    }
    if (!valid_name(state.identity.display_name, kMaxDisplayNameBytes) ||
        !valid_name(state.personal_server.name, kMaxServerNameBytes)) {
        return StateError{StateErrorCode::invalid_name, "identity or server name is empty or too long"};
    }
    if (state.personal_server.owner_id != state.identity.id) {
        return StateError{StateErrorCode::invalid_owner, "personal server owner must be the local identity"};
    }
    if (state.personal_server.channels.size() > kMaxChannels) {
        return StateError{StateErrorCode::too_many_channels, "channel count exceeds the local bound"};
    }
    if (state.personal_server.members.size() > kMaxMembers) {
        return StateError{StateErrorCode::too_many_members, "member count exceeds the local bound"};
    }

    std::set<std::string> channel_ids;
    std::size_t text_channels = 0;
    std::size_t voice_channels = 0;
    for (const auto& channel : state.personal_server.channels) {
        if (channel.id.empty()) {
            return StateError{StateErrorCode::invalid_identifier, "channel id is zero"};
        }
        if (!valid_name(channel.name, kMaxChannelNameBytes)) {
            return StateError{StateErrorCode::invalid_name, "channel name is empty or too long"};
        }
        if (!channel_ids.insert(id_key(channel.id)).second) {
            return StateError{StateErrorCode::duplicate_identifier, "duplicate channel id"};
        }
        text_channels += channel.kind == ChannelKind::text ? 1U : 0U;
        voice_channels += channel.kind == ChannelKind::voice ? 1U : 0U;
    }
    if (text_channels == 0 || voice_channels == 0) {
        return StateError{StateErrorCode::invalid_channel_layout,
                          "server must retain at least one text and one voice channel"};
    }

    std::set<std::string> member_ids;
    std::size_t owners = 0;
    bool local_member = false;
    for (const auto& member : state.personal_server.members) {
        if (member.user_id.empty()) {
            return StateError{StateErrorCode::invalid_identifier, "member id is zero"};
        }
        if (!member_ids.insert(id_key(member.user_id)).second) {
            return StateError{StateErrorCode::duplicate_identifier, "duplicate member id"};
        }
        owners += member.role == ServerRole::owner ? 1U : 0U;
        local_member = local_member || member.user_id == state.identity.id;
        if (member.role == ServerRole::owner && member.user_id != state.personal_server.owner_id) {
            return StateError{StateErrorCode::invalid_owner, "only the server owner may have owner role"};
        }
    }
    if (owners != 1 || !local_member) {
        return StateError{StateErrorCode::invalid_owner,
                          "personal server must contain exactly one owner and the local member"};
    }
    return std::nullopt;
}

std::variant<LocalState, StateError> bootstrap_personal_state(EntropySource& entropy) {
    const auto user_id = generate_id<UserId>(entropy);
    const auto server_id = generate_id<ServerId>(entropy);
    const auto text_id = generate_id<ChannelId>(entropy);
    const auto voice_id = generate_id<ChannelId>(entropy);
    if (!user_id || !server_id || !text_id || !voice_id) {
        return StateError{StateErrorCode::entropy_failure, "secure entropy was unavailable"};
    }

    LocalState state{
        .identity = Identity{.id = *user_id, .display_name = "You"},
        .personal_server =
            PersonalServer{
                .id = *server_id,
                .owner_id = *user_id,
                .name = "My Server",
                .channels =
                    {
                        Channel{.id = *text_id, .name = "general", .kind = ChannelKind::text},
                        Channel{.id = *voice_id, .name = "Voice", .kind = ChannelKind::voice},
                    },
                .members = {Member{.user_id = *user_id, .role = ServerRole::owner}},
            },
    };

    if (const auto error = validate(state)) {
        return *error;
    }
    return state;
}

std::variant<Invite, StateError> create_invite(
    EntropySource& entropy, ServerId server_id, UserId creator_id) {
    if (server_id.empty() || creator_id.empty()) {
        return StateError{StateErrorCode::invalid_identifier, "invite requires valid server and creator ids"};
    }

    for (int attempt = 0; attempt < 4; ++attempt) {
        InviteCode code;
        if (!entropy.fill(code.bytes)) {
            return StateError{StateErrorCode::entropy_failure, "secure entropy was unavailable"};
        }
        if (!code.empty()) {
            return Invite{.code = code, .server_id = server_id, .creator_id = creator_id};
        }
    }
    return StateError{StateErrorCode::entropy_failure, "secure entropy produced only invalid invite codes"};
}

} // namespace catro::community
