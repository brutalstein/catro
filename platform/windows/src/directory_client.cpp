#include <catro/platform/windows/directory_client.hpp>

#include <nlohmann/json.hpp>

#include <Windows.h>
#include <bcrypt.h>
#include <shlobj.h>
#include <wincrypt.h>
#include <winhttp.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

namespace catro::platform::windows {
namespace {

using Json = nlohmann::json;
using namespace std::chrono_literals;

constexpr std::size_t kCredentialBytes = 32;
constexpr std::size_t kMaxConfigBytes = 16U * 1024U;
constexpr std::size_t kMaxResponseBytes =
    community::kMaxDirectoryResponseBytes;
constexpr int kHttpTimeoutMs = 10'000;

struct InternetHandleCloser {
    void operator()(void* value) const noexcept {
        if (value != nullptr) {
            WinHttpCloseHandle(value);
        }
    }
};
using InternetHandle =
    std::unique_ptr<void, InternetHandleCloser>;

struct LocalMemoryCloser {
    void operator()(void* value) const noexcept {
        if (value != nullptr) {
            LocalFree(value);
        }
    }
};
using LocalMemory =
    std::unique_ptr<void, LocalMemoryCloser>;

struct HandleCloser {
    void operator()(void* value) const noexcept {
        if (value != nullptr &&
            value != INVALID_HANDLE_VALUE) {
            CloseHandle(value);
        }
    }
};
using UniqueHandle =
    std::unique_ptr<void, HandleCloser>;

struct ParsedBaseUrl {
    std::wstring host;
    std::wstring base_path;
    INTERNET_PORT port = 0;
    bool secure = true;
};

struct HttpResponse {
    int status = 0;
    std::string body;
};

[[nodiscard]] DirectoryError error(
    DirectoryErrorCode code,
    std::string message,
    std::int64_t native = 0,
    int status = 0) {
    return DirectoryError{
        code, std::move(message), native, status};
}

[[nodiscard]] std::optional<std::string>
environment(const char* name) {
    const auto needed =
        GetEnvironmentVariableA(name, nullptr, 0);
    if (needed == 0) {
        return std::nullopt;
    }
    std::string value(
        static_cast<std::size_t>(needed), '\0');
    const auto written =
        GetEnvironmentVariableA(
            name, value.data(), needed);
    if (written == 0 || written >= needed) {
        return std::nullopt;
    }
    value.resize(written);
    return value;
}

[[nodiscard]] std::wstring wide(
    std::string_view value) {
    if (value.empty()) {
        return {};
    }
    const auto required = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        nullptr, 0);
    if (required <= 0) {
        return {};
    }
    std::wstring output(
        static_cast<std::size_t>(required), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS,
            value.data(),
            static_cast<int>(value.size()),
            output.data(),
            required) != required) {
        return {};
    }
    return output;
}

[[nodiscard]] std::variant<
    ParsedBaseUrl, DirectoryError>
parse_base_url(
    const DirectoryServiceConfig& service) {
    if (const auto failure =
            community::validate_directory_service_config(
                service)) {
        return *failure;
    }
    const auto url = wide(service.api_base_url);
    if (url.empty()) {
        return error(
            DirectoryErrorCode::invalid_config,
            "directory API URL is invalid");
    }

    URL_COMPONENTS components{};
    components.dwStructSize = sizeof(components);
    components.dwSchemeLength =
        static_cast<DWORD>(-1);
    components.dwHostNameLength =
        static_cast<DWORD>(-1);
    components.dwUrlPathLength =
        static_cast<DWORD>(-1);
    components.dwExtraInfoLength =
        static_cast<DWORD>(-1);

    if (!WinHttpCrackUrl(
            url.c_str(),
            static_cast<DWORD>(url.size()),
            0,
            &components)) {
        return error(
            DirectoryErrorCode::invalid_config,
            "directory API URL could not be parsed",
            static_cast<std::int64_t>(
                GetLastError()));
    }

    const bool secure =
        components.nScheme == INTERNET_SCHEME_HTTPS;
    const bool plain =
        components.nScheme == INTERNET_SCHEME_HTTP;
    if ((!secure && !plain) ||
        (plain && !service.allow_insecure_http) ||
        components.dwHostNameLength == 0 ||
        components.dwExtraInfoLength != 0) {
        return error(
            DirectoryErrorCode::invalid_config,
            "directory API requires HTTPS");
    }

    ParsedBaseUrl parsed;
    parsed.secure = secure;
    parsed.port = components.nPort;
    parsed.host.assign(
        components.lpszHostName,
        components.dwHostNameLength);
    if (components.dwUrlPathLength != 0) {
        parsed.base_path.assign(
            components.lpszUrlPath,
            components.dwUrlPathLength);
    }
    while (parsed.base_path.size() > 1 &&
           parsed.base_path.back() == L'/') {
        parsed.base_path.pop_back();
    }
    if (parsed.base_path == L"/") {
        parsed.base_path.clear();
    }
    return parsed;
}

