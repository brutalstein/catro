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
constexpr std::size_t kMaxResponseBytes = 256U * 1024U;
constexpr std::size_t kMaxMessageContentBytes = 2000;
constexpr std::size_t kMaxJoinRequestMessageBytes = 280;
constexpr std::size_t kMaxJoinRequestPage = 64;
constexpr std::size_t kMaxMessagePage = 100;
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
    std::string message =
        "directory request rejected";
    try {
        const auto parsed =
            Json::parse(response.body);
        if (const auto found =
                parsed.find("error");
            found != parsed.end() &&
            found->is_string()) {
            message =
                found->get<std::string>();
        }
    } catch (...) {
    }

    const auto code =
        response.status == 401
            ? DirectoryErrorCode::unauthorized
            : DirectoryErrorCode::rejected;
    return error(
        code,
        std::move(message),
        0,
        response.status);
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

[[nodiscard]] bool valid_remote_id(std::string_view value) noexcept {
    if (value.empty() || value.size() > 128) {
        return false;
    }
    for (const auto ch : value) {
        const bool alpha =
            (ch >= 'a' && ch <= 'z') ||
            (ch >= 'A' && ch <= 'Z');
        const bool digit = ch >= '0' && ch <= '9';
        if (!alpha && !digit &&
            ch != '-' && ch != '_' && ch != ':') {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool valid_server_code(std::string_view value) noexcept {
    if (value.size() != 28 ||
        value.substr(0, 4) != "CAT-") {
        return false;
    }
    for (std::size_t index = 4; index < value.size(); ++index) {
        if (index == 8 || index == 13 ||
            index == 18 || index == 23) {
            if (value[index] != '-') {
                return false;
            }
            continue;
        }
        const auto ch = value[index];
        const bool digit = ch >= '0' && ch <= '9';
        const bool hex = ch >= 'A' && ch <= 'F';
        if (!digit && !hex) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool valid_join_request_status(
    std::string_view value) noexcept {
    return value == "pending" ||
           value == "approved" ||
           value == "rejected" ||
           value == "cancelled";
}

[[nodiscard]] std::optional<DirectoryServer>
parse_server(const Json& value) {
    try {
        DirectoryServer server;
        server.id = value.at("id").get<std::string>();
        server.owner_id =
            value.at("owner_id").get<std::string>();
        server.name =
            value.at("name").get<std::string>();
        server.public_code =
            value.value("public_code", std::string{});
        server.text_channel_id =
            value.value("text_channel_id", std::string{});
        server.voice_channel_id =
            value.at("voice_channel_id")
                .get<std::string>();
        server.role =
            value.at("role").get<std::string>();
        server.member_count =
            value.at("member_count")
                .get<std::size_t>();
        if (!valid_remote_id(server.id) ||
            !valid_remote_id(server.owner_id) ||
            server.name.empty() ||
            server.name.size() > community::kMaxServerNameBytes ||
            (server.role == "owner" &&
             !valid_server_code(server.public_code)) ||
            (server.role != "owner" &&
             !server.public_code.empty()) ||
            (!server.text_channel_id.empty() &&
             !valid_remote_id(server.text_channel_id)) ||
            !valid_remote_id(server.voice_channel_id) ||
            (server.role != "owner" &&
             server.role != "member") ||
            server.member_count == 0 ||
            server.member_count >
                community::kMaxMembers) {
            return std::nullopt;
        }
        return server;
    } catch (...) {
        return std::nullopt;
    }
}

[[nodiscard]] std::optional<DirectoryMember>
parse_member(const Json& value) {
    try {
        DirectoryMember member;
        member.user_id =
            value.at("user_id").get<std::string>();
        member.display_name =
            value.at("display_name").get<std::string>();
        member.role =
            value.at("role").get<std::string>();

        if (!valid_remote_id(member.user_id) ||
            member.display_name.empty() ||
            member.display_name.size() >
                community::kMaxDisplayNameBytes ||
            (member.role != "owner" &&
             member.role != "member")) {
            return std::nullopt;
        }
        return member;
    } catch (...) {
        return std::nullopt;
    }
}

[[nodiscard]] std::optional<DirectoryServerLookup>
parse_server_lookup(const Json& value) {
    try {
        DirectoryServerLookup preview;
        preview.public_code =
            value.at("public_code").get<std::string>();
        preview.name =
            value.at("name").get<std::string>();
        preview.member_count =
            value.at("member_count").get<std::size_t>();
        preview.relationship =
            value.at("relationship").get<std::string>();
        preview.request_id =
            value.value("request_id", std::string{});

        const bool relationship_ok =
            preview.relationship == "none" ||
            preview.relationship == "pending" ||
            preview.relationship == "member" ||
            preview.relationship == "owner";
        if (!valid_server_code(preview.public_code) ||
            preview.name.empty() ||
            preview.name.size() > community::kMaxServerNameBytes ||
            preview.member_count == 0 ||
            preview.member_count > community::kMaxMembers ||
            !relationship_ok ||
            (preview.relationship == "pending" &&
             !valid_remote_id(preview.request_id)) ||
            (preview.relationship != "pending" &&
             !preview.request_id.empty())) {
            return std::nullopt;
        }
        return preview;
    } catch (...) {
        return std::nullopt;
    }
}

[[nodiscard]] std::optional<DirectoryJoinRequest>
parse_join_request(const Json& value) {
    try {
        DirectoryJoinRequest request;
        request.id =
            value.at("id").get<std::string>();
        request.server_name =
            value.at("server_name").get<std::string>();
        request.public_code =
            value.at("public_code").get<std::string>();
        request.requester_display_name =
            value.value("requester_display_name", std::string{});
        request.message =
            value.value("message", std::string{});
        request.status =
            value.at("status").get<std::string>();
        request.created_at =
            value.at("created_at").get<std::int64_t>();
        request.updated_at =
            value.at("updated_at").get<std::int64_t>();
        request.expires_at =
            value.at("expires_at").get<std::int64_t>();

        if (!valid_remote_id(request.id) ||
            request.server_name.empty() ||
            request.server_name.size() >
                community::kMaxServerNameBytes ||
            !valid_server_code(request.public_code) ||
            (!request.requester_display_name.empty() &&
             request.requester_display_name.size() >
                 community::kMaxDisplayNameBytes) ||
            request.message.size() >
                kMaxJoinRequestMessageBytes ||
            !valid_join_request_status(request.status) ||
            request.created_at <= 0 ||
            request.updated_at < request.created_at ||
            request.expires_at <= request.updated_at) {
            return std::nullopt;
        }
        return request;
    } catch (...) {
        return std::nullopt;
    }
}

[[nodiscard]] std::optional<DirectoryMessage>
parse_message(const Json& value) {
    try {
        DirectoryMessage message;
        message.id =
            value.at("id").get<std::string>();
        message.sequence =
            value.at("sequence").get<std::uint64_t>();
        message.server_id =
            value.at("server_id").get<std::string>();
        message.channel_id =
            value.at("channel_id").get<std::string>();
        message.author_id =
            value.at("author_id").get<std::string>();
        message.author_display_name =
            value.at("author_display_name").get<std::string>();
        message.content =
            value.at("content").get<std::string>();
        message.created_at =
            value.at("created_at").get<std::int64_t>();

        if (!valid_remote_id(message.id) ||
            message.sequence == 0 ||
            !valid_remote_id(message.server_id) ||
            !valid_remote_id(message.channel_id) ||
            !valid_remote_id(message.author_id) ||
            message.author_display_name.empty() ||
            message.author_display_name.size() >
                community::kMaxDisplayNameBytes ||
            message.content.empty() ||
            message.content.size() >
                kMaxMessageContentBytes ||
            message.created_at <= 0) {
            return std::nullopt;
        }
        return message;
    } catch (...) {
        return std::nullopt;
    }
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

            std::ifstream input(path);
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
            {"user_id",
             community::to_hex(identity.id)},
            {"display_name",
             identity.display_name},
            {"credential",
             std::string{credential}},
        };
        const auto response =
            successful_json(
                request_json(
                    service,
                    L"POST",
                    L"/v1/users/register",
                    {},
                    &body));
        if (const auto* failure =
                std::get_if<DirectoryError>(
                    &response)) {
            return *failure;
        }
        const auto token =
            std::get<Json>(response)
                .at("access_token")
                .get<std::string>();
        if (token.empty()) {
            return error(
                DirectoryErrorCode::malformed_response,
                "directory access token is empty");
        }
        return token;
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
                DirectoryErrorCode::invalid_config,
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
        const auto response =
            successful_json(
                request_json(
                    service,
                    L"POST",
                    L"/v1/servers/sync",
                    access_token,
                    &body));
        if (const auto* failure =
                std::get_if<DirectoryError>(
                    &response)) {
            return *failure;
        }
        const auto parsed =
            parse_server(
                std::get<Json>(response));
        return parsed
            ? DirectoryServerResult{*parsed}
            : DirectoryServerResult{
                  error(
                      DirectoryErrorCode::malformed_response,
                      "directory server response is invalid")};
    } catch (...) {
        return error(
            DirectoryErrorCode::malformed_response,
            "directory server response is invalid");
    }
}

DirectoryServersResult list_directory_servers(
    const DirectoryServiceConfig& service,
    std::string_view access_token) noexcept {
    try {
        const auto response =
            successful_json(
                request_json(
                    service,
                    L"GET",
                    L"/v1/servers",
                    access_token,
                    nullptr));
        if (const auto* failure =
                std::get_if<DirectoryError>(
                    &response)) {
            return *failure;
        }

        const auto& json =
            std::get<Json>(response);
        const auto& items =
            json.at("servers");
        if (!items.is_array() ||
            items.size() >
                community::kMaxMembers) {
            return error(
                DirectoryErrorCode::malformed_response,
                "directory server list is invalid");
        }

        std::vector<DirectoryServer>
            servers;
        servers.reserve(items.size());
        for (const auto& item : items) {
            const auto parsed =
                parse_server(item);
            if (!parsed) {
                return error(
                    DirectoryErrorCode::malformed_response,
                    "directory server list contains invalid data");
            }
            servers.push_back(*parsed);
        }
        return servers;
    } catch (...) {
        return error(
            DirectoryErrorCode::malformed_response,
            "directory server list response is invalid");
    }
}

DirectoryInviteResult create_directory_invite(
    const DirectoryServiceConfig& service,
    std::string_view access_token,
    std::string_view server_id) noexcept {
    try {
        const Json body{
            {"server_id",
             std::string{server_id}}};
        const auto response =
            successful_json(
                request_json(
                    service,
                    L"POST",
                    L"/v1/invites",
                    access_token,
                    &body));
        if (const auto* failure =
                std::get_if<DirectoryError>(
                    &response)) {
            return *failure;
        }
        const auto& json =
            std::get<Json>(response);
        const auto server =
            parse_server(json.at("server"));
        if (!server) {
            return error(
                DirectoryErrorCode::malformed_response,
                "directory invite server is invalid");
        }

        DirectoryInvite invite;
        invite.code =
            json.at("code")
                .get<std::string>();
        invite.expires =
            json.at("expires")
                .get<std::int64_t>();
        invite.server = *server;
        if (invite.code.empty()) {
            return error(
                DirectoryErrorCode::malformed_response,
                "directory invite code is empty");
        }
        return invite;
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
        const Json body{
            {"code",
             std::string{invite_code}}};
        const auto response =
            successful_json(
                request_json(
                    service,
                    L"POST",
                    L"/v1/invites/accept",
                    access_token,
                    &body));
        if (const auto* failure =
                std::get_if<DirectoryError>(
                    &response)) {
            return *failure;
        }
        const auto parsed =
            parse_server(
                std::get<Json>(response));
        return parsed
            ? DirectoryServerResult{*parsed}
            : DirectoryServerResult{
                  error(
                      DirectoryErrorCode::malformed_response,
                      "accepted server response is invalid")};
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
    try {
        if (!valid_remote_id(server_id)) {
            return error(
                DirectoryErrorCode::invalid_config,
                "member roster server is invalid");
        }

        const auto server = wide(server_id);
        if (server.empty()) {
            return error(
                DirectoryErrorCode::invalid_config,
                "member roster server encoding failed");
        }

        std::wstring endpoint =
            L"/v1/members?server_id=";
        endpoint += server;

        const auto response =
            successful_json(
                request_json(
                    service,
                    L"GET",
                    endpoint,
                    access_token,
                    nullptr));
        if (const auto* failure =
                std::get_if<DirectoryError>(
                    &response)) {
            return *failure;
        }

        const auto& json =
            std::get<Json>(response);
        const auto& items =
            json.at("members");
        if (!items.is_array() ||
            items.empty() ||
            items.size() >
                community::kMaxMembers) {
            return error(
                DirectoryErrorCode::malformed_response,
                "member roster is invalid");
        }

        std::vector<DirectoryMember> members;
        members.reserve(items.size());
        std::unordered_set<std::string> seen;
        seen.reserve(items.size());
        std::size_t owner_count = 0;

        for (std::size_t index = 0;
             index < items.size();
             ++index) {
            const auto parsed =
                parse_member(items[index]);
            if (!parsed ||
                !seen.insert(parsed->user_id).second) {
                return error(
                    DirectoryErrorCode::malformed_response,
                    "member roster contains invalid data");
            }
            if (parsed->role == "owner") {
                ++owner_count;
                if (index != 0) {
                    return error(
                        DirectoryErrorCode::malformed_response,
                        "member roster owner ordering is invalid");
                }
            }
            members.push_back(*parsed);
        }

        if (owner_count != 1 ||
            members.front().role != "owner") {
            return error(
                DirectoryErrorCode::malformed_response,
                "member roster owner is invalid");
        }
        return members;
    } catch (...) {
        return error(
            DirectoryErrorCode::malformed_response,
            "member roster response is invalid");
    }
}

DirectoryServerLookupResult lookup_directory_server(
    const DirectoryServiceConfig& service,
    std::string_view access_token,
    std::string_view server_code) noexcept {
    try {
        if (server_code.empty() ||
            server_code.size() > 64) {
            return error(
                DirectoryErrorCode::invalid_config,
                "Server Code is invalid");
        }
        const auto code = wide(server_code);
        if (code.empty()) {
            return error(
                DirectoryErrorCode::invalid_config,
                "Server Code encoding failed");
        }
        std::wstring endpoint =
            L"/v1/server-lookup?code=";
        endpoint += code;
        const auto response =
            successful_json(
                request_json(
                    service,
                    L"GET",
                    endpoint,
                    access_token,
                    nullptr));
        if (const auto* failure =
                std::get_if<DirectoryError>(&response)) {
            return *failure;
        }
        const auto parsed =
            parse_server_lookup(std::get<Json>(response));
        return parsed
            ? DirectoryServerLookupResult{*parsed}
            : DirectoryServerLookupResult{
                  error(
                      DirectoryErrorCode::malformed_response,
                      "server lookup response is invalid")};
    } catch (...) {
        return error(
            DirectoryErrorCode::malformed_response,
            "server lookup response is invalid");
    }
}

DirectoryJoinRequestResult create_directory_join_request(
    const DirectoryServiceConfig& service,
    std::string_view access_token,
    std::string_view server_code,
    std::string_view message) noexcept {
    try {
        if (server_code.empty() ||
            server_code.size() > 64 ||
            message.size() > kMaxJoinRequestMessageBytes) {
            return error(
                DirectoryErrorCode::invalid_config,
                "join request is invalid");
        }
        const Json body{
            {"server_code", std::string{server_code}},
            {"message", std::string{message}},
        };
        const auto response =
            successful_json(
                request_json(
                    service,
                    L"POST",
                    L"/v1/join-requests",
                    access_token,
                    &body));
        if (const auto* failure =
                std::get_if<DirectoryError>(&response)) {
            return *failure;
        }
        const auto parsed =
            parse_join_request(std::get<Json>(response));
        return parsed
            ? DirectoryJoinRequestResult{*parsed}
            : DirectoryJoinRequestResult{
                  error(
                      DirectoryErrorCode::malformed_response,
                      "join request response is invalid")};
    } catch (...) {
        return error(
            DirectoryErrorCode::malformed_response,
            "join request response is invalid");
    }
}

[[nodiscard]] DirectoryJoinRequestsResult
parse_join_request_page(
    std::variant<Json, DirectoryError> response) {
    if (const auto* failure =
            std::get_if<DirectoryError>(&response)) {
        return *failure;
    }
    try {
        const auto& items =
            std::get<Json>(response).at("requests");
        if (!items.is_array() ||
            items.size() > kMaxJoinRequestPage) {
            return error(
                DirectoryErrorCode::malformed_response,
                "join request page is invalid");
        }
        std::vector<DirectoryJoinRequest> requests;
        requests.reserve(items.size());
        std::unordered_set<std::string> ids;
        ids.reserve(items.size());
        for (const auto& item : items) {
            const auto parsed = parse_join_request(item);
            if (!parsed ||
                !ids.insert(parsed->id).second) {
                return error(
                    DirectoryErrorCode::malformed_response,
                    "join request page contains invalid data");
            }
            requests.push_back(*parsed);
        }
        return requests;
    } catch (...) {
        return error(
            DirectoryErrorCode::malformed_response,
            "join request page response is invalid");
    }
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
        const auto response =
            successful_json(
                request_json(
                    service,
                    L"POST",
                    L"/v1/join-requests/decision",
                    access_token,
                    &body));
        if (const auto* failure =
                std::get_if<DirectoryError>(&response)) {
            return *failure;
        }
        const auto parsed =
            parse_join_request(std::get<Json>(response));
        return parsed
            ? DirectoryJoinRequestResult{*parsed}
            : DirectoryJoinRequestResult{
                  error(
                      DirectoryErrorCode::malformed_response,
                      "join request decision response is invalid")};
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
        const auto response =
            successful_json(
                request_json(
                    service,
                    L"POST",
                    L"/v1/join-requests/cancel",
                    access_token,
                    &body));
        if (const auto* failure =
                std::get_if<DirectoryError>(&response)) {
            return *failure;
        }
        const auto parsed =
            parse_join_request(std::get<Json>(response));
        return parsed
            ? DirectoryJoinRequestResult{*parsed}
            : DirectoryJoinRequestResult{
                  error(
                      DirectoryErrorCode::malformed_response,
                      "join request cancel response is invalid")};
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
        if (!valid_remote_id(server_id) ||
            !valid_remote_id(channel_id) ||
            limit == 0 || limit > kMaxMessagePage) {
            return error(
                DirectoryErrorCode::invalid_config,
                "message query is invalid");
        }

        const auto server = wide(server_id);
        const auto channel = wide(channel_id);
        if (server.empty() || channel.empty()) {
            return error(
                DirectoryErrorCode::invalid_config,
                "message query encoding failed");
        }

        std::wstring endpoint =
            L"/v1/messages?server_id=";
        endpoint += server;
        endpoint += L"&channel_id=";
        endpoint += channel;
        endpoint += L"&after=";
        endpoint += std::to_wstring(after);
        endpoint += L"&limit=";
        endpoint += std::to_wstring(limit);

        const auto response =
            successful_json(
                request_json(
                    service,
                    L"GET",
                    endpoint,
                    access_token,
                    nullptr));
        if (const auto* failure =
                std::get_if<DirectoryError>(
                    &response)) {
            return *failure;
        }

        const auto& json = std::get<Json>(response);
        const auto& items = json.at("messages");
        if (!items.is_array() ||
            items.size() > limit) {
            return error(
                DirectoryErrorCode::malformed_response,
                "message page is invalid");
        }

        DirectoryMessagePage page;
        page.next_after =
            json.at("next_after").get<std::uint64_t>();
        page.messages.reserve(items.size());
        std::uint64_t previous = after;
        for (const auto& item : items) {
            const auto parsed = parse_message(item);
            if (!parsed ||
                parsed->server_id != server_id ||
                parsed->channel_id != channel_id ||
                parsed->sequence <= previous) {
                return error(
                    DirectoryErrorCode::malformed_response,
                    "message page contains invalid data");
            }
            previous = parsed->sequence;
            page.messages.push_back(*parsed);
        }
        if (!page.messages.empty() &&
            page.next_after !=
                page.messages.back().sequence) {
            return error(
                DirectoryErrorCode::malformed_response,
                "message cursor is inconsistent");
        }
        if (page.messages.empty() &&
            page.next_after != after) {
            return error(
                DirectoryErrorCode::malformed_response,
                "empty message cursor is inconsistent");
        }
        return page;
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
        if (!valid_remote_id(server_id) ||
            !valid_remote_id(channel_id) ||
            content.empty() ||
            content.size() > kMaxMessageContentBytes) {
            return error(
                DirectoryErrorCode::invalid_config,
                "message is invalid");
        }

        const Json body{
            {"server_id", std::string{server_id}},
            {"channel_id", std::string{channel_id}},
            {"content", std::string{content}},
        };
        const auto response =
            successful_json(
                request_json(
                    service,
                    L"POST",
                    L"/v1/messages",
                    access_token,
                    &body));
        if (const auto* failure =
                std::get_if<DirectoryError>(
                    &response)) {
            return *failure;
        }

        const auto parsed =
            parse_message(std::get<Json>(response));
        if (!parsed ||
            parsed->server_id != server_id ||
            parsed->channel_id != channel_id ||
            parsed->content != content) {
            return error(
                DirectoryErrorCode::malformed_response,
                "sent message response is invalid");
        }
        return *parsed;
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
        const Json body{
            {"server_id",
             std::string{server_id}},
            {"channel_id",
             std::string{channel_id}},
        };
        const auto response =
            successful_json(
                request_json(
                    service,
                    L"POST",
                    L"/v1/rtc-token",
                    access_token,
                    &body));
        if (const auto* failure =
                std::get_if<DirectoryError>(
                    &response)) {
            return *failure;
        }

        const auto& json =
            std::get<Json>(response);
        RtcProvisioning provisioning;
        provisioning.token =
            json.at("token")
                .get<std::string>();
        provisioning.expires =
            json.at("expires")
                .get<std::int64_t>();
        provisioning.server_id =
            json.at("server_id")
                .get<std::string>();
        provisioning.channel_id =
            json.at("channel_id")
                .get<std::string>();
        provisioning.peer_id =
            json.at("peer_id")
                .get<std::string>();
        provisioning.signaling_url =
            json.at("signaling_url")
                .get<std::string>();
        provisioning.ice_servers =
            json.at("ice_servers")
                .get<std::vector<std::string>>();
        provisioning.max_room_peers =
            json.at("max_room_peers")
                .get<std::size_t>();
        provisioning.allow_insecure_signaling =
            json.value(
                "allow_insecure_signaling",
                false);
        provisioning.allow_no_turn =
            json.value(
                "allow_no_turn", false);

        if (provisioning.token.empty() ||
            provisioning.server_id.empty() ||
            provisioning.channel_id.empty() ||
            provisioning.peer_id.empty() ||
            provisioning.signaling_url.empty() ||
            provisioning.ice_servers.empty() ||
            provisioning.max_room_peers < 2 ||
            provisioning.max_room_peers > 5) {
            return error(
                DirectoryErrorCode::malformed_response,
                "RTC provisioning response is incomplete");
        }
        return provisioning;
    } catch (...) {
        return error(
            DirectoryErrorCode::malformed_response,
            "RTC provisioning response is invalid");
    }
}

} // namespace catro::platform::windows
