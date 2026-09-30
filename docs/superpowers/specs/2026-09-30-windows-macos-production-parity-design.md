# Windows/macOS production parity and distribution readiness — design

Date: 2026-09-30  
Status: approved for autonomous execution by the user

## 1. Goal

Bring Catro to one honest production contract on Windows x64, macOS arm64, and macOS x64:

- the same identity, server, invite, approval, roster, text, and voice-room behavior;
- simultaneous voice for two through five participants;
- native screen/window sharing, stream audio where the operating system permits it, and remote viewing;
- equivalent source-picker, settings, error, permission, keyboard, and accessibility behavior;
- hardware-accelerated media paths with bounded queues and measured game impact;
- transactional install, upgrade, rollback, and uninstall instructions;
- reproducible CI plus real-machine, different-network acceptance evidence;
- no public tag or release until every release gate in this design passes.

“Parity” means equivalent user capability and acceptance behavior, not identical framework code or
pixel-for-pixel controls. Native WinUI 3 and SwiftUI/AppKit remain the product shells.

## 2. Verified baseline

### Windows

The Windows application already implements:

- durable local identity and personal server state;
- authenticated directory bootstrap and server synchronization;
- direct invites and approval-based Server Code joins;
- bounded member and text-channel refresh;
- RTC provisioning and a maximum of four remote peers/five total participants;
- microphone voice, mute, deafen, and local mixing;
- server-authorized single-publisher screen ownership;
- display/window enumeration and a Catro-owned share dialog;
- GPU capture, H.264 encode/decode, RTP packetization, remote viewing, pop-out, and full screen;
- selected-window process audio in production rooms;
- Windows packaging and transactional installation.

The implementation is concentrated in `apps/windows/Catro`, the Windows-gated room/voice/screen
runtimes, `platform/windows`, and the portable audio/voice/video/transport/community cores.

### macOS

The macOS application currently implements:

- passive capability diagnostics;
- durable local identity/personal state support in the platform layer;
- CoreAudio device enumeration and local meter/tone/monitor tests;
- native SwiftUI diagnostics and accessibility for those diagnostic views;
- arm64 and x64 preview packaging and transactional installation.

It does not currently implement the production directory session, room join, Internet voice,
screen sharing, stream audio, remote video, or the product server UI. Its package correctly says
that it is a preview.

### Release and acceptance

- Installer CI is green on Windows, Apple Silicon macOS, and Intel macOS.
- `docs/validation/oracle-free-production-acceptance.md` is still `NOT VALIDATED`.
- Hosted CI has no trustworthy GPU/display/audio hardware and cannot replace real-machine media
  acceptance.
- The current release workflow milestone is intentionally unfinished and remains blocked by this
  parity milestone.

## 3. Chosen architecture

### 3.1 Rejected alternatives

1. **Duplicate the complete Windows media stack on macOS.**  
   Rejected because it duplicates room, voice scheduling, packetization, queue, and state logic and
   makes parity regressions likely.
2. **Adopt Electron, Qt, WebRTC.framework, a hosted media SDK, or a paid relay/media service.**  
   Rejected because it adds runtime/dependency cost, weakens native integration, or violates the
   free-service constraint.
3. **Use one cross-platform UI.**  
   Rejected by the accepted native-shell architecture and by the performance/accessibility goals.

### 3.2 Selected boundary

Catro keeps one portable real-time core and two native media/UI edges:

```text
WinUI 3 shell                         SwiftUI/AppKit shell
        |                                      |
Windows product bridge                Objective-C++ product bridge
        |                                      |
shared directory values + room/voice/video runtime contracts
        |                                      |
RoomMeshTransport + Opus voice + H.264 RTP + bounded queues
        |                                      |
WASAPI / WGC / Media Foundation       CoreAudio / ScreenCaptureKit / VideoToolbox
        |                                      |
D3D11 presentation                    Metal or AVSampleBufferDisplayLayer presentation
```

The portable layer owns protocol, room state, media packet formats, queue bounds, counters, and
error semantics. Platform layers own device access, permissions, native surfaces, hardware codecs,
and presentation.

## 4. Portable runtime extraction

The existing `core/rtc` and room-runtime implementation are platform-neutral in behavior but
Windows-gated in CMake. They become supported on Windows and Apple toolchains using the already
pinned libdatachannel and Mbed TLS revisions.

The runtime directories stop expressing a Windows ownership boundary:

- room runtime becomes portable and keeps its existing C ABI;
- the production room voice loop becomes portable and receives an `AudioPlatform`;
- direct unencrypted UDP remains an explicit engineering-only adapter and is not required by the
  macOS product;
- shared screen transport logic owns room send/receive, H.264 RTP assembly, stream-audio packet
  handling, queue limits, snapshots, and counters;
- native screen implementations provide capture frames, encoded access units, decoded frames, and
  presentation surfaces without leaking native objects across the Swift bridge.

The Windows product must pass its existing tests unchanged before any macOS feature is considered
green.

## 5. Directory and identity parity

The service API, authorization, and JSON limits remain unchanged.

Shared C++ value types own directory records and strict response validation. Each operating system
keeps a native HTTPS/credential adapter:

- Windows continues to use WinHTTP and DPAPI-protected credentials;
- macOS uses URLSession/Foundation networking and a Keychain generic-password item;
- production rejects plaintext HTTP, user-info URLs, query/fragment secrets, loopback endpoints,
  malformed JSON, and out-of-bound records;
- network work never runs on the UI or real-time media threads.

The macOS product exposes the same bootstrap, server list, Server Code, request/approval, invite,
roster, text, and RTC-provisioning states as Windows.

## 6. macOS voice

macOS voice uses the existing `VoicePipeline`, Opus codec, jitter buffer, multi-speaker mix, limiter,
and room datagrams.

The native edge uses `CoreAudioPlatform` for duplex 48 kHz mono float audio. Product voice adds:

- input/output device settings;
- permission-denied, device-loss, and route-change recovery;
- mute preserving capture timing while encoding silence;
- deafen suppressing render without accumulating playback backlog;
- the same bounded drain/catch-up rules and counters as Windows;
- no allocation, logging, networking, JSON, or UI work on CoreAudio callbacks.

## 7. macOS screen and stream audio

The minimum supported system remains macOS 13.0.

- `SCShareableContent` enumerates displays and eligible windows.
- The Catro source picker is a native SwiftUI sheet/grid with app icon, title, dimensions, source
  kind, selected state, and a bounded preview image. The stock system picker is not the primary
  product experience.
- `SCStream` captures the selected display/window into IOSurface-backed sample buffers.
- `VTCompressionSession` and `VTDecompressionSession` provide low-latency H.264 hardware paths.
- The sender preserves aspect ratio, never upscales, uses even dimensions, and drops stale frames
  rather than building latency.
- The receiver creates decode/presentation work only while the user watches the remote stream.
- Presentation uses a native GPU-backed layer/view and supports embedded, pop-out, full-screen, and
  aspect-fit behavior.
- Screen Recording permission denial/revocation is visible and recoverable without restarting the
  application.

ScreenCaptureKit audio is offered only when the selected source and supported macOS version can
produce it. The UI states the actual behavior before sharing. Catro does not silently capture all
system audio and does not claim per-process isolation where macOS cannot provide it.

## 8. Product UI and UX parity

Both shells implement the same information architecture:

- server rail;
- channel rail;
- text timeline/composer;
- voice channel state and member rail;
- join/leave, mute, deafen, share, watch, pop-out, and full-screen controls;
- owner invite/access controls;
- source picker and share quality settings;
- audio/video/performance settings;
- diagnostics and actionable failure banners.

The visual language remains Catro’s warm neutral palette rather than copying Discord assets or
trade dress. Native controls, virtualization, semantic headings, visible focus, high contrast,
reduced motion, Dynamic Type/text scaling, keyboard traversal, and screen-reader names are required.

The Windows source picker is upgraded from a text-only combo box to the same card/grid contract used
on macOS. Thumbnail generation is lazy, bounded, cancellable, and stops when the picker closes.

No capture, encode, decode, network, JSON, image generation, or polling work may run on a UI thread.

## 9. Performance contract

Performance is measured, not promised by architecture alone. Release acceptance records the exact
machine, OS, GPU, driver, network, resolution, frame rate, bitrate, and game/benchmark.

Required budgets:

| Scenario | Gate |
| --- | --- |
| Idle, 10 minutes | process CPU p95 <= 1.5%; RSS stable with <= 25 MiB growth |
| Five-person voice, 30 minutes | no unbounded queue growth; no sustained codec failures; mouth-to-ear p95 <= 250 ms direct and <= 350 ms forced TURN |
| 1080p30 share, 30 minutes | hardware encode/decode active where advertised; capture-to-send p95 <= 80 ms; glass-to-glass p95 <= 300 ms direct and <= 450 ms forced TURN |
| Game impact | median FPS loss <= 5%; 1% low loss <= 10%; no repeated >100 ms application-induced stalls |
| Network impairment | recovery from 2% loss, 50 ms jitter, and a 2-second interruption without permanent stale backlog |
| Stop/restart | media workers, permissions, surfaces, TURN allocations, and room ownership return to idle bounds |