[[nodiscard]] std::variant<
    HttpResponse, DirectoryError>
request_json(
    const DirectoryServiceConfig& service,
    std::wstring_view method,
    std::wstring_view endpoint,
    std::string_view access_token,
    const Json* body) {
    const auto parsed_result =
        parse_base_url(service);
    if (const auto* failure =
            std::get_if<DirectoryError>(
                &parsed_result)) {
        return *failure;
    }
    const auto parsed =
        std::get<ParsedBaseUrl>(
            parsed_result);

    InternetHandle session{
        WinHttpOpen(
            L"Catro/0.1",
            WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS,
            0)};
    if (!session) {
        return error(
            DirectoryErrorCode::network_failure,
            "WinHTTP session creation failed",
            static_cast<std::int64_t>(
                GetLastError()));
    }
    (void)WinHttpSetTimeouts(
        session.get(),
        kHttpTimeoutMs,
        kHttpTimeoutMs,
        kHttpTimeoutMs,
        kHttpTimeoutMs);

    InternetHandle connection{
        WinHttpConnect(
            session.get(),
            parsed.host.c_str(),
            parsed.port,
            0)};
    if (!connection) {
        return error(
            DirectoryErrorCode::network_failure,
            "directory connection failed",
            static_cast<std::int64_t>(
                GetLastError()));
    }

    std::wstring path = parsed.base_path;
    if (endpoint.empty() ||
        endpoint.front() != L'/') {
        path.push_back(L'/');
    }
    path.append(endpoint);
    if (path.empty()) {
        path = L"/";
    }

    const wchar_t* accept_types[] = {
        L"application/json", nullptr};
    InternetHandle request{
        WinHttpOpenRequest(
            connection.get(),
            std::wstring(method).c_str(),
            path.c_str(),
            nullptr,
            WINHTTP_NO_REFERER,
            accept_types,
            parsed.secure
                ? WINHTTP_FLAG_SECURE
                : 0)};
    if (!request) {
        return error(
            DirectoryErrorCode::network_failure,
            "directory request creation failed",
            static_cast<std::int64_t>(
                GetLastError()));
    }

    DWORD disable_redirects =
        WINHTTP_DISABLE_REDIRECTS;
    (void)WinHttpSetOption(
        request.get(),
        WINHTTP_OPTION_DISABLE_FEATURE,
        &disable_redirects,
        sizeof(disable_redirects));

    std::string encoded;
    if (body != nullptr) {
        try {
            encoded = body->dump();
        } catch (...) {
            return error(
                DirectoryErrorCode::invalid_config,
                "directory request JSON failed");
        }
    }

    std::wstring headers =
        L"Accept: application/json\r\n";
    if (body != nullptr) {
        headers +=
            L"Content-Type: application/json\r\n";
    }
    if (!access_token.empty()) {
        const auto token = wide(access_token);
        if (token.empty()) {
            return error(
                DirectoryErrorCode::invalid_config,
                "directory token encoding failed");
        }
        headers += L"Authorization: Bearer ";
        headers += token;
        headers += L"\r\n";
    }

    if (encoded.size() >
        static_cast<std::size_t>(
            std::numeric_limits<DWORD>::max())) {
        return error(
            DirectoryErrorCode::invalid_config,
            "directory request is too large");
    }

    auto* request_bytes =
        encoded.empty()
            ? WINHTTP_NO_REQUEST_DATA
            : const_cast<char*>(encoded.data());
    const auto request_size =
        static_cast<DWORD>(encoded.size());
    if (!WinHttpSendRequest(
            request.get(),
            headers.c_str(),
            static_cast<DWORD>(headers.size()),
            request_bytes,
            request_size,
            request_size,
            0) ||
        !WinHttpReceiveResponse(
            request.get(), nullptr)) {
        return error(
            DirectoryErrorCode::network_failure,
            "directory request failed",
            static_cast<std::int64_t>(
                GetLastError()));
    }

    DWORD status = 0;
    DWORD status_size = sizeof(status);
    if (!WinHttpQueryHeaders(
            request.get(),
            WINHTTP_QUERY_STATUS_CODE |
                WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &status,
            &status_size,
            WINHTTP_NO_HEADER_INDEX)) {
        return error(
            DirectoryErrorCode::network_failure,
            "directory HTTP status unavailable",
            static_cast<std::int64_t>(
                GetLastError()));
    }

    HttpResponse response;
    response.status = static_cast<int>(status);

    while (true) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(
                request.get(), &available)) {
            return error(
                DirectoryErrorCode::network_failure,
                "directory response read failed",
                static_cast<std::int64_t>(
                    GetLastError()));
        }
        if (available == 0) {
            break;
        }
        if (response.body.size() + available >
            kMaxResponseBytes) {
            return error(
                DirectoryErrorCode::malformed_response,
                "directory response exceeds size bound");
        }

        const auto old_size = response.body.size();
        response.body.resize(
            old_size + available);
        DWORD read = 0;
        if (!WinHttpReadData(
                request.get(),
                response.body.data() + old_size,
                available,
                &read)) {
            return error(
                DirectoryErrorCode::network_failure,
                "directory response read failed",
                static_cast<std::int64_t>(
                    GetLastError()));
        }
        response.body.resize(old_size + read);
        if (read == 0) {
            break;
        }
    }

    return response;
}

