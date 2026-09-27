#include "windows_translation.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

using namespace catro::capabilities;
using namespace catro::platform::windows;

namespace {

constexpr std::string_view kProbe = "windows.test.v1";
constexpr NativeLuid kDiscrete{0, 0x1111};
constexpr NativeLuid kIntegrated{0, 0x2222};

template <class T>
const T& require_known(const Observed<T>& fact) {
    REQUIRE(fact.knowledge() == Knowledge::known);
    REQUIRE(fact.value());
    return *fact.value();
}

template <class T>
void require_unknown(const Observed<T>& fact, IssueCode issue) {
    REQUIRE(fact.knowledge() == Knowledge::unknown);
    REQUIRE_FALSE(fact.value());
    REQUIRE(fact.provenance().issue == issue);
}

bool has_issue(const std::vector<ProbeIssue>& issues, IssueCode code) {
    return std::ranges::any_of(issues, [&](const ProbeIssue& issue) { return issue.code == code; });
}

NativeAdapter adapter(NativeLuid luid, std::optional<bool> integrated) {
    return {.luid = luid, .vendor_id = 0x10DE, .device_id = 0x2684, .description = "Adapter",
            .integrated = integrated, .dedicated_memory = 1ULL << 33, .direct3d11 = true, .direct3d12 = true};
}

NativeDisplay display(NativeLuid luid, std::uint32_t target, bool primary) {
    return {.adapter = luid, .target_id = target, .width = 3840, .height = 2160, .refresh_numerator = 144000,
            .refresh_denominator = 1000, .dpi = 144, .primary = primary, .hdr_supported = true, .hdr_enabled = false,
            .bits_per_channel = std::uint8_t{10}};
}

NativeEncoder hardware(Codec codec, NativeLuid luid) {
    return {.codec = codec, .clsid = "{966F107C-8EA2-425D-A8A8-6C8E3E5A0B5E}", .hardware = true, .adapter = luid,
            .name = "Hardware encoder", .inputs = {PixelFormat::nv12, PixelFormat::bgra8, PixelFormat::p010,
                                                   PixelFormat::nv12}};
}

NativeEncoder software() {
    return {.codec = Codec::h264, .clsid = "{6CA50344-051A-4DED-9779-A43305165E35}", .name = "H.264 software",
            .inputs = {PixelFormat::nv12}};
}

} // namespace

TEST_CASE("Windows identifiers are stable, typed, and scoped") {
    CHECK(gpu_id({1, 0xABC}) == GpuId{"luid:00000001:00000abc", IdentityScope::os_session});
    CHECK(display_id({0, 0xABC}, 7) == DisplayId{"display:00000000:00000abc:7", IdentityScope::os_session});
    CHECK(encoder_id(software()) ==
          EncoderId{"mft:h264:software:{6ca50344-051a-4ded-9779-a43305165e35}", IdentityScope::persistent});
    const auto id = encoder_id(hardware(Codec::hevc, kDiscrete));
    CHECK(id.value == "mft:hevc:hardware:{966f107c-8ea2-425d-a8a8-6c8e3e5a0b5e}:00000000:00001111");
    CHECK(id.scope == IdentityScope::os_session);
}

TEST_CASE("adapter kinds come from DXGI and DXCore, never from the vendor") {
    std::vector<ProbeIssue> issues;
    auto warp = adapter({0, 3}, std::nullopt);
    warp.software = true;
    auto egpu = adapter({0, 4}, false);
    egpu.detachable = true;
    const auto facts = translate_gpu_display(
        {.adapters = std::vector{adapter(kDiscrete, false), adapter(kIntegrated, true), warp, egpu,
                                 adapter({0, 5}, std::nullopt), adapter(kDiscrete, false)}},
        kProbe, issues);

    // The repeated LUID is dropped.
    REQUIRE(facts.gpus.size() == 5);
    CHECK(require_known(facts.gpus[0].kind) == GpuKind::discrete);
    CHECK(require_known(facts.gpus[1].kind) == GpuKind::integrated);
    CHECK(require_known(facts.gpus[2].kind) == GpuKind::software);
    CHECK(require_known(facts.gpus[3].kind) == GpuKind::external);
    require_unknown(facts.gpus[4].kind, IssueCode::not_reported);

    CHECK(require_known(facts.gpus[0].graphics_apis) ==
          std::vector{GraphicsApi::direct3d11, GraphicsApi::direct3d12});
    CHECK(require_known(facts.gpus[0].dedicated_memory) == Bytes{1ULL << 33});
    require_unknown(facts.gpus[0].unified_memory, IssueCode::not_reported);
    require_unknown(facts.gpus[0].preferred_for_high_performance, IssueCode::api_unavailable);
}

