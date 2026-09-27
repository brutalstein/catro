#include <catch2/catch_test_macros.hpp>

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

namespace {

std::string read(std::filesystem::path path) {
    std::ifstream input(path, std::ios::binary);
    REQUIRE(input.good());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

} // namespace

TEST_CASE("product shell XAML stays on the low-cost composition path") {
    const std::filesystem::path root = CATRO_WINDOWS_XAML_DIR;
    const std::array files{
        "App.xaml",
        "MainWindow.xaml",
        "Server/ServerView.xaml",
        "Settings/SettingsView.xaml",
    };
    const std::array<std::string_view, 8> forbidden{
        "MicaBackdrop",
        "DesktopAcrylicBackdrop",
        "AcrylicBrush",
        "ThemeShadow",
        "DropShadow",
        "<Storyboard",
        "DoubleAnimation",
        "ColorAnimation",
    };

    for (const auto* relative : files) {
        INFO(relative);
        const auto text = read(root / relative);
        CHECK(text.size() < 64U * 1024U);
        for (const auto token : forbidden) {
            CHECK(text.find(token) == std::string::npos);
        }
        // Product decoration stays vector/text based; bitmap content must be justified explicitly.
        CHECK(text.find("<Image") == std::string::npos);
    }
}

TEST_CASE("product shell palette does not regress to a blue accent") {
    const auto app = read(std::filesystem::path(CATRO_WINDOWS_XAML_DIR) / "App.xaml");
    CHECK(app.find("CatroAccentBrush") != std::string::npos);
    CHECK(app.find("#0078D4") == std::string::npos);
    CHECK(app.find("#0067C0") == std::string::npos);
    CHECK(app.find("#60CDFF") == std::string::npos);
}