[[nodiscard]] DirectoryError
error_from_response(
    const HttpResponse& response) {
    return community::map_directory_http_error(
        response.status, response.body);
}

[[nodiscard]] std::variant<
    Json, DirectoryError>
successful_json(
    std::variant<
        HttpResponse, DirectoryError> response) {
    if (const auto* failure =
            std::get_if<DirectoryError>(
                &response)) {
        return *failure;
    }
    auto value =
        std::get<HttpResponse>(
            std::move(response));
    if (value.status < 200 ||
        value.status >= 300) {
        return error_from_response(value);
    }
    try {
        return Json::parse(value.body);
    } catch (...) {
        return error(
            DirectoryErrorCode::malformed_response,
            "directory returned invalid JSON",
            0,
            value.status);
    }
}

template <class Result>
[[nodiscard]] Result parse_response(
    std::variant<Json, DirectoryError> response,
    Result (*parser)(std::string_view) noexcept) {
    if (const auto* failure =
            std::get_if<DirectoryError>(
                &response)) {
        return *failure;
    }
    try {
        return parser(
            std::get<Json>(response).dump());
    } catch (...) {
        return error(
            DirectoryErrorCode::malformed_response,
            "directory response serialization failed");
    }
}

[[nodiscard]] bool valid_remote_id(std::string_view value) noexcept {
    return community::valid_remote_directory_id(value);
}

[[nodiscard]] bool valid_server_code(std::string_view value) noexcept {
    return community::valid_directory_server_code(value);
}

[[nodiscard]] std::filesystem::path
credential_path() {
    PWSTR raw = nullptr;
    const auto result =
        SHGetKnownFolderPath(
            FOLDERID_LocalAppData,
            KF_FLAG_CREATE,
            nullptr,
            &raw);
    if (FAILED(result) || raw == nullptr) {
        if (raw != nullptr) {
            CoTaskMemFree(raw);
        }
        return {};
    }
    const std::filesystem::path root{raw};
    CoTaskMemFree(raw);
    return root /
           L"Catro" /
           L"directory-credential-v1.bin";
}

[[nodiscard]] std::variant<
    std::array<std::byte, kCredentialBytes>,
    DirectoryError>
