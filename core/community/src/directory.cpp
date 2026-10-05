#include <catro/community/directory.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <cctype>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>

namespace catro::community {
namespace {

using Json = nlohmann::json;

[[nodiscard]] DirectoryError error(
    DirectoryErrorCode code,
    std::string message,
    std::int64_t native_code = 0,
    int http_status = 0) {
    return {code, std::move(message), native_code, http_status};
}

template <class Result>
[[nodiscard]] Result malformed(std::string message) {
    return error(DirectoryErrorCode::malformed_response, std::move(message));
}

[[nodiscard]] bool ascii_space(std::string_view value) noexcept {
    return std::ranges::any_of(value, [](unsigned char ch) { return std::isspace(ch) != 0; });
}

[[nodiscard]] std::string lower(std::string_view value) {
    std::string output(value);
    std::ranges::transform(output, output.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return output;
}

struct ParsedNetworkUrl {
    std::string scheme;
    std::string host;
};

[[nodiscard]] std::optional<ParsedNetworkUrl> parse_network_url(
    std::string_view value,
    bool allow_query) {
    if (value.empty() || value.size() > kMaxDirectoryUrlBytes || ascii_space(value)) {
        return std::nullopt;
    }
    const auto separator = value.find("://");
    if (separator == std::string_view::npos || separator == 0) {
        return std::nullopt;
    }
    ParsedNetworkUrl parsed;
    parsed.scheme = lower(value.substr(0, separator));
    auto remainder = value.substr(separator + 3);
    if (remainder.empty() || remainder.find('@') != std::string_view::npos ||
        remainder.find('#') != std::string_view::npos ||
        (!allow_query && remainder.find('?') != std::string_view::npos)) {
        return std::nullopt;
    }
    const auto authority_end = remainder.find_first_of("/?");
    const auto authority = remainder.substr(0, authority_end);
    if (authority.empty()) {
        return std::nullopt;
    }

    std::string_view host;
    std::string_view port;
    if (authority.front() == '[') {
        const auto close = authority.find(']');
        if (close == std::string_view::npos) {
            return std::nullopt;
        }
        host = authority.substr(1, close - 1);
        if (close + 1 < authority.size()) {
            if (authority[close + 1] != ':') {
                return std::nullopt;
            }
            port = authority.substr(close + 2);
        }
    } else {
        const auto colon = authority.rfind(':');
        if (colon != std::string_view::npos) {
            if (authority.find(':') != colon) {
                return std::nullopt;
            }
            host = authority.substr(0, colon);
            port = authority.substr(colon + 1);
        } else {
            host = authority;
        }
    }
    if (host.empty()) {
        return std::nullopt;
    }
    if (!port.empty()) {
        unsigned int number = 0;
        const auto [end, parse_error] =
            std::from_chars(port.data(), port.data() + port.size(), number);
        if (parse_error != std::errc{} || end != port.data() + port.size() || number == 0 ||
            number > 65535) {
            return std::nullopt;
        }
    } else if (!authority.empty() && authority.back() == ':') {
        return std::nullopt;
    }
    parsed.host = lower(host);
    return parsed;
}

[[nodiscard]] bool loopback_host(std::string_view host) noexcept {
    return host == "localhost" || host.ends_with(".localhost") || host == "::1" ||
           host == "0.0.0.0" || host.starts_with("127.");
}

[[nodiscard]] bool valid_role(std::string_view value) noexcept {
    return value == "owner" || value == "member";
}

[[nodiscard]] bool valid_join_status(std::string_view value) noexcept {
    return value == "pending" || value == "approved" || value == "rejected" ||
           value == "cancelled";
}

[[nodiscard]] std::optional<DirectoryServer> server_from_json(const Json& value) {
    try {
        DirectoryServer server;
        server.id = value.at("id").get<std::string>();
        server.owner_id = value.at("owner_id").get<std::string>();
        server.name = value.at("name").get<std::string>();
        server.public_code = value.value("public_code", std::string{});
        server.text_channel_id = value.value("text_channel_id", std::string{});
        server.voice_channel_id = value.at("voice_channel_id").get<std::string>();
        server.role = value.at("role").get<std::string>();
        server.member_count = value.at("member_count").get<std::size_t>();
        if (!valid_remote_directory_id(server.id) ||
            !valid_remote_directory_id(server.owner_id) || server.name.empty() ||
            server.name.size() > kMaxServerNameBytes ||
            (server.role == "owner" && !valid_directory_server_code(server.public_code)) ||
            (server.role != "owner" && !server.public_code.empty()) ||
            (!server.text_channel_id.empty() &&
             !valid_remote_directory_id(server.text_channel_id)) ||
            !valid_remote_directory_id(server.voice_channel_id) || !valid_role(server.role) ||
            server.member_count == 0 || server.member_count > kMaxMembers) {
            return std::nullopt;
        }
        return server;
    } catch (...) {
        return std::nullopt;
    }
}

[[nodiscard]] std::optional<DirectoryMember> member_from_json(const Json& value) {
    try {
        DirectoryMember member{
            value.at("user_id").get<std::string>(),
            value.at("display_name").get<std::string>(),
            value.at("role").get<std::string>(),
            value.value("voice_channel_id", std::string{}),
        };
        if (!valid_remote_directory_id(member.user_id) || member.display_name.empty() ||
            member.display_name.size() > kMaxDisplayNameBytes || !valid_role(member.role) ||
            (!member.voice_channel_id.empty() &&
             !valid_remote_directory_id(member.voice_channel_id))) {
            return std::nullopt;
        }
        return member;
    } catch (...) {
        return std::nullopt;
    }
}

[[nodiscard]] std::optional<DirectoryServerLookup> lookup_from_json(const Json& value) {
    try {
        DirectoryServerLookup lookup{
            value.at("public_code").get<std::string>(),
            value.at("name").get<std::string>(),
            value.at("member_count").get<std::size_t>(),
            value.at("relationship").get<std::string>(),
            value.value("request_id", std::string{}),
        };
        const bool relationship_ok = lookup.relationship == "none" ||
                                     lookup.relationship == "pending" ||
                                     lookup.relationship == "member" ||
                                     lookup.relationship == "owner";
        if (!valid_directory_server_code(lookup.public_code) || lookup.name.empty() ||
            lookup.name.size() > kMaxServerNameBytes || lookup.member_count == 0 ||
            lookup.member_count > kMaxMembers || !relationship_ok ||
            (lookup.relationship == "pending" &&
             !valid_remote_directory_id(lookup.request_id)) ||
            (lookup.relationship != "pending" && !lookup.request_id.empty())) {
            return std::nullopt;
        }
        return lookup;
    } catch (...) {
        return std::nullopt;
    }
}

[[nodiscard]] std::optional<DirectoryJoinRequest> join_request_from_json(const Json& value) {
    try {
        DirectoryJoinRequest request{
            value.at("id").get<std::string>(),
            value.at("server_name").get<std::string>(),
            value.at("public_code").get<std::string>(),
            value.value("requester_display_name", std::string{}),
            value.value("message", std::string{}),
            value.at("status").get<std::string>(),
            value.at("created_at").get<std::int64_t>(),
            value.at("updated_at").get<std::int64_t>(),
            value.at("expires_at").get<std::int64_t>(),
        };
        if (!valid_remote_directory_id(request.id) || request.server_name.empty() ||
            request.server_name.size() > kMaxServerNameBytes ||
            !valid_directory_server_code(request.public_code) ||
            (!request.requester_display_name.empty() &&
             request.requester_display_name.size() > kMaxDisplayNameBytes) ||
            request.message.size() > kMaxJoinRequestMessageBytes ||
            !valid_join_status(request.status) || request.created_at <= 0 ||
            request.updated_at < request.created_at || request.expires_at <= request.updated_at) {
            return std::nullopt;
        }
        return request;
    } catch (...) {
        return std::nullopt;
    }
}

[[nodiscard]] std::optional<DirectoryMessage> message_from_json(const Json& value) {
    try {
        DirectoryMessage message{
            value.at("id").get<std::string>(),
            value.at("sequence").get<std::uint64_t>(),
            value.at("server_id").get<std::string>(),
            value.at("channel_id").get<std::string>(),
            value.at("author_id").get<std::string>(),
            value.at("author_display_name").get<std::string>(),
            value.at("content").get<std::string>(),
            value.at("created_at").get<std::int64_t>(),
        };
        if (!valid_remote_directory_id(message.id) || message.sequence == 0 ||
            !valid_remote_directory_id(message.server_id) ||
            !valid_remote_directory_id(message.channel_id) ||
            !valid_remote_directory_id(message.author_id) || message.author_display_name.empty() ||
            message.author_display_name.size() > kMaxDisplayNameBytes || message.content.empty() ||
            message.content.size() > kMaxMessageContentBytes || message.created_at <= 0) {
            return std::nullopt;
        }
        return message;
    } catch (...) {
        return std::nullopt;
    }
}

[[nodiscard]] bool valid_signaling_url(
    std::string_view value,
    bool allow_insecure) {
    const auto parsed = parse_network_url(value, false);
    if (!parsed || (parsed->scheme != "wss" && !(allow_insecure && parsed->scheme == "ws"))) {
        return false;
    }
    return allow_insecure || !loopback_host(parsed->host);
}

[[nodiscard]] bool valid_ice_url(std::string_view value) {
    if (value.empty() || value.size() > kMaxDirectoryUrlBytes || ascii_space(value)) {
        return false;
    }
    const auto separator = value.find(':');
    if (separator == std::string_view::npos || separator + 1 >= value.size()) {
        return false;
    }
    const auto scheme = lower(value.substr(0, separator));
    return scheme == "stun" || scheme == "stuns" || scheme == "turn" || scheme == "turns";
}

} // namespace

bool valid_remote_directory_id(std::string_view value) noexcept {
    if (value.empty() || value.size() > 128) {
        return false;
    }
    return std::ranges::all_of(value, [](char ch) {
        const bool alpha = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
        const bool digit = ch >= '0' && ch <= '9';
        return alpha || digit || ch == '-' || ch == '_' || ch == ':';
    });
}

bool valid_directory_server_code(std::string_view value) noexcept {
    if (value.size() != 28 || value.substr(0, 4) != "CAT-") {
        return false;
    }
    for (std::size_t index = 4; index < value.size(); ++index) {
        if (index == 8 || index == 13 || index == 18 || index == 23) {
            if (value[index] != '-') {
                return false;
            }
            continue;
        }
        const auto ch = value[index];
        if (!((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'F'))) {
            return false;
        }
    }
    return true;
}

std::optional<DirectoryError>
validate_directory_service_config(const DirectoryServiceConfig& config) noexcept {
    const auto parsed = parse_network_url(config.api_base_url, false);
    if (!parsed || (parsed->scheme != "https" &&
                    !(config.allow_insecure_http && parsed->scheme == "http")) ||
        (!config.allow_insecure_http && loopback_host(parsed->host))) {
        return error(
            DirectoryErrorCode::invalid_config,
            "directory API requires a public HTTPS URL without credentials, query, or fragment");
    }
    return std::nullopt;
}

DirectoryError map_directory_http_error(int status, std::string_view body) noexcept {
    std::string message = "directory request rejected";
    try {
        const auto parsed = Json::parse(body);
        if (const auto found = parsed.find("error");
            found != parsed.end() && found->is_string() && !found->get_ref<const std::string&>().empty()) {
            message = found->get<std::string>();
        }
    } catch (...) {
    }
    return error(
        status == 401 ? DirectoryErrorCode::unauthorized : DirectoryErrorCode::rejected,
        std::move(message),
        0,
        status);
}

DirectoryStringResult parse_directory_access_token(std::string_view json) noexcept {
    try {
        const auto token = Json::parse(json).at("access_token").get<std::string>();
        if (token.empty() || token.size() > kMaxDirectoryTokenBytes) {
            return malformed<DirectoryStringResult>("directory access token is invalid");
        }
        return token;
    } catch (...) {
        return malformed<DirectoryStringResult>("directory registration response is invalid");
    }
}

DirectoryServerResult parse_directory_server(std::string_view json) noexcept {
    try {
        const auto parsed = server_from_json(Json::parse(json));
        return parsed ? DirectoryServerResult{*parsed}
                      : malformed<DirectoryServerResult>("directory server response is invalid");
    } catch (...) {
        return malformed<DirectoryServerResult>("directory server response is invalid");
    }
}

DirectoryServersResult parse_directory_servers(std::string_view json) noexcept {
    try {
        const auto value = Json::parse(json);
        const auto& items = value.at("servers");
        if (!items.is_array() || items.size() > kMaxDirectoryServers) {
            return malformed<DirectoryServersResult>("directory server list is invalid");
        }
        DirectoryServers servers;
        std::unordered_set<std::string> ids;
        servers.reserve(items.size());
        ids.reserve(items.size());
        for (const auto& item : items) {
            const auto parsed = server_from_json(item);
            if (!parsed || !ids.insert(parsed->id).second) {
                return malformed<DirectoryServersResult>(
                    "directory server list contains invalid data");
            }
            servers.push_back(*parsed);
        }
        return servers;
    } catch (...) {
        return malformed<DirectoryServersResult>("directory server list response is invalid");
    }
}

DirectoryInviteResult parse_directory_invite(std::string_view json) noexcept {
    try {
        const auto value = Json::parse(json);
        DirectoryInvite invite{
            value.at("code").get<std::string>(),
            value.at("expires").get<std::int64_t>(),
            {},
        };
        const auto server = server_from_json(value.at("server"));
        if (invite.code.empty() || invite.code.size() > 128 || invite.expires <= 0 || !server) {
            return malformed<DirectoryInviteResult>("directory invite response is invalid");
        }
        invite.server = *server;
        return invite;
    } catch (...) {
        return malformed<DirectoryInviteResult>("directory invite response is invalid");
    }
}

DirectoryMembersResult parse_directory_members(std::string_view json) noexcept {
    try {
        const auto value = Json::parse(json);
        const auto& items = value.at("members");
        if (!items.is_array() || items.empty() || items.size() > kMaxMembers) {
            return malformed<DirectoryMembersResult>("member roster is invalid");
        }
        DirectoryMembers members;
        std::unordered_set<std::string> ids;
        members.reserve(items.size());
        ids.reserve(items.size());
        std::size_t owners = 0;
        for (std::size_t index = 0; index < items.size(); ++index) {
            const auto parsed = member_from_json(items[index]);
            if (!parsed || !ids.insert(parsed->user_id).second) {
                return malformed<DirectoryMembersResult>("member roster contains invalid data");
            }
            if (parsed->role == "owner" && (++owners != 1 || index != 0)) {
                return malformed<DirectoryMembersResult>(
                    "member roster owner ordering is invalid");
            }
            members.push_back(*parsed);
        }
        if (owners != 1) {
            return malformed<DirectoryMembersResult>("member roster owner is invalid");
        }
        return members;
    } catch (...) {
        return malformed<DirectoryMembersResult>("member roster response is invalid");
    }
}

DirectoryServerLookupResult parse_directory_server_lookup(std::string_view json) noexcept {
    try {
        const auto parsed = lookup_from_json(Json::parse(json));
        return parsed ? DirectoryServerLookupResult{*parsed}
                      : malformed<DirectoryServerLookupResult>(
                            "server lookup response is invalid");
    } catch (...) {
        return malformed<DirectoryServerLookupResult>("server lookup response is invalid");
    }
}

DirectoryJoinRequestResult parse_directory_join_request(std::string_view json) noexcept {
    try {
        const auto parsed = join_request_from_json(Json::parse(json));
        return parsed ? DirectoryJoinRequestResult{*parsed}
                      : malformed<DirectoryJoinRequestResult>(
                            "join request response is invalid");
    } catch (...) {
        return malformed<DirectoryJoinRequestResult>("join request response is invalid");
    }
}

DirectoryJoinRequestsResult parse_directory_join_requests(std::string_view json) noexcept {
    try {
        const auto value = Json::parse(json);
        const auto& items = value.at("requests");
        if (!items.is_array() || items.size() > kMaxJoinRequestPage) {
            return malformed<DirectoryJoinRequestsResult>("join request page is invalid");
        }
        DirectoryJoinRequests requests;
        std::unordered_set<std::string> ids;
        requests.reserve(items.size());
        ids.reserve(items.size());
        for (const auto& item : items) {
            const auto parsed = join_request_from_json(item);
            if (!parsed || !ids.insert(parsed->id).second) {
                return malformed<DirectoryJoinRequestsResult>(
                    "join request page contains invalid data");
            }
            requests.push_back(*parsed);
        }
        return requests;
    } catch (...) {
        return malformed<DirectoryJoinRequestsResult>(
            "join request page response is invalid");
    }
}

DirectoryMessageResult parse_directory_message(std::string_view json) noexcept {
    try {
        const auto parsed = message_from_json(Json::parse(json));
        return parsed ? DirectoryMessageResult{*parsed}
                      : malformed<DirectoryMessageResult>("message response is invalid");
    } catch (...) {
        return malformed<DirectoryMessageResult>("message response is invalid");
    }
}

DirectoryMessagesResult parse_directory_messages(
    std::string_view json,
    std::string_view expected_server_id,
    std::string_view expected_channel_id,
    std::uint64_t after,
    std::size_t limit) noexcept {
    try {
        if (!valid_remote_directory_id(expected_server_id) ||
            !valid_remote_directory_id(expected_channel_id) || limit == 0 ||
            limit > kMaxMessagePage) {
            return error(DirectoryErrorCode::invalid_config, "message query is invalid");
        }
        const auto value = Json::parse(json);
        const auto& items = value.at("messages");
        if (!items.is_array() || items.size() > limit) {
            return malformed<DirectoryMessagesResult>("message page is invalid");
        }
        DirectoryMessagePage page;
        page.next_after = value.at("next_after").get<std::uint64_t>();
        page.messages.reserve(items.size());
        std::unordered_set<std::string> ids;
        ids.reserve(items.size());
        auto previous = after;
        for (const auto& item : items) {
            const auto parsed = message_from_json(item);
            if (!parsed || !ids.insert(parsed->id).second ||
                parsed->server_id != expected_server_id ||
                parsed->channel_id != expected_channel_id || parsed->sequence <= previous) {
                return malformed<DirectoryMessagesResult>(
                    "message page contains invalid data");
            }
            previous = parsed->sequence;
            page.messages.push_back(*parsed);
        }
        if ((!page.messages.empty() && page.next_after != page.messages.back().sequence) ||
            (page.messages.empty() && page.next_after != after)) {
            return malformed<DirectoryMessagesResult>("message cursor is inconsistent");
        }
        return page;
    } catch (...) {
        return malformed<DirectoryMessagesResult>("message page response is invalid");
    }
}

RtcProvisioningResult parse_rtc_provisioning(std::string_view json) noexcept {
    try {
        const auto value = Json::parse(json);
        RtcProvisioning rtc{
            value.at("token").get<std::string>(),
            value.at("expires").get<std::int64_t>(),
            value.at("server_id").get<std::string>(),
            value.at("channel_id").get<std::string>(),
            value.at("peer_id").get<std::string>(),
            value.at("signaling_url").get<std::string>(),
            value.at("ice_servers").get<std::vector<std::string>>(),
            value.at("max_room_peers").get<std::size_t>(),
            value.value("allow_insecure_signaling", false),
            value.value("allow_no_turn", false),
        };
        const bool ice_ok = !rtc.ice_servers.empty() &&
                            rtc.ice_servers.size() <= kMaxIceServers &&
                            std::ranges::all_of(rtc.ice_servers, valid_ice_url);
        if (rtc.token.empty() || rtc.token.size() > kMaxDirectoryTokenBytes || rtc.expires <= 0 ||
            !valid_remote_directory_id(rtc.server_id) ||
            !valid_remote_directory_id(rtc.channel_id) ||
            !valid_remote_directory_id(rtc.peer_id) ||
            !valid_signaling_url(rtc.signaling_url, rtc.allow_insecure_signaling) || !ice_ok ||
            rtc.max_room_peers < 2 || rtc.max_room_peers > 5) {
            return malformed<RtcProvisioningResult>("RTC provisioning response is incomplete");
        }
        return rtc;
    } catch (...) {
        return malformed<RtcProvisioningResult>("RTC provisioning response is invalid");
    }
}

} // namespace catro::community
