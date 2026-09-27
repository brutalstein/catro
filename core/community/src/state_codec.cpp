#include <catro/community/state_codec.hpp>

#include <nlohmann/json.hpp>

#include <exception>
#include <string>
#include <utility>

namespace catro::community {
namespace {

using json = nlohmann::json;

std::string_view kind_name(ChannelKind kind) noexcept {
    return kind == ChannelKind::voice ? "voice" : "text";
}

std::string_view role_name(ServerRole role) noexcept {
    return role == ServerRole::owner ? "owner" : "member";
}

std::optional<ChannelKind> parse_kind(std::string_view value) noexcept {
    if (value == "text") {
        return ChannelKind::text;
    }
    if (value == "voice") {
        return ChannelKind::voice;
    }
    return std::nullopt;
}

std::optional<ServerRole> parse_role(std::string_view value) noexcept {
    if (value == "owner") {
        return ServerRole::owner;
    }
    if (value == "member") {
        return ServerRole::member;
    }
    return std::nullopt;
}

CodecError field_error(std::string detail) {
    return {CodecErrorCode::invalid_field, std::move(detail)};
}

} // namespace

std::variant<std::string, CodecError> encode_local_state(const LocalState& state) {
    if (const auto error = validate(state)) {
        return CodecError{CodecErrorCode::invalid_state, error->detail};
    }

    json root;
    root["schema"] = {
        {"id", kLocalStateSchema},
        {"major", kLocalStateMajor},
        {"minor", kLocalStateMinor},
    };
    root["identity"] = {
        {"id", to_hex(state.identity.id)},
        {"display_name", state.identity.display_name},
    };

    auto& server = root["personal_server"];
    server["id"] = to_hex(state.personal_server.id);
    server["owner_id"] = to_hex(state.personal_server.owner_id);
    server["name"] = state.personal_server.name;
    server["channels"] = json::array();
    for (const auto& channel : state.personal_server.channels) {
        server["channels"].push_back({
            {"id", to_hex(channel.id)},
            {"name", channel.name},
            {"kind", kind_name(channel.kind)},
        });
    }
    server["members"] = json::array();
    for (const auto& member : state.personal_server.members) {
        server["members"].push_back({
            {"user_id", to_hex(member.user_id)},
            {"role", role_name(member.role)},
        });
    }

    auto encoded = root.dump(2);
    encoded.push_back('\n');
    return encoded;
}

std::variant<LocalState, CodecError> decode_local_state(std::string_view payload) {
    if (payload.size() > kMaxStateBytes) {
        return CodecError{CodecErrorCode::too_large, "local state exceeds the 64 KiB bound"};
    }

    try {
        const auto root = json::parse(payload);
        if (!root.is_object() || !root.contains("schema") || !root["schema"].is_object()) {
            return field_error("missing schema");
        }
        const auto& schema = root["schema"];
        if (schema.value("id", std::string{}) != kLocalStateSchema) {
            return CodecError{CodecErrorCode::wrong_schema, "unexpected local-state schema id"};
        }
        if (!schema.contains("major") || !schema["major"].is_number_unsigned() ||
            !schema.contains("minor") || !schema["minor"].is_number_unsigned()) {
            return field_error("schema version is missing or invalid");
        }
        if (schema["major"].get<std::uint32_t>() != kLocalStateMajor) {
            return CodecError{CodecErrorCode::unsupported_version, "unsupported local-state major version"};
        }

        if (!root.contains("identity") || !root["identity"].is_object() ||
            !root.contains("personal_server") || !root["personal_server"].is_object()) {
            return field_error("identity or personal_server is missing");
        }

        const auto& identity_json = root["identity"];
        if (!identity_json.contains("id") || !identity_json["id"].is_string() ||
            !identity_json.contains("display_name") || !identity_json["display_name"].is_string()) {
            return field_error("identity fields are invalid");
        }
        const auto user_id = user_id_from_hex(identity_json["id"].get<std::string>());
        if (!user_id) {
            return field_error("identity id is invalid");
        }

        const auto& server_json = root["personal_server"];
        if (!server_json.contains("id") || !server_json["id"].is_string() ||
            !server_json.contains("owner_id") || !server_json["owner_id"].is_string() ||
            !server_json.contains("name") || !server_json["name"].is_string() ||
            !server_json.contains("channels") || !server_json["channels"].is_array() ||
            !server_json.contains("members") || !server_json["members"].is_array()) {
            return field_error("personal server fields are invalid");
        }
        const auto server_id = server_id_from_hex(server_json["id"].get<std::string>());
        const auto owner_id = user_id_from_hex(server_json["owner_id"].get<std::string>());
        if (!server_id || !owner_id) {
            return field_error("personal server identifiers are invalid");
        }
        if (server_json["channels"].size() > kMaxChannels ||
            server_json["members"].size() > kMaxMembers) {
            return field_error("personal server collection exceeds local bounds");
        }

        LocalState state{
            .identity =
                Identity{
                    .id = *user_id,
                    .display_name = identity_json["display_name"].get<std::string>(),
                },
            .personal_server =
                PersonalServer{
                    .id = *server_id,
                    .owner_id = *owner_id,
                    .name = server_json["name"].get<std::string>(),
                },
        };

        for (const auto& entry : server_json["channels"]) {
            if (!entry.is_object() || !entry.contains("id") || !entry["id"].is_string() ||
                !entry.contains("name") || !entry["name"].is_string() ||
                !entry.contains("kind") || !entry["kind"].is_string()) {
                return field_error("channel entry is invalid");
            }
            const auto id = channel_id_from_hex(entry["id"].get<std::string>());
            const auto kind = parse_kind(entry["kind"].get<std::string>());
            if (!id || !kind) {
                return field_error("channel id or kind is invalid");
            }
            state.personal_server.channels.push_back(
                Channel{.id = *id, .name = entry["name"].get<std::string>(), .kind = *kind});
        }

        for (const auto& entry : server_json["members"]) {
            if (!entry.is_object() || !entry.contains("user_id") || !entry["user_id"].is_string() ||
                !entry.contains("role") || !entry["role"].is_string()) {
                return field_error("member entry is invalid");
            }
            const auto id = user_id_from_hex(entry["user_id"].get<std::string>());
            const auto role = parse_role(entry["role"].get<std::string>());
            if (!id || !role) {
                return field_error("member id or role is invalid");
            }
            state.personal_server.members.push_back(Member{.user_id = *id, .role = *role});
        }

        if (const auto error = validate(state)) {
            return CodecError{CodecErrorCode::invalid_state, error->detail};
        }
        return state;
    } catch (const std::exception&) {
        return CodecError{CodecErrorCode::invalid_json, "local state is not valid bounded JSON"};
    }
}

} // namespace catro::community
