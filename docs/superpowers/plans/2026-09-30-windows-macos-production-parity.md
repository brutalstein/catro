# Windows/macOS Production Parity Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make Windows x64, macOS arm64, and macOS x64 satisfy one production feature, UX, performance, installer, and E2E contract while leaving only the final public tag/release action.

**Architecture:** Keep native WinUI 3 and SwiftUI/AppKit shells, extract the room and voice runtime contracts onto the portable C++ core, and implement only capture/codec/presentation/permission edges with native operating-system frameworks. Release automation remains non-publishing until cross-platform real-machine media and performance gates pass.

**Tech Stack:** C++20, CMake 3.28+, pinned libdatachannel/Mbed TLS/Opus/nlohmann_json/Catch2, WinUI 3/C++/WinRT/WASAPI/WGC/Media Foundation/D3D11, Swift 5/SwiftUI/AppKit/CoreAudio/ScreenCaptureKit/VideoToolbox/Metal or AVFoundation, Go signaling service, PowerShell/POSIX shell, GitHub Actions, Oracle Cloud Free Tier.

**Spec:** `docs/superpowers/specs/2026-09-30-windows-macos-production-parity-design.md`

## Global Constraints

- Preserve the existing Windows product behavior before adding macOS behavior.
- macOS minimum is 13.0; ship separate arm64 and x64 packages.
- No paid service, paid SDK, Electron, Qt, embedded browser, SFU, or server-side media processing.
- Production service is `https://51-170-186-61.sslip.io`.
- Maximum room size is five total participants; maximum screen publishers is one.
- Use native hardware media paths and bounded latest-edge queues; never move media work to a UI thread.
- Production rejects insecure signaling and no-TURN configurations unless an explicit engineering test opts in.
- Every task follows RED → GREEN, runs `graphify update .`, commits only owned files, and pushes `HEAD:feature/two-client-screen-stream`.
- Never commit `graphify-out/**` or `apps/windows/Catro/Assets/**`.
- Do not create a public tag or GitHub Release.

## Review Focus

- A platform runtime must stop cleanly while callbacks, queues, or permission changes are in flight; Tasks 1–5 add lifecycle tests.
- Malformed directory/RTC responses must not partially mutate identity, server, or room UI state; Tasks 2 and 6 pin strict parsing and generations.
- Audio/video queues must drop stale work rather than increase latency; Tasks 3–5 and 8 pin counters and recovery.
- Screen/audio permission denial or revocation must leave the current installation and room usable; Tasks 4–6 pin recovery.
- “Green CI” without real hardware must never flip the distribution-ready status; Tasks 8–11 enforce separate evidence gates.

---

### Task 1: Make RTC and room runtime portable

**Files:**
- Modify: `cmake/Dependencies.cmake`
- Modify: `CMakeLists.txt`
- Modify: `core/rtc/CMakeLists.txt`
- Move: `apps/room-runtime/windows/*` to `apps/room-runtime/*`
- Modify: `tests/CMakeLists.txt`
- Create: `tests/rtc/room_runtime_test.cpp`
- Modify: `.github/workflows/ci.yml`

**Interfaces:**
- Produces: the existing `CatroRoomRuntime*` C ABI on Windows and macOS.
- Consumes: `catro::rtc::RoomMeshTransport`, pinned libdatachannel, pinned Mbed TLS.

- [ ] **Step 1: Write the failing cross-platform room-runtime contract**

Add tests for invalid config, bounded peer count `1..4`, strict insecure/no-TURN flags, queue overflow
dropping the oldest complete datagram, stop waking blocked receivers, screen ownership snapshots, and
idempotent repeated start/stop.

- [ ] **Step 2: Run the macOS CI build to prove RED**

Run the workflow branch with the new test registered on both macOS runners.

Expected: macOS configure/build fails because RTC dependencies and room runtime are Windows-gated.

- [ ] **Step 3: Enable the pinned RTC dependency on Apple toolchains**

Keep the same Mbed TLS/libdatachannel options and `NO_MEDIA` data-channel transport. Apply
Windows-only definitions only on Windows.

- [ ] **Step 4: Move the room runtime to a platform-neutral target**

Retain the current C ABI names and queue/counter semantics. Do not add a second macOS implementation.

- [ ] **Step 5: Run focused and regression tests**

Run Windows and both macOS jobs; require room-runtime tests plus all pre-existing tests to pass.

- [ ] **Step 6: Refresh graph, commit, and push**

Commit: `refactor: make room transport portable`

### Task 2: Add shared directory values and macOS production session

