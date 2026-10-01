#pragma once

#include <catro/screen_runtime.hpp>

#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace winrt::Catro::implementation::server_view_detail {

struct Endpoint {
    std::string address;
    std::uint16_t port = 0;
};

inline std::optional<std::string> environment(char const* name) {
    const auto required = GetEnvironmentVariableA(name, nullptr, 0);
    if (required == 0) {
        return std::nullopt;
    }

    std::string value(static_cast<std::size_t>(required), '\0');
    const auto written = GetEnvironmentVariableA(name, value.data(), required);
    if (written == 0 || written >= required) {
        return std::nullopt;
    }
    value.resize(static_cast<std::size_t>(written));
    return value;
}

inline std::optional<Endpoint> parse_endpoint(std::string_view value) {
    const auto separator = value.rfind(':');
    if (separator == std::string_view::npos ||
        separator == 0 ||
        separator + 1 >= value.size()) {
        return std::nullopt;
    }

    unsigned port = 0;
    const auto port_text = value.substr(separator + 1);
    const auto [end, error] =
        std::from_chars(
            port_text.data(),
            port_text.data() + port_text.size(),
            port);
    if (error != std::errc{} ||
        end != port_text.data() + port_text.size() ||
        port == 0 ||
        port > 65535) {
        return std::nullopt;
    }
    return Endpoint{
        std::string(value.substr(0, separator)),
        static_cast<std::uint16_t>(port),
    };
}

struct DirectVoiceConfig {
    Endpoint bind;
    Endpoint peer;
};

inline DirectVoiceConfig direct_voice_config() {
    const bool slot_two =
        environment("CATRO_VOICE_SLOT").value_or("") == "2";
    DirectVoiceConfig config{
        .bind = {
            "127.0.0.1",
            static_cast<std::uint16_t>(
                slot_two ? 50001 : 50000)},
        .peer = {
            "127.0.0.1",
            static_cast<std::uint16_t>(
                slot_two ? 50000 : 50001)},
    };

    if (const auto value = environment("CATRO_VOICE_BIND")) {
        if (const auto parsed = parse_endpoint(*value)) {
            config.bind = *parsed;
        }
    }
    if (const auto value = environment("CATRO_VOICE_PEER")) {
        if (const auto parsed = parse_endpoint(*value)) {
            config.peer = *parsed;
        }
    }
    return config;
}

struct DirectVideoConfig {
    catro::transport::UdpEndpoint bind;
    catro::transport::UdpEndpoint peer;
};

inline DirectVideoConfig direct_video_config() {
    const auto explicit_slot = environment("CATRO_VIDEO_SLOT");
    const auto voice_slot = environment("CATRO_VOICE_SLOT");
    const bool slot_two =
        explicit_slot.value_or(voice_slot.value_or("")) == "2";

    DirectVideoConfig config{
        .bind = {
            "127.0.0.1",
            static_cast<std::uint16_t>(
                slot_two ? 55001 : 55000)},
        .peer = {
            "127.0.0.1",
            static_cast<std::uint16_t>(
                slot_two ? 55000 : 55001)},
    };

    if (const auto value = environment("CATRO_VIDEO_BIND")) {
        if (const auto parsed = parse_endpoint(*value)) {
            config.bind = {parsed->address, parsed->port};
        }
    }
    if (const auto value = environment("CATRO_VIDEO_PEER")) {
        if (const auto parsed = parse_endpoint(*value)) {
            config.peer = {parsed->address, parsed->port};
        }
    }
    return config;
}

} // namespace winrt::Catro::implementation::server_view_detail
