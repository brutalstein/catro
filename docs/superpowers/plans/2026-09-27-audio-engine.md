# Audio Capture and Playback — Implementation Plan

Spec: `docs/superpowers/specs/2026-09-27-audio-engine-design.md`. Branch: `feature/audio-engine`.
Every task leaves MSVC and clang builds green and ends in one commit.

| # | Task | Files | Verification |
| --- | --- | --- | --- |
| 1 | Real-time primitives: `SpscRing`, `LevelMeter`, `ToneGenerator`, `MonitorPipe` | `core/audio/include/catro/audio/*.hpp`, `core/audio/src/*.cpp`, `tests/audio/primitives_test.cpp` | ring stress test, pipe priming/underrun/overrun/drift, meter math |
| 2 | Engine and platform contract: `AudioPlatform`, `AudioStream`, `AudioError`, `AudioEngine`, `AudioStatistics` | `core/audio/...engine*`, `tests/audio/engine_test.cpp` (fake platform) | state machine, failure propagation, latency estimate |
| 3 | Windows WASAPI backend | `platform/windows/include/.../audio_platform.hpp`, `platform/windows/src/audio/*.cpp` | builds `/W4 /WX` |
| 4 | `catro-audio-check` and Windows integration test | `tools/audio-check/*`, `tests/windows/audio_platform_test.cpp` | three modes run locally; test SKIPs without endpoints |
| 5 | Windows shell Audio page | `apps/windows/Catro/Diagnostics/*`, `apps/diagnostics/*` (shared audio view model) | msbuild clean, launch, session runs with meters |
| 6 | macOS AUHAL backend and permission | `platform/macos/src/audio/*.mm`, header | CI only (unverified locally) |
| 7 | macOS shell Audio page, `NSMicrophoneUsageDescription` plist | `apps/macos/*` | CI only |
| 8 | Docs, CI, scripts, final gates | `docs/*`, `README.md`, `.github/workflows/ci.yml` | fresh Windows gate |

Real-time rule checks in review: no `new`, `std::vector` growth, `std::string`, mutex, or logging reachable from
`on_captured` / `on_render`.
