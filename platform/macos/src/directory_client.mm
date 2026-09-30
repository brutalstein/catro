#include <catro/platform/macos/directory_client.hpp>

#include <nlohmann/json.hpp>

#import <Foundation/Foundation.h>
#import <Security/Security.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

@interface CatroDirectorySessionDelegate : NSObject <NSURLSessionTaskDelegate>
@end

@implementation CatroDirectorySessionDelegate

- (void)URLSession:(NSURLSession*)session
              task:(NSURLSessionTask*)task
willPerformHTTPRedirection:(NSHTTPURLResponse*)response
        newRequest:(NSURLRequest*)request
 completionHandler:(void (^)(NSURLRequest* _Nullable))completionHandler {
    (void)session;
    (void)task;
    (void)response;
    (void)request;
    completionHandler(nil);
}

@end

namespace catro::platform::macos {
namespace {

using Json = nlohmann::json;
using namespace std::chrono_literals;

constexpr std::size_t kCredentialBytes = 32;
constexpr std::size_t kMaxConfigBytes = 16U * 1024U;
constexpr NSTimeInterval kRequestTimeoutSeconds = 10.0;

[[nodiscard]] community::DirectoryError error(
    community::DirectoryErrorCode code,
    std::string message,
    std::int64_t native_code = 0,
    int http_status = 0) {
    return {code, std::move(message), native_code, http_status};
}

[[nodiscard]] NSString* ns_string(std::string_view value) {
    return [[NSString alloc]
        initWithBytes:value.data()
               length:value.size()
             encoding:NSUTF8StringEncoding];
}

[[nodiscard]] std::string utf8(NSString* value) {
    if (value == nil) {
        return {};
    }
    const char* bytes = value.UTF8String;
    return bytes == nullptr ? std::string{} : std::string{bytes};
}

[[nodiscard]] std::string base64url(std::span<const std::byte> bytes) {
    static constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789-_";
    std::string output;
    output.reserve((bytes.size() * 4U + 2U) / 3U);
    std::uint32_t accumulator = 0;
    int bits = 0;
    for (const auto byte : bytes) {
        accumulator =
            (accumulator << 8U) |
            std::to_integer<std::uint32_t>(byte);
        bits += 8;
        while (bits >= 6) {
            bits -= 6;
            output.push_back(
                alphabet[(accumulator >> bits) & 0x3fU]);
        }
    }
    if (bits > 0) {
        output.push_back(
            alphabet[(accumulator << (6 - bits)) & 0x3fU]);
    }
    return output;
}

[[nodiscard]] std::optional<community::Channel> channel_of_kind(
    const community::PersonalServer& server,
    community::ChannelKind kind) {
    for (const auto& channel : server.channels) {
        if (channel.kind == kind) {
            return channel;
        }
    }
    return std::nullopt;
}

[[nodiscard]] bool valid_token(std::string_view token) noexcept {
    return !token.empty() &&
           token.size() <= community::kMaxDirectoryTokenBytes &&
           token.find_first_of("\r\n") == std::string_view::npos;
}

[[nodiscard]] DirectoryHttpResult perform(
    const community::DirectoryServiceConfig& service,
    DirectoryHttpTransport* transport,
    DirectoryHttpRequest request,
    std::stop_token stop) {
    if ([NSThread isMainThread]) {
        return error(
            community::DirectoryErrorCode::wrong_thread,
            "directory operations must run on a background thread");
    }
    if (stop.stop_requested()) {
        return error(
            community::DirectoryErrorCode::cancelled,
            "directory request cancelled");
    }
    if (transport == nullptr) {
        return error(
            community::DirectoryErrorCode::network_failure,
            "directory transport is unavailable");
    }
    if (const auto failure =
            community::validate_directory_service_config(
                service)) {
        return *failure;
    }
    if (!request.access_token.empty() &&
        !valid_token(request.access_token)) {
        return error(
            community::DirectoryErrorCode::invalid_config,
            "directory access token is invalid");
    }
    if (request.body.size() >
        community::kMaxDirectoryResponseBytes) {
        return error(
            community::DirectoryErrorCode::invalid_config,
            "directory request is too large");
    }
    auto result = transport->request(request, stop);
    if (const auto* failure =
            std::get_if<community::DirectoryError>(
                &result)) {
        return *failure;
    }
    auto response =
        std::get<DirectoryHttpResponse>(
            std::move(result));
    if (response.body.size() >
        community::kMaxDirectoryResponseBytes) {
        return error(
            community::DirectoryErrorCode::malformed_response,
            "directory response exceeds size bound",
            0,
            response.status);
    }
    if (response.status < 200 ||
        response.status >= 300) {
        return community::map_directory_http_error(
            response.status, response.body);
    }
    return response;
}

template <class Result>
[[nodiscard]] Result decode(
    DirectoryHttpResult response,
    Result (*parser)(std::string_view) noexcept) {
    if (const auto* failure =
            std::get_if<community::DirectoryError>(
                &response)) {
        return *failure;
    }
    return parser(
        std::get<DirectoryHttpResponse>(
            response)
            .body);
}

[[nodiscard]] std::string encoded(const Json& value) {
    return value.dump();
}

class FoundationDirectoryHttpTransport final
    : public DirectoryHttpTransport {
public:
    explicit FoundationDirectoryHttpTransport(
        community::DirectoryServiceConfig service)
        : base_url_(std::move(service.api_base_url)) {
        while (base_url_.size() > 1 &&
               base_url_.back() == '/') {
            base_url_.pop_back();
        }
    }

    DirectoryHttpResult request(
        const DirectoryHttpRequest& request,
        std::stop_token stop) noexcept override {
        @autoreleasepool {
            if ([NSThread isMainThread]) {
                return error(
                    community::DirectoryErrorCode::wrong_thread,
                    "directory networking must run off the main thread");
            }
            if (stop.stop_requested()) {
                return error(
                    community::DirectoryErrorCode::cancelled,
                    "directory request cancelled");
            }
            NSString* url_string =
                ns_string(base_url_ + request.endpoint);
            NSURL* url =
                url_string == nil
                    ? nil
                    : [NSURL URLWithString:url_string];
            if (url == nil) {
                return error(
                    community::DirectoryErrorCode::invalid_config,
                    "directory request URL is invalid");
            }

            NSMutableURLRequest* native =
                [NSMutableURLRequest requestWithURL:url];
            native.HTTPMethod = ns_string(request.method);
            native.timeoutInterval = kRequestTimeoutSeconds;
            [native setValue:@"application/json"
                forHTTPHeaderField:@"Accept"];
            if (!request.access_token.empty()) {
                NSString* authorization =
                    [@"Bearer "
                        stringByAppendingString:
                            ns_string(
                                request.access_token)];
                [native setValue:authorization
                    forHTTPHeaderField:@"Authorization"];
            }
            if (!request.body.empty()) {
                [native setValue:@"application/json"
                    forHTTPHeaderField:@"Content-Type"];
                native.HTTPBody =
                    [NSData
                        dataWithBytes:
                            request.body.data()
                               length:
                            request.body.size()];
            }

            NSURLSessionConfiguration* configuration =
                [NSURLSessionConfiguration
                    ephemeralSessionConfiguration];
            configuration.timeoutIntervalForRequest =
                kRequestTimeoutSeconds;
            configuration.timeoutIntervalForResource =
                kRequestTimeoutSeconds;
            configuration.URLCache = nil;
            configuration.requestCachePolicy =
                NSURLRequestReloadIgnoringLocalCacheData;

            CatroDirectorySessionDelegate* delegate =
                [[CatroDirectorySessionDelegate alloc] init];
            NSOperationQueue* queue =
                [[NSOperationQueue alloc] init];
            queue.maxConcurrentOperationCount = 1;
            NSURLSession* session =
                [NSURLSession
                    sessionWithConfiguration:
                        configuration
                    delegate:delegate
                    delegateQueue:queue];

            dispatch_semaphore_t completed =
                dispatch_semaphore_create(0);
            __block NSData* response_data = nil;
            __block NSURLResponse* response_value = nil;
            __block NSError* response_error = nil;
            NSURLSessionDataTask* task =
                [session
                    dataTaskWithRequest:native
                    completionHandler:^(
                        NSData* data,
                        NSURLResponse* response,
                        NSError* request_error) {
                        response_data = data;
                        response_value = response;
                        response_error = request_error;
                        dispatch_semaphore_signal(
                            completed);
                    }];
            std::stop_callback cancel{
                stop, [task] { [task cancel]; }};
            [task resume];

            const auto wait =
                dispatch_semaphore_wait(
                    completed,
                    dispatch_time(
                        DISPATCH_TIME_NOW,
                        static_cast<std::int64_t>(
                            (kRequestTimeoutSeconds +
                             1.0) *
                            NSEC_PER_SEC)));
            if (wait != 0) {
                [task cancel];
                [session invalidateAndCancel];
                return error(
                    community::DirectoryErrorCode::network_failure,
                    "directory request timed out");
            }
            [session finishTasksAndInvalidate];

            if (response_error != nil) {
                const auto cancelled =
                    response_error.code ==
                    NSURLErrorCancelled;
                return error(
                    cancelled
                        ? community::DirectoryErrorCode::
                              cancelled
                        : community::DirectoryErrorCode::
                              network_failure,
                    cancelled
                        ? "directory request cancelled"
                        : utf8(
                              response_error
                                  .localizedDescription),
                    response_error.code);
            }
            NSHTTPURLResponse* http =
                [response_value
                    isKindOfClass:
                        [NSHTTPURLResponse class]]
                    ? static_cast<
                          NSHTTPURLResponse*>(
                          response_value)
                    : nil;
            if (http == nil) {
                return error(
                    community::DirectoryErrorCode::network_failure,
                    "directory HTTP response is unavailable");
            }
            if (response_data.length >
                community::
                    kMaxDirectoryResponseBytes) {
                return error(
                    community::DirectoryErrorCode::
                        malformed_response,
                    "directory response exceeds size bound",
                    0,
                    static_cast<int>(
                        http.statusCode));
            }
            std::string body;
            if (response_data.length != 0) {
                body.assign(
                    static_cast<const char*>(
                        response_data.bytes),
                    response_data.length);
            }
            return DirectoryHttpResponse{
                static_cast<int>(
                    http.statusCode),
                std::move(body)};
        }
    }

private:
    std::string base_url_;
};

} // namespace

DirectoryClient::DirectoryClient(
    community::DirectoryServiceConfig service,
    DirectoryHttpTransport& transport) noexcept
    : service_(std::move(service)),
      transport_(&transport) {}

community::DirectoryStringResult
DirectoryClient::register_identity(
    const community::Identity& identity,
    std::string_view credential,
    std::stop_token stop) noexcept {
    try {
        if (identity.id.empty() ||
            identity.display_name.empty() ||
            identity.display_name.size() >
                community::kMaxDisplayNameBytes ||
            !valid_token(credential)) {
            return error(
                community::DirectoryErrorCode::invalid_config,
                "directory identity is invalid");
        }
        const Json body{
            {"user_id",
             community::to_hex(identity.id)},
            {"display_name",
             identity.display_name},
            {"credential",
             std::string{credential}},
        };
        return decode(
            perform(
                service_,
                transport_,
                {"POST",
                 "/v1/users/register",
                 {},
                 encoded(body)},
                stop),
            community::parse_directory_access_token);
    } catch (...) {
        return error(
            community::DirectoryErrorCode::invalid_config,
            "directory registration request is invalid");
    }
}

community::DirectoryServerResult
DirectoryClient::sync_personal_server(
    std::string_view access_token,
    const community::PersonalServer& server,
    std::stop_token stop) noexcept {
    try {
        const auto text =
            channel_of_kind(
                server,
                community::ChannelKind::text);
        const auto voice =
            channel_of_kind(
                server,
                community::ChannelKind::voice);
        if (!text || !voice) {
            return error(
                community::DirectoryErrorCode::invalid_config,
                "personal server is missing a required channel");
        }
        const Json body{
            {"server_id",
             community::to_hex(server.id)},
            {"name", server.name},
            {"text_channel_id",
             community::to_hex(text->id)},
            {"voice_channel_id",
             community::to_hex(voice->id)},
        };
        return decode(
            perform(
                service_,
                transport_,
                {"POST",
                 "/v1/servers/sync",
                 std::string{access_token},
                 encoded(body)},
                stop),
            community::parse_directory_server);
    } catch (...) {
        return error(
            community::DirectoryErrorCode::invalid_config,
            "directory server sync request is invalid");
    }
}

community::DirectoryServersResult
DirectoryClient::list_servers(
    std::string_view access_token,
    std::stop_token stop) noexcept {
    return decode(
        perform(
            service_,
            transport_,
            {"GET",
             "/v1/servers",
             std::string{access_token},
             {}},
            stop),
        community::parse_directory_servers);
}

community::DirectoryInviteResult
DirectoryClient::create_invite(
    std::string_view access_token,
    std::string_view server_id,
    std::stop_token stop) noexcept {
    try {
        if (!community::valid_remote_directory_id(
                server_id)) {
            return error(
                community::DirectoryErrorCode::invalid_config,
                "invite server is invalid");
        }
        return decode(
            perform(
                service_,
                transport_,
                {"POST",
                 "/v1/invites",
                 std::string{access_token},
                 encoded(
                     Json{{"server_id",
                           std::string{
                               server_id}}})},
                stop),
            community::parse_directory_invite);
    } catch (...) {
        return error(
            community::DirectoryErrorCode::invalid_config,
            "invite request is invalid");
    }
}

community::DirectoryServerResult
DirectoryClient::accept_invite(
    std::string_view access_token,
    std::string_view invite_code,
    std::stop_token stop) noexcept {
    try {
        if (invite_code.empty() ||
            invite_code.size() > 128) {
            return error(
                community::DirectoryErrorCode::invalid_config,
                "invite code is invalid");
        }
        return decode(
            perform(
                service_,
                transport_,
                {"POST",
                 "/v1/invites/accept",
                 std::string{access_token},
                 encoded(
                     Json{{"code",
                           std::string{
                               invite_code}}})},
                stop),
            community::parse_directory_server);
    } catch (...) {
        return error(
            community::DirectoryErrorCode::invalid_config,
            "invite acceptance request is invalid");
    }
}

community::DirectoryMembersResult
DirectoryClient::list_members(
    std::string_view access_token,
    std::string_view server_id,
    std::stop_token stop) noexcept {
    if (!community::valid_remote_directory_id(
            server_id)) {
        return error(
            community::DirectoryErrorCode::invalid_config,
            "member roster server is invalid");
    }
    return decode(
        perform(
            service_,
            transport_,
            {"GET",
             "/v1/members?server_id=" +
                 std::string{server_id},
             std::string{access_token},
             {}},
            stop),
        community::parse_directory_members);
}

community::DirectoryServerLookupResult
DirectoryClient::lookup_server(
    std::string_view access_token,
    std::string_view server_code,
    std::stop_token stop) noexcept {
    if (!community::valid_directory_server_code(
            server_code)) {
        return error(
            community::DirectoryErrorCode::invalid_config,
            "Server Code is invalid");
    }
    return decode(
        perform(
            service_,
            transport_,
            {"GET",
             "/v1/server-lookup?code=" +
                 std::string{server_code},
             std::string{access_token},
             {}},
            stop),
        community::parse_directory_server_lookup);
}

community::DirectoryJoinRequestResult
DirectoryClient::create_join_request(
    std::string_view access_token,
    std::string_view server_code,
    std::string_view message,
    std::stop_token stop) noexcept {
    try {
        if (!community::valid_directory_server_code(
                server_code) ||
            message.size() >
                community::
                    kMaxJoinRequestMessageBytes) {
            return error(
                community::DirectoryErrorCode::invalid_config,
                "join request is invalid");
        }
        return decode(
            perform(
                service_,
                transport_,
                {"POST",
                 "/v1/join-requests",
                 std::string{access_token},
                 encoded(
                     Json{
                         {"server_code",
                          std::string{
                              server_code}},
                         {"message",
                          std::string{
                              message}},
                     })},
                stop),
            community::parse_directory_join_request);
    } catch (...) {
        return error(
            community::DirectoryErrorCode::invalid_config,
            "join request is invalid");
    }
}

community::DirectoryJoinRequestsResult
DirectoryClient::list_outgoing_join_requests(
    std::string_view access_token,
    std::stop_token stop) noexcept {
    return decode(
        perform(
            service_,
            transport_,
            {"GET",
             "/v1/join-requests?mine=1",
             std::string{access_token},
             {}},
            stop),
        community::parse_directory_join_requests);
}

community::DirectoryJoinRequestsResult
DirectoryClient::list_pending_join_requests(
    std::string_view access_token,
    std::string_view server_id,
    std::stop_token stop) noexcept {
    if (!community::valid_remote_directory_id(
            server_id)) {
        return error(
            community::DirectoryErrorCode::invalid_config,
            "join request server is invalid");
    }
    return decode(
        perform(
            service_,
            transport_,
            {"GET",
             "/v1/join-requests?server_id=" +
                 std::string{server_id},
             std::string{access_token},
             {}},
            stop),
        community::parse_directory_join_requests);
}

community::DirectoryJoinRequestResult
DirectoryClient::decide_join_request(
    std::string_view access_token,
    std::string_view request_id,
    bool approve,
    std::stop_token stop) noexcept {
    try {
        if (!community::valid_remote_directory_id(
                request_id)) {
            return error(
                community::DirectoryErrorCode::invalid_config,
                "join request id is invalid");
        }
        return decode(
            perform(
                service_,
                transport_,
                {"POST",
                 "/v1/join-requests/decision",
                 std::string{access_token},
                 encoded(
                     Json{
                         {"request_id",
                          std::string{
                              request_id}},
                         {"decision",
                          approve
                              ? "approve"
                              : "reject"},
                     })},
                stop),
            community::parse_directory_join_request);
    } catch (...) {
        return error(
            community::DirectoryErrorCode::invalid_config,
            "join request decision is invalid");
    }
}

community::DirectoryJoinRequestResult
DirectoryClient::cancel_join_request(
    std::string_view access_token,
    std::string_view request_id,
    std::stop_token stop) noexcept {
    try {
        if (!community::valid_remote_directory_id(
                request_id)) {
            return error(
                community::DirectoryErrorCode::invalid_config,
                "join request id is invalid");
        }
        return decode(
            perform(
                service_,
                transport_,
                {"POST",
                 "/v1/join-requests/cancel",
                 std::string{access_token},
                 encoded(
                     Json{
                         {"request_id",
                          std::string{
                              request_id}},
                     })},
                stop),
            community::parse_directory_join_request);
    } catch (...) {
        return error(
            community::DirectoryErrorCode::invalid_config,
            "join request cancellation is invalid");
    }
}

community::DirectoryMessagesResult
DirectoryClient::list_messages(
    std::string_view access_token,
    std::string_view server_id,
    std::string_view channel_id,
    std::uint64_t after,
    std::size_t limit,
    std::stop_token stop) noexcept {
    try {
        if (!community::valid_remote_directory_id(
                server_id) ||
            !community::valid_remote_directory_id(
                channel_id) ||
            limit == 0 ||
            limit >
                community::kMaxMessagePage) {
            return error(
                community::DirectoryErrorCode::invalid_config,
                "message query is invalid");
        }
        std::string endpoint =
            "/v1/messages?server_id=";
        endpoint += server_id;
        endpoint += "&channel_id=";
        endpoint += channel_id;
        endpoint += "&after=";
        endpoint += std::to_string(after);
        endpoint += "&limit=";
        endpoint += std::to_string(limit);
        auto response =
            perform(
                service_,
                transport_,
                {"GET",
                 std::move(endpoint),
                 std::string{access_token},
                 {}},
                stop);
        if (const auto* failure =
                std::get_if<
                    community::DirectoryError>(
                    &response)) {
            return *failure;
        }
        return community::
            parse_directory_messages(
                std::get<
                    DirectoryHttpResponse>(
                    response)
                    .body,
                server_id,
                channel_id,
                after,
                limit);
    } catch (...) {
        return error(
            community::DirectoryErrorCode::invalid_config,
            "message query is invalid");
    }
}

community::DirectoryMessageResult
DirectoryClient::send_message(
    std::string_view access_token,
    std::string_view server_id,
    std::string_view channel_id,
    std::string_view content,
    std::stop_token stop) noexcept {
    try {
        if (!community::valid_remote_directory_id(
                server_id) ||
            !community::valid_remote_directory_id(
                channel_id) ||
            content.empty() ||
            content.size() >
                community::
                    kMaxMessageContentBytes) {
            return error(
                community::DirectoryErrorCode::invalid_config,
                "message is invalid");
        }
        auto result =
            decode(
                perform(
                    service_,
                    transport_,
                    {"POST",
                     "/v1/messages",
                     std::string{access_token},
                     encoded(
                         Json{
                             {"server_id",
                              std::string{
                                  server_id}},
                             {"channel_id",
                              std::string{
                                  channel_id}},
                             {"content",
                              std::string{
                                  content}},
                         })},
                    stop),
                community::parse_directory_message);
        const auto* message =
            std::get_if<
                community::DirectoryMessage>(
                &result);
        if (!message ||
            message->server_id != server_id ||
            message->channel_id != channel_id ||
            message->content != content) {
            return error(
                community::DirectoryErrorCode::
                    malformed_response,
                "sent message response is invalid");
        }
        return *message;
    } catch (...) {
        return error(
            community::DirectoryErrorCode::invalid_config,
            "message is invalid");
    }
}

community::RtcProvisioningResult
DirectoryClient::request_rtc_provisioning(
    std::string_view access_token,
    std::string_view server_id,
    std::string_view channel_id,
    std::stop_token stop) noexcept {
    try {
        if (!community::valid_remote_directory_id(
                server_id) ||
            !community::valid_remote_directory_id(
                channel_id)) {
            return error(
                community::DirectoryErrorCode::invalid_config,
                "RTC room is invalid");
        }
        return decode(
            perform(
                service_,
                transport_,
                {"POST",
                 "/v1/rtc-token",
                 std::string{access_token},
                 encoded(
                     Json{
                         {"server_id",
                          std::string{
                              server_id}},
                         {"channel_id",
                          std::string{
                              channel_id}},
                     })},
                stop),
            community::parse_rtc_provisioning);
    } catch (...) {
        return error(
            community::DirectoryErrorCode::invalid_config,
            "RTC room is invalid");
    }
}

community::DirectoryConfigResult
load_directory_service_config() noexcept {
    try {
        community::DirectoryServiceConfig config;
        if (const char* override_url =
                std::getenv(
                    "CATRO_SERVICE_URL");
            override_url != nullptr &&
            *override_url != '\0') {
            config.api_base_url =
                override_url;
            const char* insecure =
                std::getenv(
                    "CATRO_ALLOW_INSECURE_RTC");
            config.allow_insecure_http =
                insecure != nullptr &&
                std::string_view{
                    insecure} == "1";
        } else {
            NSString* resources =
                NSBundle.mainBundle
                    .resourcePath;
            if (resources == nil) {
                return error(
                    community::DirectoryErrorCode::
                        not_configured,
                    "Catro network configuration path unavailable");
            }
            const auto path =
                std::filesystem::path{
                    utf8(resources)} /
                "catro-network.json";
            std::error_code filesystem_error;
            const auto size =
                std::filesystem::file_size(
                    path,
                    filesystem_error);
            if (filesystem_error ||
                size == 0 ||
                size > kMaxConfigBytes) {
                return error(
                    community::DirectoryErrorCode::
                        not_configured,
                    "catro-network.json is not configured");
            }
            std::ifstream input(
                path,
                std::ios::binary);
            std::string raw(
                static_cast<std::size_t>(
                    size),
                '\0');
            if (!input.read(
                    raw.data(),
                    static_cast<
                        std::streamsize>(
                        raw.size()))) {
                return error(
                    community::DirectoryErrorCode::
                        invalid_config,
                    "catro-network.json could not be read");
            }
            const auto parsed =
                Json::parse(raw);
            config.api_base_url =
                parsed.at(
                    "api_base_url")
                    .get<std::string>();
            config.allow_insecure_http =
                parsed.value(
                    "allow_insecure_http",
                    false);
        }
        if (const auto failure =
                community::
                    validate_directory_service_config(
                        config)) {
            return *failure;
        }
        return config;
    } catch (...) {
        return error(
            community::DirectoryErrorCode::invalid_config,
            "catro-network.json is invalid");
    }
}

community::DirectoryStringResult
load_or_create_directory_credential(
    std::string_view keychain_service,
    std::string_view keychain_account) noexcept {
    @autoreleasepool {
        try {
            if (keychain_service.empty() ||
                keychain_account.empty() ||
                keychain_service.size() > 256 ||
                keychain_account.size() > 256) {
                return error(
                    community::DirectoryErrorCode::
                        credential_failure,
                    "directory Keychain identity is invalid");
            }
            NSString* service =
                ns_string(keychain_service);
            NSString* account =
                ns_string(keychain_account);
            if (service == nil ||
                account == nil) {
                return error(
                    community::DirectoryErrorCode::
                        credential_failure,
                    "directory Keychain identity encoding failed");
            }

            NSDictionary* lookup = @{
                (__bridge id)kSecClass :
                    (__bridge id)
                        kSecClassGenericPassword,
                (__bridge id)kSecAttrService :
                    service,
                (__bridge id)kSecAttrAccount :
                    account,
                (__bridge id)kSecReturnData :
                    @YES,
                (__bridge id)kSecMatchLimit :
                    (__bridge id)
                        kSecMatchLimitOne,
            };
            auto read_existing =
                [&]() ->
                community::
                    DirectoryStringResult {
                CFTypeRef raw = nullptr;
                const auto status =
                    SecItemCopyMatching(
                        (__bridge CFDictionaryRef)
                            lookup,
                        &raw);
                if (status ==
                    errSecItemNotFound) {
                    return error(
                        community::
                            DirectoryErrorCode::
                                not_configured,
                        "directory credential is not configured",
                        status);
                }
                if (status != errSecSuccess ||
                    raw == nullptr) {
                    if (raw != nullptr) {
                        CFRelease(raw);
                    }
                    return error(
                        community::
                            DirectoryErrorCode::
                                credential_failure,
                        "directory credential could not be read from Keychain",
                        status);
                }
                NSData* data =
                    CFBridgingRelease(raw);
                if (data.length !=
                    kCredentialBytes) {
                    return error(
                        community::
                            DirectoryErrorCode::
                                credential_failure,
                        "directory credential size is invalid");
                }
                return base64url(
                    std::span{
                        static_cast<
                            const std::byte*>(
                            data.bytes),
                        data.length});
            };

            auto existing = read_existing();
            if (std::holds_alternative<
                    std::string>(
                    existing)) {
                return existing;
            }
            const auto* missing =
                std::get_if<
                    community::
                        DirectoryError>(
                    &existing);
            if (missing == nullptr ||
                missing->code !=
                    community::
                        DirectoryErrorCode::
                            not_configured) {
                return existing;
            }

            std::array<
                std::byte,
                kCredentialBytes>
                credential{};
            const auto random_status =
                SecRandomCopyBytes(
                    kSecRandomDefault,
                    credential.size(),
                    reinterpret_cast<
                        std::uint8_t*>(
                        credential.data()));
            if (random_status !=
                errSecSuccess) {
                return error(
                    community::DirectoryErrorCode::
                        credential_failure,
                    "directory credential entropy failed",
                    random_status);
            }
            NSData* data =
                [NSData
                    dataWithBytes:
                        credential.data()
                           length:
                        credential.size()];
            NSDictionary* add = @{
                (__bridge id)kSecClass :
                    (__bridge id)
                        kSecClassGenericPassword,
                (__bridge id)kSecAttrService :
                    service,
                (__bridge id)kSecAttrAccount :
                    account,
                (__bridge id)kSecAttrAccessible :
                    (__bridge id)
                        kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly,
                (__bridge id)kSecValueData :
                    data,
            };
            const auto add_status =
                SecItemAdd(
                    (__bridge CFDictionaryRef)
                        add,
                    nullptr);
            if (add_status ==
                errSecDuplicateItem) {
                return read_existing();
            }
            if (add_status !=
                errSecSuccess) {
                return error(
                    community::DirectoryErrorCode::
                        credential_failure,
                    "directory credential could not be saved to Keychain",
                    add_status);
            }
            return base64url(credential);
        } catch (...) {
            return error(
                community::DirectoryErrorCode::
                    credential_failure,
                "directory credential initialization failed");
        }
    }
}

std::unique_ptr<DirectoryHttpTransport>
make_foundation_directory_http_transport(
    community::DirectoryServiceConfig service) noexcept {
    try {
        if (const auto failure =
                community::
                    validate_directory_service_config(
                        service)) {
            return nullptr;
        }
        return std::make_unique<
            FoundationDirectoryHttpTransport>(
            std::move(service));
    } catch (...) {
        return nullptr;
    }
}

} // namespace catro::platform::macos