**Files:**
- Create: `core/community/include/catro/community/directory.hpp`
- Create: `core/community/src/directory.cpp`
- Modify: `core/community/CMakeLists.txt`
- Modify: `platform/windows/include/catro/platform/windows/directory_client.hpp`
- Modify: `platform/windows/src/directory_client.cpp`
- Create: `platform/macos/include/catro/platform/macos/directory_client.hpp`
- Create: `platform/macos/src/directory_client.mm`
- Modify: `platform/macos/CMakeLists.txt`
- Create: `tests/community/directory_test.cpp`
- Create: `tests/macos/directory_client_test.mm`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces: shared bounded directory DTOs/validation and a macOS HTTPS/Keychain client matching the Windows operation set.
- Consumes: existing signaling `/v1/*` contracts and `community::LocalState`.

- [ ] **Step 1: Write failing shared validation tests**

Cover every DTO bound, malformed JSON, missing fields, duplicate IDs, invalid roles, invalid service
URLs, and over-bound text/join-request data.

- [ ] **Step 2: Write failing macOS credential and endpoint tests**

Use temporary Keychain service/account names and an injected HTTP transport. Assert stable credential
reuse, no plaintext credential file, HTTPS-only production, and actionable status mapping.

- [ ] **Step 3: Extract shared directory values without changing Windows behavior**

Move data declarations and pure validation only; keep WinHTTP/DPAPI native.

- [ ] **Step 4: Implement the macOS adapter**

Use Foundation networking and Security.framework. Keep all requests cancellable and off the main and
media threads.

- [ ] **Step 5: Run service, Windows, and macOS tests**

Require existing signaling tests plus new client contracts on both macOS architectures.

- [ ] **Step 6: Refresh graph, commit, and push**

Commit: `feat: add macOS directory session`

### Task 3: Extract portable production voice runtime