TEST_CASE("the active display mode is exact and scaled by effective DPI") {
    std::vector<ProbeIssue> issues;
    auto unscaled = display(kDiscrete, 2, false);
    unscaled.dpi.reset();
    unscaled.refresh_numerator = 60000;
    unscaled.refresh_denominator = 1001;
    const auto facts = translate_gpu_display(
        {.adapters = std::vector{adapter(kDiscrete, false)},
         .displays = std::vector{display(kDiscrete, 1, true), unscaled, display({0, 0x9999}, 3, false)}},
        kProbe, issues);

    REQUIRE(facts.displays.size() == 3);
    CHECK(require_known(facts.displays[0].gpu) == gpu_id(kDiscrete));
    require_unknown(facts.displays[2].gpu, IssueCode::relationship_unprovable);
    // Integer-Hz mode lists would misstate fractional rates.
    require_unknown(facts.displays[0].modes, IssueCode::not_reported);
    CHECK(facts.displays[0].hdr.status == Support::supported);

    const auto& scaled = require_known(facts.display_states[0].active_mode);
    CHECK(scaled.pixels == Dimensions{3840, 2160});
    CHECK(scaled.logical == Dimensions{2560, 1440});
    CHECK(scaled.refresh_rate == Rational{144, 1});
    CHECK(require_known(facts.display_states[0].scale) == Rational{3, 2});
    CHECK(require_known(facts.display_states[0].primary));
    CHECK_FALSE(require_known(facts.display_states[0].hdr_enabled));

    // Without DPI the pixels and rate stay exact, but the logical size is degraded evidence.
    const auto& fallback = facts.display_states[1].active_mode;
    CHECK(require_known(fallback).refresh_rate == Rational{60000, 1001});
    CHECK(fallback.provenance().issue == IssueCode::not_reported);
    require_unknown(facts.display_states[1].scale, IssueCode::not_reported);
}

TEST_CASE("cloned primary targets make the primary display unprovable") {
    std::vector<ProbeIssue> issues;
    const auto facts = translate_gpu_display(
        {.displays = std::vector{display(kDiscrete, 1, true), display(kDiscrete, 2, true), display(kDiscrete, 3, false)}},
        kProbe, issues);
    require_unknown(facts.display_states[0].primary, IssueCode::relationship_unprovable);
    require_unknown(facts.display_states[1].primary, IssueCode::relationship_unprovable);
    CHECK_FALSE(require_known(facts.display_states[2].primary));
    // Without an adapter enumeration no display claims a GPU.
    require_unknown(facts.displays[0].gpu, IssueCode::relationship_unprovable);
}

TEST_CASE("capture paths are granted only when usable") {
    std::vector<ProbeIssue> issues;
    const auto facts = translate_gpu_display(
        {.capture = {.graphics_capture = true, .desktop_duplication = true, .desktop_duplication_hdr = true}}, kProbe,
        issues);
    REQUIRE(facts.capture_paths.size() == 3);
    CHECK(facts.capture_paths[0].id.value == kGraphicsCaptureDisplay);
    CHECK(require_known(facts.capture_paths[2].output_formats) ==
          std::vector{PixelFormat::bgra8, PixelFormat::rgba16f});
    CHECK(facts.capture_permissions.size() == 3);
    for (const auto& path : facts.capture_paths) {
        require_unknown(path.gpu, IssueCode::relationship_unprovable);
    }

    std::vector<ProbeIssue> headless_issues;
    const auto headless = translate_gpu_display({.capture = {.graphics_capture = false}}, kProbe, headless_issues);
    CHECK(headless.capture_paths[0].support.status == Support::unsupported);
    CHECK(headless.capture_paths[2].support.status == Support::unknown);
    CHECK(headless.capture_paths[2].output_formats.knowledge() == Knowledge::unavailable);
    CHECK(headless.capture_permissions.empty());
    CHECK(has_issue(headless_issues, IssueCode::not_applicable));
}