read_protected_credential(
    const std::filesystem::path& path) {
    std::error_code filesystem_error;
    const auto size =
        std::filesystem::file_size(
            path, filesystem_error);
    if (filesystem_error) {
        return error(
            DirectoryErrorCode::credential_failure,
            "credential file unavailable",
            filesystem_error.value());
    }
    if (size == 0 || size > 4096) {
        return error(
            DirectoryErrorCode::credential_failure,
            "credential file is invalid");
    }

    std::vector<std::byte> encrypted(
        static_cast<std::size_t>(size));
    std::ifstream input(
        path, std::ios::binary);
    if (!input.read(
            reinterpret_cast<char*>(
                encrypted.data()),
            static_cast<std::streamsize>(
                encrypted.size()))) {
        return error(
            DirectoryErrorCode::credential_failure,
            "credential file read failed");
    }

    DATA_BLOB source{
        static_cast<DWORD>(encrypted.size()),
        reinterpret_cast<BYTE*>(
            encrypted.data())};
    DATA_BLOB decoded{};
    if (!CryptUnprotectData(
            &source,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            CRYPTPROTECT_UI_FORBIDDEN,
            &decoded)) {
        return error(
            DirectoryErrorCode::credential_failure,
            "credential decryption failed",
            static_cast<std::int64_t>(
                GetLastError()));
    }
    LocalMemory decoded_memory{decoded.pbData};
    if (decoded.cbData != kCredentialBytes) {
        return error(
            DirectoryErrorCode::credential_failure,
            "credential size is invalid");
    }

    std::array<std::byte, kCredentialBytes>
        credential{};
    std::copy_n(
        reinterpret_cast<const std::byte*>(
            decoded.pbData),
        kCredentialBytes,
        credential.begin());
    return credential;
}

[[nodiscard]] std::variant<
    std::array<std::byte, kCredentialBytes>,
    DirectoryError>