CPU percentages use total-machine CPU as reported by the native profiler. If a supported acceptance
machine misses a budget, the product is not release-ready; the result is not hidden behind a
hardware-specific disclaimer.

The release copy may say Catro is designed and measured to minimize game impact. It may not say that
screen sharing has zero cost or can never affect a game.

## 10. Automated verification

CI must cover:

- Windows Debug and Release tests;
- macOS arm64 and x64 Debug and Release tests;
- shared runtime tests on Windows and macOS;
- strict directory JSON/security contracts;
- deterministic room, voice, RTP, queue, ownership, and failure tests;
- native permission/state contract tests using fakes where hardware cannot be used;
- installer first-install, upgrade, rollback, checksum, architecture, and no-auto-launch tests;
- package content, signature, endpoint, and secret scans;
- UI policy tests for virtualization, focus/accessibility labels, no heavy visual effects, and no
  product use of the stock capture picker;
- accessibility audits and screenshot baselines on native runners where deterministic;
- sanitizer, signaling race, deployment, and release-validator jobs.

Hosted runner media reports are diagnostic artifacts only.

## 11. Real-machine E2E matrix

Release requires observed packaged-client evidence from:

- Windows x64 ↔ Windows x64;
- Windows x64 ↔ macOS arm64;
- Windows x64 ↔ macOS x64;
- macOS arm64 ↔ macOS x64 where an Intel Mac is available;
- at least two genuinely different Internet providers/cities;
- direct ICE and forced TURN;
- two, three, and five participants, plus an explicit sixth-client rejection;
- both screen-publisher directions for every cross-platform pair;
- permission denial/revocation, device loss, service restart, TURN restart, and VM reboot;
- at least one 30-minute five-person voice run and one 30-minute 1080p30 share run.

Raw logs and measurements are stored as non-secret validation artifacts. A checklist without raw
evidence is not a pass.

## 12. Distribution and security

- The existing transactional installers remain the installation mechanism.
- macOS asset names lose `preview` only after parity and real-machine gates pass.
- Free ad-hoc signing remains allowed; documentation must accurately explain Gatekeeper and the lack
  of notarized publisher identity.
- Installers never use `sudo`, disable Gatekeeper, remove quarantine, weaken Windows security, or
  auto-launch.
- Packages contain only the public production endpoint and no SSH key, room token, TURN secret,
  invite, credential, or local identity.
- A release workflow may assemble and validate artifacts through manual dispatch, but no public tag
  or GitHub Release is created during development.

## 13. Release gates

The project is “ready for distribution” only when all of the following are true at one commit:

1. Windows and both macOS architectures pass Debug, Release, sanitizer, package, and installer CI.
2. The parity feature matrix has no preview-only or unimplemented product capability.
3. Native UI screenshot, keyboard, high-contrast/text-scaling, VoiceOver, and Narrator checks pass.
4. Direct and forced-TURN cross-platform E2E runs pass.
5. Two/three/five-client and sixth-client rejection runs pass.
6. Performance budgets pass on recorded Windows, Apple Silicon, and Intel hardware.
7. Oracle health, WSS, TURN, restart, backup, restore, exposure, and resource checks pass freshly.
8. Release assets are assembled, re-downloaded, checksummed, scanned, and installed in a
   non-publishing workflow-dispatch rehearsal.
9. Documentation matches the observed feature and security state.
10. The only remaining action is creating the approved version tag and publishing the already
    validated release.

Until then, the correct answer to “her şey bitti mi?” is **no**, with the remaining failed or
unobserved gates listed explicitly.

## 14. Non-goals

- Paid Apple Developer membership, notarization, paid TURN/media hosting, SFU, recording, streaming
  to public broadcast platforms, or server-side transcoding.
- Electron, Qt, embedded Chromium, or a second cross-platform UI runtime.
- More than five simultaneous room participants.
- More than one simultaneous screen publisher.
- Universal macOS binaries; arm64 and x64 remain separate native packages.
- A claim of zero performance impact.
- Public release publication before the gates above pass.