**Files:**
- Create: `apps/voice-runtime/common/CMakeLists.txt`
- Create: `apps/voice-runtime/common/include/catro/room_voice_runtime.hpp`
- Create: `apps/voice-runtime/common/src/room_voice_runtime.cpp`
- Modify: `apps/voice-runtime/windows/src/voice_runtime.cpp`
- Modify: `apps/voice-runtime/windows/CMakeLists.txt`
- Create: `apps/voice-runtime/macos/CMakeLists.txt`
- Create: `apps/voice-runtime/macos/src/voice_runtime.mm`
- Modify: `CMakeLists.txt`
- Create: `tests/voice/room_voice_runtime_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces: one portable room voice loop and the existing `CatroVoiceRuntime*` C ABI on both platforms.
- Consumes: `audio::AudioPlatform`, `VoicePipeline`, and `CatroRoomRuntimeHandle`.

- [ ] **Step 1: Write a failing fake-audio/fake-room runtime test**

Assert duplex encode/send/receive/mix, mute, deafen, five-stream mixing, device failure, room failure,
late packet recovery, stop latency, and bounded drain/catch-up behavior.

- [ ] **Step 2: Prove RED on macOS**

Expected: no macOS voice-runtime target exists.

- [ ] **Step 3: Extract the production room loop**

Keep direct UDP in the Windows engineering adapter. The shared loop accepts injected audio and room
operations and owns no OS type.

- [ ] **Step 4: Add the CoreAudio-backed macOS C ABI adapter**

Use the existing `CoreAudioPlatform`; expose identical states, controls, snapshots, and errors.

- [ ] **Step 5: Run focused, sanitizer, and platform regressions**

Require the new fake test on all platforms and existing Windows runtime tests unchanged.

- [ ] **Step 6: Refresh graph, commit, and push**

Commit: `feat: add production voice on macOS`

### Task 4: Add macOS capture, H.264 codec, and presentation primitives

**Files:**
- Create: `platform/macos/include/catro/platform/macos/screen_capture.hpp`
- Create: `platform/macos/src/screen_capture.mm`
- Create: `platform/macos/include/catro/platform/macos/video_encoder.hpp`
- Create: `platform/macos/src/video_encoder.mm`
- Create: `platform/macos/include/catro/platform/macos/video_decoder.hpp`
- Create: `platform/macos/src/video_decoder.mm`
- Create: `platform/macos/include/catro/platform/macos/video_presenter.hpp`
- Create: `platform/macos/src/video_presenter.mm`
- Modify: `platform/macos/CMakeLists.txt`
- Modify: `apps/macos/Info.plist.in`
- Create: `tests/macos/screen_capture_test.mm`
- Create: `tests/macos/video_encoder_test.mm`
- Create: `tests/macos/video_decoder_test.mm`
- Create: `tests/macos/video_presenter_test.mm`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces: source enumeration, one-frame capture mailbox, low-latency H.264 access units, decoded native frames, and GPU-backed presentation.
- Consumes: ScreenCaptureKit, CoreMedia, CoreVideo, VideoToolbox, Metal/AppKit or AVFoundation.

- [ ] **Step 1: Write failing validation/lifecycle tests with injected native adapters**

Cover empty/closed sources, permission denial/revocation, resize, aspect/even geometry, invalid bitrate
or frame rate, hardware-required mode, keyframe output, malformed access units, stale-frame drops,
surface replacement, and stop while callbacks are active.

- [ ] **Step 2: Prove RED on both macOS runners**

Expected: native classes/targets are undefined.

- [ ] **Step 3: Implement lazy source enumeration and capture**

Enumeration opens no persistent stream. Capture publishes only the newest frame and performs no
blocking work in ScreenCaptureKit callbacks.

- [ ] **Step 4: Implement VideoToolbox encode/decode**

Request low latency and hardware acceleration, expose whether hardware was actually selected, and
fail the production path rather than silently hiding a software fallback.

- [ ] **Step 5: Implement presentation and permission recovery**

Create presentation work only while visible; detach native surfaces before teardown.

- [ ] **Step 6: Run native tests and collect diagnostic artifacts**

Hosted runners validate contracts only; mark hardware capability output as diagnostic.

- [ ] **Step 7: Refresh graph, commit, and push**

Commit: `feat: add macOS hardware video primitives`

### Task 5: Add portable screen transport and macOS full-duplex screen runtime

**Files:**
- Create: `apps/screen-runtime/common/CMakeLists.txt`
- Create: `apps/screen-runtime/common/include/catro/screen_transport_runtime.hpp`
- Create: `apps/screen-runtime/common/src/screen_transport_runtime.cpp`
- Modify: `apps/screen-runtime/windows/screen_runtime.cpp`
- Modify: `apps/screen-runtime/windows/CMakeLists.txt`
- Create: `apps/screen-runtime/macos/CMakeLists.txt`
- Create: `apps/screen-runtime/macos/include/catro/macos_screen_runtime.h`
- Create: `apps/screen-runtime/macos/src/macos_screen_runtime.mm`
- Modify: `CMakeLists.txt`
- Create: `tests/video/screen_transport_runtime_test.cpp`
- Create: `tests/macos/screen_runtime_test.mm`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces: shared room video/stream-audio packet flow and a macOS runtime supporting listen, share, stop-share, watch, and stop.
- Consumes: Task 1 room runtime and Task 4 native media primitives.

- [ ] **Step 1: Write failing shared transport tests**

Cover RTP packetization/reassembly, stream reset/keyframe recovery, owner filtering, bounded receive
queues, watch-disabled decode suppression, malformed packets, sender backpressure, and restart.

- [ ] **Step 2: Refactor Windows onto the shared transport without behavior changes**

Keep current public `WindowsScreenShareRuntime` and snapshot semantics.

- [ ] **Step 3: Run full Windows tests**

Expected: all existing Windows screen tests pass before macOS implementation begins.

- [ ] **Step 4: Implement macOS capture/encode/send and receive/decode/present**

Use one room membership, one encode per source, and opt-in remote decode/presentation.

- [ ] **Step 5: Add ScreenCaptureKit stream audio when supported**

Reuse bounded Opus framing. Report unsupported source/version behavior before share starts.

- [ ] **Step 6: Run platform and sanitizer tests**

Require no queue/counter regressions and deterministic stop/restart behavior.

- [ ] **Step 7: Refresh graph, commit, and push**

Commit: `feat: add macOS screen sharing`

### Task 6: Build the macOS product shell and equivalent settings

**Files:**
- Modify: `apps/macos/Catro/CatroApp.swift`
- Create: `apps/macos/Catro/Product/AppModel.swift`
- Create: `apps/macos/Catro/Product/ServerWorkspace.swift`
- Create: `apps/macos/Catro/Product/ChannelSidebar.swift`
- Create: `apps/macos/Catro/Product/MemberSidebar.swift`
- Create: `apps/macos/Catro/Product/VoiceControls.swift`
- Create: `apps/macos/Catro/Product/SourcePicker.swift`
- Create: `apps/macos/Catro/Product/StreamViewer.swift`
- Create: `apps/macos/Catro/Product/SettingsView.swift`
- Create: `apps/macos/Catro/Bridge/CatroProductBridge.h`
- Create: `apps/macos/Catro/Bridge/CatroProductBridge.mm`
- Modify: `apps/macos/CMakeLists.txt`
- Create: `tests/macos/product_bridge_test.mm`
- Create: `tests/ui/macos_ui_contract_test.py`

**Interfaces:**
- Produces: the complete macOS server/text/voice/share/watch/settings experience.
- Consumes: Tasks 2, 3, and 5 through immutable Objective-C value objects and command methods.

- [ ] **Step 1: Write failing product-bridge state-machine tests**

Pin bootstrap generations, server switching, invite/request/approval flows, bounded polling, voice
join/leave, share ownership, permission errors, watch state, and teardown.

- [ ] **Step 2: Write failing SwiftUI source/accessibility contract tests**

Require native split navigation, virtualized lists, labels/help text, keyboard commands, reduced
motion, no stock capture picker, and no media work in view bodies.

- [ ] **Step 3: Implement the narrow Objective-C++ bridge**

No C++ or native media object crosses into Swift. All callback delivery is marshalled onto the main
actor after immutable snapshot creation.

- [ ] **Step 4: Implement the product workspace and settings**

Match the Windows feature/state contract while using native SwiftUI controls and macOS conventions.

- [ ] **Step 5: Run arm64/x64 builds and UI contracts**

Require clean launch with denied microphone/screen permissions and with no network.

- [ ] **Step 6: Refresh graph, commit, and push**

Commit: `feat: add macOS production workspace`

### Task 7: Upgrade both native source pickers and accessibility parity

**Files:**
- Modify: `apps/windows/Catro/Server/ServerView.xaml`
- Modify: `apps/windows/Catro/Server/ServerView.xaml.cpp`
- Modify: `apps/windows/Catro/Server/ServerView.xaml.h`
- Create: `apps/windows/Catro/Server/ShareSourceItem.*`
- Modify: `apps/windows/Catro/Settings/SettingsView.xaml`
- Modify: `apps/windows/Catro/Settings/SettingsView.xaml.cpp`
- Modify: macOS product files from Task 6
- Modify: `tests/apps/ui_policy_test.cpp`
- Create: `tests/ui/accessibility-contract.ps1`
- Create: `tests/ui/accessibility-contract.sh`

**Interfaces:**
- Produces: equivalent card/grid source selection, quality/device settings, keyboard navigation, and screen-reader semantics.
- Consumes: platform source enumeration and bounded thumbnail providers.

- [ ] **Step 1: Write failing UI policy/accessibility tests**

Require lazy thumbnails, cancellation, visible source labels, dimensions/kind, selected state,
default/cancel actions, complete automation names, focus order, high contrast, text scaling, and no
heavy backdrop/shadow/bitmap-decode regressions.

- [ ] **Step 2: Implement the Windows source grid and real settings controls**

Replace the text-only combo box. Preserve current validation and share configuration.

- [ ] **Step 3: Align macOS source/settings behavior**

Use equivalent labels, defaults, error states, and quality ceilings.

- [ ] **Step 4: Run screenshot, keyboard, Narrator, and VoiceOver checks**

Automated contracts may assist, but recorded native assistive-technology runs remain required.

- [ ] **Step 5: Refresh graph, commit, and push**

Commit: `feat: align native gaming UX`

### Task 8: Add deterministic E2E and performance harnesses

**Files:**
- Create: `tests/e2e/room-scenario.*`
- Create: `tests/e2e/network-impairment.*`
- Create: `tests/performance/catro-perf.ps1`
- Create: `tests/performance/catro-perf.sh`
- Create: `tests/performance/schema.json`
- Create: `scripts/validate-performance.ps1`
- Modify: `.github/workflows/ci.yml`
- Create: `docs/validation/cross-platform-media-acceptance.md`

**Interfaces:**
- Produces: machine-readable room/media counters, timing samples, process-resource samples, and pass/fail validation against the spec budgets.
- Consumes: packaged applications, Oracle service, native profilers/counters, and explicit test identities.

- [ ] **Step 1: Write failing fixture tests for the performance validator**

Cover exact threshold boundaries, missing samples, clock disorder, wrong commit/platform, queue
growth, software-codec fallback, and fabricated/empty evidence.

- [ ] **Step 2: Implement a standard-library-only validator**

It reads JSON evidence and emits actionable failures; it never invents unavailable measurements.

- [ ] **Step 3: Add deterministic simulated room/E2E scenarios**

Exercise two/three/five peers, sixth rejection, ownership, malformed media, loss/jitter/interruption,
service disconnect, and rejoin without physical devices.

- [ ] **Step 4: Add native measurement launchers**

Record exact hardware/software/network metadata and raw counters. Do not mark hosted CI as hardware
acceptance.

- [ ] **Step 5: Run fixture tests and CI simulations**

- [ ] **Step 6: Refresh graph, commit, and push**

Commit: `test: add parity and performance gates`

### Task 9: Complete stable packages and non-publishing release workflow

**Files:**
- Modify: `scripts/package-macos.sh`
- Modify: `scripts/install-macos.sh`
- Create: `scripts/uninstall-windows.ps1`
- Create: `scripts/uninstall-macos.sh`
- Create: `scripts/validate-release.ps1`
- Create: `tests/release/release-validation-test.ps1`
- Create: `tests/release/release-workflow-test.ps1`
- Modify: installer tests
- Create: `.github/workflows/release.yml`
- Modify: `.github/workflows/ci.yml`

**Interfaces:**
- Produces: stable Windows/macOS assets, uninstall commands, exact asset/checksum validation, and a manual non-publishing assembly rehearsal.
- Consumes: parity-complete packaged apps and the production endpoint.

- [ ] **Step 1: Write failing stable-asset and workflow contract tests**

Require exact asset names, tag/version agreement, least privilege, producer dependencies, manual
dispatch without publication, draft-only tag publication, re-download validation, and no marketplace
release action.

- [ ] **Step 2: Remove macOS preview naming only after Tasks 1–8 are green**

Update package README text to the observed production contract.

- [ ] **Step 3: Add uninstall and final package security checks**

Preserve user state by default and scan archives for secrets/private keys/tokens.

- [ ] **Step 4: Implement and test release assembly**

Manual dispatch uploads one assembled workflow artifact and creates no release.

- [ ] **Step 5: Refresh graph, commit, and push**

Commit: `feat: assemble parity release artifacts`

### Task 10: Run fresh Oracle and real-machine acceptance

**Files:**
- Modify: `docs/validation/oracle-free-production-acceptance.md`
- Modify: `docs/validation/cross-platform-media-acceptance.md`
- Add: `docs/validation/evidence/<commit>/*` sanitized summaries/manifests only

**Interfaces:**
- Produces: observed acceptance evidence bound to one commit and package checksum set.
- Consumes: Oracle VM `51.170.186.61`, packaged Windows/macOS clients, at least two networks/cities, and required hardware.

- [ ] **Step 1: Verify/deploy the exact commit to Oracle**

Run install/update/verify, HTTPS/WSS, TURN allocation, exposure, backup, offline restore, and reboot
checks using `C:\Users\cenke\Downloads\ssh1.key`.

- [ ] **Step 2: Run the cross-platform pair matrix**

Complete Windows/Windows, Windows/arm64, Windows/x64, and arm64/x64 where hardware is available.

- [ ] **Step 3: Run capacity, forced TURN, failure, and 30-minute soak scenarios**

- [ ] **Step 4: Capture performance and assistive-technology evidence**

Reject the gate if any required platform/hardware/network observation is missing.

- [ ] **Step 5: Commit and push only sanitized evidence/manifests**

Commit: `test: record production acceptance`

### Task 11: Rehearse final distribution and freeze

**Files:**
- Modify: `README.md`
- Modify: `deploy/oracle-free/README.md`
- Modify: release/validation docs as required by observed results

**Interfaces:**
- Produces: a reviewed commit for which only the version tag and public release publication remain.
- Consumes: all prior task outputs and green gates.

- [ ] **Step 1: Update documentation to exact observed behavior**

Include install/download/uninstall, Gatekeeper, permissions, supported architectures, service status,
performance wording, known limits, and recovery.

- [ ] **Step 2: Run the complete local and CI matrix at the candidate commit**

- [ ] **Step 3: Run manual `workflow_dispatch` assembly**

Download the assembled artifact, re-run checksum/package/install validators on clean Windows and both
Mac architectures, and confirm no GitHub Release exists.

- [ ] **Step 4: Verify repository and release freeze**

Require clean owned files, exact pushed SHA, no secrets, no unresolved acceptance item, and no
preview wording/assets.

- [ ] **Step 5: Commit and push the readiness freeze**

Commit: `docs: mark distribution candidate ready`

- [ ] **Step 6: Stop before publication**

Do not create a tag or public release. Report that only the final approved distribution action
remains.