load_or_create_credential_bytes() {
    const auto path = credential_path();
    if (path.empty()) {
        return error(
            DirectoryErrorCode::credential_failure,
            "LocalAppData credential path unavailable");
    }

    std::error_code filesystem_error;
    std::filesystem::create_directories(
        path.parent_path(),
        filesystem_error);
    if (filesystem_error) {
        return error(
            DirectoryErrorCode::credential_failure,
            "credential directory creation failed",
            filesystem_error.value());
    }

    auto lock_path = path;
    lock_path += L".lock";
    UniqueHandle lock;
    for (int attempt = 0;
         attempt < 200;
         ++attempt) {
        auto raw = CreateFileW(
            lock_path.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            0,
            nullptr,
            OPEN_ALWAYS,
            FILE_ATTRIBUTE_TEMPORARY |
                FILE_FLAG_DELETE_ON_CLOSE,
            nullptr);
        if (raw != INVALID_HANDLE_VALUE) {
            lock.reset(raw);
            break;
        }
        const auto code = GetLastError();
        if (code != ERROR_SHARING_VIOLATION &&
            code != ERROR_LOCK_VIOLATION) {
            return error(
                DirectoryErrorCode::credential_failure,
                "credential lock failed",
                code);
        }
        std::this_thread::sleep_for(25ms);
    }
    if (!lock) {
        return error(
            DirectoryErrorCode::credential_failure,
            "credential lock timed out",
            ERROR_TIMEOUT);
    }

    if (std::filesystem::exists(
            path, filesystem_error) &&
        !filesystem_error) {
        return read_protected_credential(path);
    }

    std::array<std::byte, kCredentialBytes>
        credential{};
    if (BCryptGenRandom(
            nullptr,
            reinterpret_cast<PUCHAR>(
                credential.data()),
            static_cast<ULONG>(
                credential.size()),
            BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
        return error(
            DirectoryErrorCode::credential_failure,
            "credential entropy failed");
    }

    DATA_BLOB source{
        static_cast<DWORD>(
            credential.size()),
        reinterpret_cast<BYTE*>(
            credential.data())};
    DATA_BLOB encrypted{};
    if (!CryptProtectData(
            &source,
            L"Catro directory credential",
            nullptr,
            nullptr,
            nullptr,
            CRYPTPROTECT_UI_FORBIDDEN,
            &encrypted)) {
        return error(
            DirectoryErrorCode::credential_failure,
            "credential encryption failed",
            static_cast<std::int64_t>(
                GetLastError()));
    }
    LocalMemory encrypted_memory{
        encrypted.pbData};

    auto staged = path;
    staged += L".tmp";
    UniqueHandle output{
        CreateFileW(
            staged.c_str(),
            GENERIC_WRITE,
            0,
            nullptr,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_HIDDEN |
                FILE_FLAG_WRITE_THROUGH,
            nullptr)};
    if (!output ||
        output.get() ==
            INVALID_HANDLE_VALUE) {
        return error(
            DirectoryErrorCode::credential_failure,
            "credential staging file failed",
            static_cast<std::int64_t>(
                GetLastError()));
    }

    DWORD written = 0;
    if (!WriteFile(
            output.get(),
            encrypted.pbData,
            encrypted.cbData,
            &written,
            nullptr) ||
        written != encrypted.cbData ||
        !FlushFileBuffers(output.get())) {
        return error(
            DirectoryErrorCode::credential_failure,
            "credential write failed",
            static_cast<std::int64_t>(
                GetLastError()));
    }
    output.reset();

    if (!MoveFileExW(
            staged.c_str(),
            path.c_str(),
            MOVEFILE_REPLACE_EXISTING |
                MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(staged.c_str());
        return error(
            DirectoryErrorCode::credential_failure,
            "credential commit failed",
            static_cast<std::int64_t>(
                GetLastError()));
    }

    return credential;
}

[[nodiscard]] std::string base64url(
    std::span<const std::byte> bytes) {
    static constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789-_";

    std::string output;
    output.reserve(
        (bytes.size() * 4U + 2U) / 3U);

    std::uint32_t accumulator = 0;
    int bits = 0;
    for (const auto byte : bytes) {
        accumulator =
            (accumulator << 8U) |
            std::to_integer<std::uint32_t>(
                byte);
        bits += 8;
        while (bits >= 6) {
            bits -= 6;
            output.push_back(
                alphabet[
                    (accumulator >> bits) &
                    0x3fU]);
        }
    }
    if (bits > 0) {
        output.push_back(
            alphabet[
                (accumulator <<
                 (6 - bits)) &
                0x3fU]);
    }
    return output;
}

[[nodiscard]] std::optional<
    community::Channel>
channel_of_kind(
    const community::PersonalServer& server,
    community::ChannelKind kind) {
    for (const auto& channel : server.channels) {
        if (channel.kind == kind) {
            return channel;
        }
    }
    return std::nullopt;
}

} // namespace

DirectoryConfigResult
load_directory_service_config() noexcept {
    try {
        DirectoryServiceConfig config;

        if (const auto override_url =
                environment(
                    "CATRO_SERVICE_URL")) {
            config.api_base_url =
                *override_url;
            config.allow_insecure_http =
                environment(
                    "CATRO_ALLOW_INSECURE_RTC")
                    .value_or("") == "1";
        } else {
            std::wstring module_path(
                32768, L'\0');
            const auto length =
                GetModuleFileNameW(
                    nullptr,
                    module_path.data(),
                    static_cast<DWORD>(
                        module_path.size()));
            if (length == 0 ||
                length >= module_path.size()) {
                return error(
                    DirectoryErrorCode::not_configured,
                    "Catro network configuration path unavailable",
                    static_cast<std::int64_t>(
                        GetLastError()));
            }
            module_path.resize(length);
            const auto path =
                std::filesystem::path{
                    module_path}
                    .parent_path() /
                L"catro-network.json";

            std::error_code filesystem_error;
            const auto size =
                std::filesystem::file_size(
                    path,
                    filesystem_error);
            if (filesystem_error ||
                size == 0 ||
                size > kMaxConfigBytes) {
                return error(
                    DirectoryErrorCode::not_configured,
                    "catro-network.json is not configured");
            }

            // Binary mode: text mode folds CRLF, so file_size bytes would never be read.
            std::ifstream input(path, std::ios::binary);
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
                    DirectoryErrorCode::invalid_config,
                    "catro-network.json could not be read");
            }

            const auto parsed =
                Json::parse(raw);
            config.api_base_url =
                parsed.at("api_base_url")
                    .get<std::string>();
            config.allow_insecure_http =
                parsed.value(
                    "allow_insecure_http",
                    false);
        }

        if (const auto parsed =
                parse_base_url(config);
            std::holds_alternative<
                DirectoryError>(parsed)) {
            return std::get<
                DirectoryError>(parsed);
        }
        return config;
    } catch (...) {
        return error(
            DirectoryErrorCode::invalid_config,
            "catro-network.json is invalid");
    }
}

