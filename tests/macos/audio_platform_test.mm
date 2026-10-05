#include <catro/platform/macos/audio_platform.hpp>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("macOS CoreAudio reports a missing saved endpoint unavailable") {
    catro::platform::macos::CoreAudioPlatform platform;
    const catro::capabilities::AudioEndpointId missing{
        "coreaudio:dev.catro.missing-device:output",
        catro::capabilities::IdentityScope::persistent};

    CHECK_FALSE(platform.device_available(
        missing,
        catro::audio::DeviceDirection::render));
}
