#pragma once

#include <cstdint>

namespace catro::platform::macos::detail {

inline constexpr std::uint32_t kInputCallbackElement = 0;
inline constexpr std::uint32_t kInputRenderElement = 1;

enum class MicrophoneAuthorization {
    authorized,
    not_determined,
    denied,
    restricted,
};

enum class MicrophoneAuthorizationAction {
    open,
    request,
    deny,
};

[[nodiscard]] constexpr MicrophoneAuthorizationAction authorization_action(MicrophoneAuthorization authorization) {
    switch (authorization) {
    case MicrophoneAuthorization::authorized:
        return MicrophoneAuthorizationAction::open;
    case MicrophoneAuthorization::not_determined:
        return MicrophoneAuthorizationAction::request;
    case MicrophoneAuthorization::denied:
    case MicrophoneAuthorization::restricted:
        return MicrophoneAuthorizationAction::deny;
    }
    return MicrophoneAuthorizationAction::deny;
}

} // namespace catro::platform::macos::detail