DirectoryStringResult
load_or_create_directory_credential() noexcept {
    try {
        const auto value =
            load_or_create_credential_bytes();
        if (const auto* failure =
                std::get_if<DirectoryError>(
                    &value)) {
            return *failure;
        }
        return base64url(
            std::get<
                std::array<
                    std::byte,
                    kCredentialBytes>>(
                value));
    } catch (...) {
        return error(
            DirectoryErrorCode::credential_failure,
            "directory credential initialization failed");
    }
}

DirectoryStringResult register_directory_identity(
    const DirectoryServiceConfig& service,
    const community::Identity& identity,
    std::string_view credential) noexcept {
    try {
        const Json body{
            {"user_id", community::to_hex(identity.id)},
            {"display_name", identity.display_name},
            {"credential", std::string{credential}},
        };
        return parse_response(
            successful_json(request_json(
                service, L"POST", L"/v1/users/register", {}, &body)),
            community::parse_directory_access_token);
    } catch (...) {
        return error(
            DirectoryErrorCode::malformed_response,
            "directory registration response is invalid");
    }
}

DirectoryServerResult sync_personal_server(
    const DirectoryServiceConfig& service,
    std::string_view access_token,
    const community::PersonalServer& server) noexcept {
    try {
        const auto text = channel_of_kind(server, community::ChannelKind::text);
        const auto voice = channel_of_kind(server, community::ChannelKind::voice);
        if (!text || !voice) {
            return error(
                DirectoryErrorCode::invalid_config,
                "personal server is missing a required channel");
        }
        const Json body{
            {"server_id", community::to_hex(server.id)},
            {"name", server.name},
            {"text_channel_id", community::to_hex(text->id)},
            {"voice_channel_id", community::to_hex(voice->id)},
        };
        return parse_response(
            successful_json(request_json(
                service, L"POST", L"/v1/servers/sync", access_token, &body)),
            community::parse_directory_server);
    } catch (...) {
        return error(
            DirectoryErrorCode::malformed_response,
            "directory server response is invalid");
    }
}

DirectoryServersResult list_directory_servers(
    const DirectoryServiceConfig& service,
    std::string_view access_token) noexcept {
    return parse_response(
        successful_json(request_json(
            service, L"GET", L"/v1/servers", access_token, nullptr)),
        community::parse_directory_servers);
}

DirectoryInviteResult create_directory_invite(
    const DirectoryServiceConfig& service,
    std::string_view access_token,
    std::string_view server_id) noexcept {
    try {
        if (!valid_remote_id(server_id)) {
            return error(DirectoryErrorCode::invalid_config, "invite server is invalid");
        }
        const Json body{{"server_id", std::string{server_id}}};
        return parse_response(
            successful_json(request_json(
                service, L"POST", L"/v1/invites", access_token, &body)),
            community::parse_directory_invite);
    } catch (...) {
        return error(
            DirectoryErrorCode::malformed_response,
            "directory invite response is invalid");
    }
}

DirectoryServerResult accept_directory_invite(
    const DirectoryServiceConfig& service,
    std::string_view access_token,
    std::string_view invite_code) noexcept {
    try {
        if (invite_code.empty() || invite_code.size() > 128) {
            return error(DirectoryErrorCode::invalid_config, "invite code is invalid");
        }
        const Json body{{"code", std::string{invite_code}}};
        return parse_response(
            successful_json(request_json(
                service, L"POST", L"/v1/invites/accept", access_token, &body)),
            community::parse_directory_server);
    } catch (...) {
        return error(
            DirectoryErrorCode::malformed_response,
            "invite acceptance response is invalid");
    }
}

