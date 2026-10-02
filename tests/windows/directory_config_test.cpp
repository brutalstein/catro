#include <catro/platform/windows/directory_client.hpp>
#include <catch2/catch_test_macros.hpp>
#include <windows.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <variant>

namespace {

struct PackagedConfig {
    std::filesystem::path path;
    std::wstring previous_service;

    PackagedConfig() {
        std::wstring module(32768, L'\0');
        const auto length = GetModuleFileNameW(nullptr, module.data(), static_cast<DWORD>(module.size()));
        REQUIRE(length > 0);
        REQUIRE(length < module.size());
        module.resize(length);
        path = std::filesystem::path{module}.parent_path() / L"catro-network.json";
        // Never overwrite an operator's pre-existing network configuration.
        REQUIRE_FALSE(std::filesystem::exists(path));
        const auto size = GetEnvironmentVariableW(L"CATRO_SERVICE_URL", nullptr, 0);
        if (size) {
            previous_service.resize(size);
            const auto copied = GetEnvironmentVariableW(L"CATRO_SERVICE_URL", previous_service.data(), size);
            previous_service.resize(copied);
        }
        SetEnvironmentVariableW(L"CATRO_SERVICE_URL", nullptr);
    }

    ~PackagedConfig() {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
        SetEnvironmentVariableW(L"CATRO_SERVICE_URL",
                                previous_service.empty() ? nullptr : previous_service.c_str());
    }
};

} // namespace

TEST_CASE("Windows reads a packaged CRLF network config by byte length") {
    PackagedConfig config;
    {
        std::ofstream file(config.path, std::ios::binary);
        file << "{\r\n  \"api_base_url\": \"https://example.invalid\",\r\n"
                "  \"allow_insecure_http\": false\r\n}\r\n";
    }
    const auto loaded = catro::platform::windows::load_directory_service_config();
    REQUIRE(std::holds_alternative<catro::platform::windows::DirectoryServiceConfig>(loaded));
    const auto& service = std::get<catro::platform::windows::DirectoryServiceConfig>(loaded);
    CHECK(service.api_base_url == "https://example.invalid");
    CHECK_FALSE(service.allow_insecure_http);
}
