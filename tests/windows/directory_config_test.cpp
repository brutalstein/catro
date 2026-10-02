#include <catro/platform/windows/directory_client.hpp>

#include <catch2/catch_test_macros.hpp>

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <variant>

TEST_CASE("Packaged Windows network config with CRLF line endings loads") {
    // The packaging script writes catro-network.json with Windows line endings next to the exe.
    REQUIRE(SetEnvironmentVariableW(L"CATRO_SERVICE_URL", nullptr) != FALSE);

    std::wstring module_path(32768, L'\0');
    const auto length = GetModuleFileNameW(
        nullptr, module_path.data(), static_cast<DWORD>(module_path.size()));
    REQUIRE(length > 0);
    module_path.resize(length);
    const auto config_path =
        std::filesystem::path{module_path}.parent_path() / L"catro-network.json";

    {
        std::ofstream output(config_path, std::ios::binary | std::ios::trunc);
        output << "{\r\n"
                  "    \"api_base_url\":  \"https://catro.example.test\",\r\n"
                  "    \"allow_insecure_http\":  false\r\n"
                  "}";
    }

    const auto result = catro::platform::windows::load_directory_service_config();
    std::filesystem::remove(config_path);

    const auto* config = std::get_if<catro::community::DirectoryServiceConfig>(&result);
    REQUIRE(config != nullptr);
    CHECK(config->api_base_url == "https://catro.example.test");
    CHECK_FALSE(config->allow_insecure_http);
}