DirectoryMembersResult list_directory_members(
    const DirectoryServiceConfig& service,
    std::string_view access_token,
    std::string_view server_id) noexcept {
    if (!valid_remote_id(server_id)) {
        return error(DirectoryErrorCode::invalid_config, "member roster server is invalid");
    }
    const auto server = wide(server_id);
    if (server.empty()) {
        return error(
            DirectoryErrorCode::invalid_config,
            "member roster server encoding failed");
    }
    return parse_response(
        successful_json(request_json(
            service,
            L"GET",
            std::wstring{L"/v1/members?server_id="} + server,
            access_token,
            nullptr)),
        community::parse_directory_members);
}

DirectoryServerLookupResult lookup_directory_server(
    const DirectoryServiceConfig& service,
    std::string_view access_token,
    std::string_view server_code) noexcept {
    if (!valid_server_code(server_code)) {
        return error(DirectoryErrorCode::invalid_config, "Server Code is invalid");
    }
    const auto code = wide(server_code);
    if (code.empty()) {
        return error(DirectoryErrorCode::invalid_config, "Server Code encoding failed");
    }
    return parse_response(
        successful_json(request_json(
            service,
            L"GET",
            std::wstring{L"/v1/server-lookup?code="} + code,
            access_token,
            nullptr)),
        community::parse_directory_server_lookup);
}

DirectoryJoinRequestResult create_directory_join_request(
    const DirectoryServiceConfig& service,
    std::string_view access_token,
    std::string_view server_code,
    std::string_view message) noexcept {
    try {
        if (!valid_server_code(server_code) ||
            message.size() > community::kMaxJoinRequestMessageBytes) {
            return error(DirectoryErrorCode::invalid_config, "join request is invalid");
        }
        const Json body{
            {"server_code", std::string{server_code}},
            {"message", std::string{message}},
        };
        return parse_response(
            successful_json(request_json(
                service, L"POST", L"/v1/join-requests", access_token, &body)),
            community::parse_directory_join_request);
    } catch (...) {
        return error(
            DirectoryErrorCode::malformed_response,
            "join request response is invalid");
    }
}

[[nodiscard]] DirectoryJoinRequestsResult
parse_join_request_page(std::variant<Json, DirectoryError> response) {
    return parse_response(
        std::move(response),
        community::parse_directory_join_requests);
}

DirectoryJoinRequestsResult list_outgoing_directory_join_requests(
    const DirectoryServiceConfig& service,
    std::string_view access_token) noexcept {
    return parse_join_request_page(
        successful_json(
            request_json(
                service,
                L"GET",
                L"/v1/join-requests?mine=1",
                access_token,
                nullptr)));
}

DirectoryJoinRequestsResult list_pending_directory_join_requests(
    const DirectoryServiceConfig& service,
    std::string_view access_token,
    std::string_view server_id) noexcept {
    if (!valid_remote_id(server_id)) {
        return error(
            DirectoryErrorCode::invalid_config,
            "join request server is invalid");
    }
    const auto server = wide(server_id);
    if (server.empty()) {
        return error(
            DirectoryErrorCode::invalid_config,
            "join request server encoding failed");
    }
    std::wstring endpoint =
        L"/v1/join-requests?server_id=";
    endpoint += server;
    return parse_join_request_page(
        successful_json(
            request_json(
                service,
                L"GET",
                endpoint,
                access_token,
                nullptr)));
}

DirectoryJoinRequestResult decide_directory_join_request(
    const DirectoryServiceConfig& service,
    std::string_view access_token,
    std::string_view request_id,
    bool approve) noexcept {
    try {
        if (!valid_remote_id(request_id)) {
            return error(
                DirectoryErrorCode::invalid_config,
                "join request id is invalid");
        }
        const Json body{
            {"request_id", std::string{request_id}},
            {"decision", approve ? "approve" : "reject"},
        };
        return parse_response(
            successful_json(
                request_json(
                    service,
                    L"POST",
                    L"/v1/join-requests/decision",
                    access_token,
                    &body)),
            community::parse_directory_join_request);
    } catch (...) {
        return error(
            DirectoryErrorCode::malformed_response,
            "join request decision response is invalid");
    }
}