TEST_CASE("encoder modes come only from planar YUV inputs") {
    std::vector<ProbeIssue> issues;
    const auto facts = translate_encoders({hardware(Codec::hevc, kDiscrete)}, {}, kProbe, issues);
    REQUIRE(facts.encoders.size() == 1);
    const auto& encoder = facts.encoders[0];
    CHECK(encoder.backend == EncoderBackend::media_foundation);
    CHECK(require_known(encoder.gpu) == gpu_id(kDiscrete));
    // BGRA fixes no chroma layout; the repeated NV12 adds nothing.
    REQUIRE(encoder.modes.size() == 2);
    CHECK(encoder.modes[0].input_format == PixelFormat::nv12);
    CHECK(require_known(encoder.modes[0].bit_depth) == 8);
    CHECK(require_known(encoder.modes[0].hdr) == HdrMode::sdr);
    CHECK(encoder.modes[1].input_format == PixelFormat::p010);
    CHECK(require_known(encoder.modes[1].bit_depth) == 10);
    require_unknown(encoder.modes[1].hdr, IssueCode::not_reported);
}

TEST_CASE("duplication transfers follow the adapters outputs are attached to") {
    std::vector<ProbeIssue> issues;
    const auto facts = translate_encoders({hardware(Codec::h264, kDiscrete), software()}, {kDiscrete, kIntegrated},
                                          kProbe, issues);
    REQUIRE(facts.encoders.size() == 2);
    // Hardware: two capture transfers plus one duplication transfer per output adapter.
    REQUIRE(facts.transfer_paths.size() == 4 + 3);

    const auto& capture = facts.transfer_paths[0];
    CHECK(capture.transfer == TransferKind::unknown);
    CHECK(capture.destination_gpu == gpu_id(kDiscrete));

    const auto same = std::ranges::find_if(facts.transfer_paths, [](const TransferPathCapability& path) {
        return path.source_gpu == gpu_id(kDiscrete);
    });
    REQUIRE(same != facts.transfer_paths.end());
    CHECK(same->transfer == TransferKind::same_adapter_copy);
    CHECK(same->evidence.status == Support::supported);
    const auto cross = std::ranges::find_if(facts.transfer_paths, [](const TransferPathCapability& path) {
        return path.source_gpu == gpu_id(kIntegrated);
    });
    REQUIRE(cross != facts.transfer_paths.end());
    CHECK(cross->transfer == TransferKind::cross_adapter_copy);
    CHECK(cross->destination_gpu == gpu_id(kDiscrete));

    // The software encoder always takes CPU-staged frames from every path.
    for (std::size_t index = 4; index < facts.transfer_paths.size(); ++index) {
        CHECK(facts.transfer_paths[index].transfer == TransferKind::cpu_staging);
        CHECK_FALSE(facts.transfer_paths[index].destination_gpu);
    }
}

TEST_CASE("encoders without a usable identifier are dropped with an issue") {
    std::vector<ProbeIssue> issues;
    auto empty = software();
    empty.clsid.clear();
    auto spaced = software();
    spaced.clsid = "{not a clsid}";
    const auto facts = translate_encoders({empty, spaced, software(), software()}, {}, kProbe, issues);
    REQUIRE(facts.encoders.size() == 1);
    CHECK(has_issue(issues, IssueCode::not_reported));
}

TEST_CASE("audio endpoints report measured defaults and explicit gaps") {
    std::vector<ProbeIssue> issues;
    const std::vector endpoints{
        NativeAudioEndpoint{.id = "{0.0.0.00000000}.{speakers}", .direction = AudioDirection::output,
                            .name = "Speakers", .channels = 2, .sample_rate_hz = 48000,
                            .sample_format = SampleFormat::pcm_f32,
                            .default_roles = std::vector{AudioRole::multimedia, AudioRole::console,
                                                         AudioRole::console}},
        NativeAudioEndpoint{.id = "{0.0.1.00000000}.{mic}", .direction = AudioDirection::input,
                            .state = NativeEndpointState::unplugged, .channels = 0},
        NativeAudioEndpoint{.id = "has space", .direction = AudioDirection::input},
        NativeAudioEndpoint{.id = "{0.0.0.00000000}.{speakers}", .direction = AudioDirection::output},
    };
    const auto facts = translate_audio(endpoints, kProbe, issues);

    REQUIRE(facts.endpoints.size() == 2);
    CHECK(facts.endpoints[0].id ==
          AudioEndpointId{"mmdevice:{0.0.0.00000000}.{speakers}", IdentityScope::persistent});
    // Roles are sorted and unique.
    CHECK(require_known(facts.states[0].default_roles) == std::vector{AudioRole::console, AudioRole::multimedia});
    CHECK(facts.states[0].default_roles.provenance().method == EvidenceMethod::measured);

    require_unknown(facts.endpoints[1].channels, IssueCode::not_reported);
    CHECK_FALSE(require_known(facts.states[1].active));
    require_unknown(facts.states[1].default_roles, IssueCode::os_failure);
    CHECK(has_issue(issues, IssueCode::not_reported));
}