DirectoryJoinRequestResult cancel_directory_join_request(
    const DirectoryServiceConfig& service,
    std::string_view access_token,
    std::string_view request_id) noexcept {
    try {
        if (!valid_remote_id(request_id)) {
            return error(
                DirectoryErrorCode::invalid_config,
                "join request id is invalid");
        }
        const Json body{
            {"request_id", std::string{request_id}},
        };
        return parse_response(
            successful_json(
                request_json(
                    service,
                    L"POST",
                    L"/v1/join-requests/cancel",
                    access_token,
                    &body)),
            community::parse_directory_join_request);
    } catch (...) {
        return error(
            DirectoryErrorCode::malformed_response,
            "join request cancel response is invalid");
    }
}

DirectoryMessagesResult list_directory_messages(
    const DirectoryServiceConfig& service,
    std::string_view access_token,
    std::string_view server_id,
    std::string_view channel_id,
    std::uint64_t after,
    std::size_t limit) noexcept {
    try {
        if (!valid_remote_id(server_id) || !valid_remote_id(channel_id) || limit == 0 ||
            limit > community::kMaxMessagePage) {
            return error(DirectoryErrorCode::invalid_config, "message query is invalid");
        }
        const auto server = wide(server_id);
        const auto channel = wide(channel_id);
        if (server.empty() || channel.empty()) {
            return error(
                DirectoryErrorCode::invalid_config,
                "message query encoding failed");
        }
        std::wstring endpoint = L"/v1/messages?server_id=";
        endpoint += server;
        endpoint += L"&channel_id=";
        endpoint += channel;
        endpoint += L"&after=";
        endpoint += std::to_wstring(after);
        endpoint += L"&limit=";
        endpoint += std::to_wstring(limit);
        const auto response = successful_json(request_json(
            service, L"GET", endpoint, access_token, nullptr));
        if (const auto* failure = std::get_if<DirectoryError>(&response)) {
            return *failure;
        }
        return community::parse_directory_messages(
            std::get<Json>(response).dump(), server_id, channel_id, after, limit);
    } catch (...) {
        return error(
            DirectoryErrorCode::malformed_response,
            "message page response is invalid");
    }
}

DirectoryMessageResult send_directory_message(
    const DirectoryServiceConfig& service,
    std::string_view access_token,
    std::string_view server_id,
    std::string_view channel_id,
    std::string_view content) noexcept {
    try {
        if (!valid_remote_id(server_id) || !valid_remote_id(channel_id) || content.empty() ||
            content.size() > community::kMaxMessageContentBytes) {
            return error(DirectoryErrorCode::invalid_config, "message is invalid");
        }
        const Json body{
            {"server_id", std::string{server_id}},
            {"channel_id", std::string{channel_id}},
            {"content", std::string{content}},
        };
        auto parsed = parse_response(
            successful_json(request_json(
                service, L"POST", L"/v1/messages", access_token, &body)),
            community::parse_directory_message);
        const auto* message = std::get_if<DirectoryMessage>(&parsed);
        if (!message || message->server_id != server_id || message->channel_id != channel_id ||
            message->content != content) {
            return error(
                DirectoryErrorCode::malformed_response,
                "sent message response is invalid");
        }
        return *message;
    } catch (...) {
        return error(
            DirectoryErrorCode::malformed_response,
            "sent message response is invalid");
    }
}

RtcProvisioningResult request_rtc_provisioning(
    const DirectoryServiceConfig& service,
    std::string_view access_token,
    std::string_view server_id,
    std::string_view channel_id) noexcept {
    try {
        if (!valid_remote_id(server_id) || !valid_remote_id(channel_id)) {
            return error(DirectoryErrorCode::invalid_config, "RTC room is invalid");
        }
        const Json body{
            {"server_id", std::string{server_id}},
            {"channel_id", std::string{channel_id}},
        };
        return parse_response(
            successful_json(request_json(
                service, L"POST", L"/v1/rtc-token", access_token, &body)),
            community::parse_rtc_provisioning);
    } catch (...) {
        return error(
            DirectoryErrorCode::malformed_response,
            "RTC provisioning response is invalid");
    }
}

} // namespace catro::platform::windows
