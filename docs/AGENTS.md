# Catro — Full Project Context

> **Purpose of this document**  
> This file consolidates the complete working context established so far between the user, ChatGPT, and the coding agent/Codex for the **Catro** project. It is intended to be dropped into a fresh coding-agent session so the project can continue without losing architectural intent, constraints, priorities, or the reasoning behind previous decisions.

> **Status note**  
> The coding agent reported that **Specification v1.0.0** was written, self-reviewed, and committed as `440f1e9` at:  
> `C:/Users/cenke/OneDrive/Desktop/catro/docs/superpowers/specs/2026-09-26-native-foundation-capabilities-design.md`  
> ChatGPT was not able to independently open that local Windows path or verify the commit from an accessible GitHub copy, so this document records the agent's report as project state, not as independently verified repository evidence.

---

# 1. Product Vision

**Catro** is a downloadable, native-first desktop communication platform for **Windows and macOS**.

The high-level experience is Discord-like in the sense that users should be able to:

- Create/join private communities/servers.
- Have text channels.
- Have voice channels.
- Chat with friends.
- Join high-quality voice calls.
- Share their screen.
- Share a specific application/game.
- Include game/application audio in the stream.
- Automatically detect games/applications where technically appropriate.
- Maintain extremely high visual/audio quality while minimizing system load.

However, **Catro should not be engineered as a simple Discord clone**. The defining product goal is to build a **serious native systems application** whose differentiators are:

1. Very low latency.
2. High audio quality.
3. High screen/game-stream quality.
4. Excellent use of CPU/GPU/media hardware.
5. Native OS integration.
6. Low unnecessary memory/copy overhead.
7. Premium, modern desktop UX.
8. Deterministic hardware-aware behavior.
9. Strong diagnostics and engineering observability.

The project should feel like it was built by a team obsessed with **systems engineering, media quality, hardware efficiency, and native desktop behavior**.

---

# 2. Intended Scale and Real Usage Pattern

The product may support communities with up to roughly **100 members**, but the expected real-time usage is much smaller.

The intended typical live session is closer to:

- 2 people in voice, 1 person streaming.
- 4 people in voice, 1 person streaming.
- Occasionally a somewhat larger small-group room.

The architecture should therefore be optimized initially for:

- Approximately **2–10 simultaneous voice users** in a room.
- Usually **1 active screen/game stream**, possibly 2 later.
- Small private communities.
- One lightweight Linux deployment initially.

The project must **not** be over-designed as if hundreds of users will simultaneously publish high-bitrate streams.

At the same time, architectural choices should avoid obvious dead ends that make future scaling impossible.

---

# 3. Business/Hosting Direction

A major product-level architecture decision was explicitly resolved:

## Primary deployment model

**Centrally hosted first.**

The default user experience should be:

1. Download Catro.
2. Create or join a community.
3. Use chat, voice, and streaming immediately.
4. No infrastructure knowledge required from normal users.

## Future optional direction

The protocol and backend boundaries should remain clean enough to allow **private/self-hosted deployments later**.

However, the current project scope explicitly excludes:

- Federation.
- Self-hosting management UI/tooling.
- Multi-region complexity.
- Kubernetes-scale infrastructure.
- Full private deployment tooling.

## Initial backend deployment target

One lightweight Linux server/VM should eventually be enough for the early private/small-group deployment.

The client/server protocol should remain **deployment-agnostic** and not hard-code assumptions that permanently bind the client to one centralized service.

---

# 4. Cost / Zero-Capital Assumption Discussed

The user wants to begin with **zero capital** if possible.

For the expected usage pattern (2–4 users in voice and one streamer), the working conclusion was that a free-tier deployment is realistic for development and small private usage.

Previously discussed infrastructure ideas included:

- A free-tier Linux VM.
- SFU architecture rather than full media transcoding.
- Direct UDP/WebRTC-style media paths.
- STUN/TURN support.
- Avoid routing media through a bandwidth-limited free load balancer.

One discussion referenced Oracle free-tier network allowances and a single public-IP VM as a possible starting point. **Any specific free-tier limits, bandwidth quotas, and pricing must be re-verified at deployment time** because cloud-provider policies change.

The core architectural conclusion remains:

> For small rooms with a single stream, the technical bottleneck is manageable, and the project can start without paid infrastructure if current free-tier offerings are sufficient.

---

# 5. Non-Negotiable Native Requirement

This is one of the strongest requirements in the entire project.

Catro must be **overwhelmingly native**.

## Required direction

- Performance-sensitive desktop code: **modern C/C++**, primarily C++.
- Shared systems core: modern C++.
- Windows shell: native Windows UI and OS APIs.
- macOS shell: native macOS UI and OS APIs.
- Native graphics/media/audio APIs should be preferred when they offer better performance, latency, reliability, or integration.

## Explicitly rejected as the primary architecture

- Electron.
- Chromium runtime.
- Browser-first application architecture.
- Localhost web UI.
- A browser engine as the main UI/runtime.
- Qt/QML as the primary UI architecture for Catro.
- Bespoke custom C++ UI renderer as the primary foundation.

A custom GPU UI could theoretically provide complete control, but it would force Catro to own accessibility, IME, focus behavior, native text input, window behavior, etc., which is not aligned with the current project priorities.

---

# 6. Agreed Client Architecture

The coding agent proposed three approaches:

1. **Split-native shells with shared C++ core** — recommended and approved.
2. Qt 6/QML shared C++ application — rejected for this product direction.
3. Custom cross-platform C++ GPU UI — rejected as unnecessary risk and scope.

The approved architecture is:

```text
Windows native shell (WinUI 3 / C++/WinRT) ─┐
                                            ├─ platform adapter → shared C++ systems core
macOS native shell (SwiftUI + AppKit) ──────┘
```

Platform-specific graphics/media paths can use:

```text
Windows → Direct3D / DXGI / Windows native media/audio APIs
macOS   → Metal / VideoToolbox / ScreenCaptureKit / CoreAudio
```

The C++ shared core should contain platform-independent high-performance logic such as:

- Capability modeling.
- Media policy.
- Protocol logic later.
- Networking logic later.
- Diagnostics representation.
- Capture/media decision policy.
- Performance-sensitive shared components.

The core must contain **no platform-native object types**.

Specifically, the shared core should not expose or depend on:

- Win32 types.
- WinRT types.
- DirectX types.
- Objective-C object types.
- Swift types.
- AppKit types.
- Metal types.

macOS should use a **narrow Objective-C++ bridge** into the shared C++ core.

No exceptions should cross that language/platform boundary.

---

# 7. UI Direction

The UI must be **premium, modern, polished, and intentionally designed**.

It must absolutely not look like:

- Old Windows Forms.
- Generic enterprise software.
- Default native framework widgets thrown together.
- A browser app inside a desktop shell.
- A dated settings utility.

Desired visual qualities:

- Refined typography.
- Excellent spacing.
- Smooth state transitions.
- Modern channel/server navigation.
- High-quality voice-room states.
- Elegant streaming presentation.
- Excellent dark mode.
- Light mode where appropriate.
- Native window behavior.
- High-DPI correctness on Windows.
- Retina correctness on macOS.
- Accessibility.
- Keyboard navigation.
- Multi-monitor support.
- Native visual behavior per platform.

The visual implementation is allowed to differ between Windows and macOS if that improves native quality.

A shared design language/system should exist conceptually without forcing identical low-level rendering.

---

# 8. Distribution Requirements

Catro is a downloadable desktop application.

It should support both normal installers and command-line installation.

## Windows

Intended experience:

```text
winget install <catro-package>
```

Expected future release properties:

- Signed installer.
- Clean uninstall.
- GitHub Releases.
- winget compatibility.
- Automatic update mechanism.

## macOS

Intended experience:

```text
brew install --cask <catro-package>
```

or an equally polished one-command installation path.

Expected future release properties:

- Correct Apple Silicon support.
- Intel Mac support where practical.
- Universal binaries where practical.
- `.app` bundle.
- Code-signing architecture.
- Notarization-ready pipeline.
- DMG or equivalent polished packaging.
- Homebrew Cask compatibility.
- Automatic updates.

GitHub Releases should be a first-class distribution mechanism.

---

# 9. High-Level Media Research Conclusions

Before the current native-foundation milestone was defined, the project was researched at a high level.

These are **future subsystem directions**, not part of the current implementation milestone.

## Windows audio

Potential native path:

- WASAPI.
- Windows audio-session APIs.
- Process-specific/system loopback capture where supported.

The key product idea is that Catro should eventually be able to share game/application audio without blindly transmitting every system sound.

## macOS audio / capture

Potential native paths:

- ScreenCaptureKit.
- CoreAudio.
- VideoToolbox.
- Metal / IOSurface where useful.

## Screen/game capture

Potential Windows paths:

- Windows Graphics Capture.
- DXGI Desktop Duplication where appropriate.
- Direct3D surfaces.

Potential macOS path:

- ScreenCaptureKit.

The architecture should prefer GPU-native surfaces and avoid unnecessary CPU staging where possible.

## Hardware video encoding

Potential Windows hardware backends:

- NVIDIA NVENC.
- AMD AMF.
- Intel hardware encoding / Quick Sync / oneVPL or native alternatives.

Potential macOS hardware backend:

- VideoToolbox / Apple hardware media engines.

## Desired media pipeline principle

Prefer:

```text
GPU capture resource
→ GPU/native surface
→ hardware encoder
→ network
```

Avoid unnecessary:

```text
GPU
→ CPU RAM
→ CPU processing
→ RAM
→ GPU encoder
```

The long-term goal is to minimize copies, latency, CPU load, and power usage while preserving quality.

## Audio codec / RTC direction

Previously discussed mature components include:

- Opus for voice.
- WebRTC media concepts/components.
- WebRTC Audio Processing for echo cancellation/noise suppression/etc. where technically appropriate.

No custom codec should be invented.

## SFU direction

The project should not use full mesh P2P for rooms containing several users.

An SFU-style architecture is preferred:

```text
Publisher → SFU → subscribers
```

The server should forward media rather than unnecessarily decode/re-encode it.

Potential technology investigated:

- mediasoup / mediasoup-rust / native client bindings.
- coturn for TURN/STUN relay functions.

These are still future media/server implementation decisions and are **not part of the native-foundation milestone**.

---

# 10. Hardware Intelligence Philosophy

Catro should not treat all computers identically.

A key design objective is to understand the machine and choose high-quality defaults based on **capabilities rather than branding**.

Examples of systems that should be treated differently:

- High-end NVIDIA desktop.
- Integrated Intel laptop.
- Hybrid-GPU Windows laptop.
- Apple Silicon MacBook.
- Older Intel Mac.
- Laptop under thermal pressure.
- Laptop on battery.
- Desktop on AC power.

The application should detect and reason about things such as:

- CPU architecture.
- Core topology.
- GPU inventory.
- Hardware encoders.
- Codec support.
- Display resolution.
- Display scaling.
- Refresh rate.
- Audio devices.
- Power source.
- Low-power mode.
- Thermal state.
- Memory pressure.
- Session state.
- Native API availability.

The critical architectural rule is:

> **Detect capabilities, not just device names.**

For example, seeing `RTX 5070` is less important than knowing:

- What codecs are available?
- Is AV1 encode available?
- Is low-latency encode supported?
- What input formats are supported?
- Which display/capture path maps to which GPU?
- Can the capture resource be passed to the encoder without a CPU copy?
- What quality ceiling is actually known?

Device-name inference should be a degraded fallback at most, never the authoritative capability model.

---

# 11. Native Foundation Milestone

The coding agent correctly reframed the initial task as an **architectural greenfield project**.

The workspace was initially empty, so the project must be decomposed into **independently verifiable milestones** rather than scaffolding the entire platform at once.

The agreed first milestone is:

> **Native application foundation + hardware/capability system.**

This milestone intentionally excludes most of the actual product functionality.

---

# 12. Explicit Non-Goals of the Current Milestone

The following must **not** enter the current foundation milestone:

- Server implementation.
- Authentication.
- User accounts.
- Messaging implementation.
- WebRTC transport.
- Voice communication pipeline.
- Screen-capture sessions.
- Game-capture sessions.
- Production hardware encoder activation.
- Full streaming implementation.
- Production voice/media stack.
- Federation.
- Self-hosting tooling.
- Full product navigation.
- Complex backend infrastructure.

Future media requirements may influence **interfaces and policy outputs**, but the actual future subsystems must not leak into this milestone.

---

# 13. Design 1/3 — Foundation Boundaries

The coding agent proposed and the user/ChatGPT approved the following architecture.

```text
Windows: WinUI 3 / C++/WinRT ─┐
                              ├─ platform adapter → immutable capability snapshot
macOS: SwiftUI + AppKit       ┘                         │
                                                        ▼
                                  shared C++ capability model
                                  shared deterministic policy engine
                                  shared diagnostics representation
```

## Core boundary

The core contains no platform-specific native types.

Windows may consume the shared C++ values directly.

macOS uses a narrow Objective-C++ bridge with explicit ownership.

No exceptions cross the Objective-C++/C++ boundary.

## Capability subsystem responsibilities

The first design defined four responsibilities:

1. Platform probes measure hardware/runtime facts.
2. An immutable `CapabilitySnapshot` normalizes the results.
3. A pure deterministic defaults engine turns capabilities into an intended media plan.
4. Native diagnostics views render raw evidence, decisions, fallbacks, and reasons.

## Explicit uncertainty

Capabilities use explicit states such as:

- supported
- unsupported
- unknown

Probe failures must produce **partial snapshots with structured issues**, not fabricated assumptions.

## Policy style

The policy engine ranks **capability classes**, not device names.

It may decide future behavior such as:

- Capture path preference.
- Codec preference.
- Encoder preference.
- Quality tier.
- FPS ceiling.
- Power-aware behavior.
- Fallback order.

Any future device-specific workaround should live in a measured compatibility table rather than contaminate the general policy.

## Snapshot consumption rule

Later subsystems such as audio, capture, encoding, networking, WebRTC, and diagnostics must consume the shared capability snapshot.

They should **not independently query the OS**.

## Approximate repository boundary

```text
/apps/windows             WinUI shell and Windows diagnostics UI
/apps/macos               SwiftUI/AppKit shell and macOS diagnostics UI
/core/capabilities        normalized model, validation, decision engine
/platform/windows         Windows capability probes
/platform/macos           macOS capability probes and Objective-C++ bridge
/tests/capabilities       deterministic fixtures and policy tests
/tools/capability-report  engineering report/export utility
/docs/architecture        milestone decisions and capability schema
```

## Additional constraints added during approval

- `CapabilitySnapshot` must be versionable.
- Important facts should preserve provenance.
- Policy should not depend directly on UI concepts.
- Every meaningful `MediaPlan` decision should be explainable.
- Multiple GPUs/encoders/displays are first-class.
- Static hardware and dynamic runtime state should remain conceptually distinct.
- Snapshot changes should be classified by domain.
- Strong typed domain objects are preferred over generic key/value capability bags.
- Fixtures should include ideal and pathological systems.
- `capability-report` should output both human-readable and machine-readable forms.

---

# 14. Design 2/3 — Capability Schema and Policy

The coding agent proposed the following strongly typed model.

## Snapshot structure

```cpp
struct CapabilitySnapshot {
    SnapshotHeader header;             // schema, generation, timestamps
    PlatformIdentity platform;         // OS, native/process architecture
    HardwareCapabilities hardware;     // mostly static
    DeviceInventory devices;           // GPUs, encoders, displays, audio
    RuntimeState runtime;               // power, thermal, memory, session
    std::vector<ProbeIssue> issues;     // partial failures
};
```

## Snapshot header

Contains:

- Stable schema identifier: `catro.capabilities`
- Major/minor schema version.
- Monotonic generation number.
- Capture timestamp.
- Probe implementation version.

Versioning rule:

- Major changes may be incompatible.
- Minor changes are additive.
- Reports and fixtures declare schema version explicitly.

## Evidence and uncertainty model

Example:

```cpp
enum class Knowledge { known, unknown, unavailable };
enum class EvidenceMethod { measured, advertised, inferred, cached };
enum class Confidence { high, degraded };

template<class T>
struct Observed {
    Knowledge knowledge;
    std::optional<T> value;
    Provenance provenance;
};

struct SupportFact {
    Support status;                    // supported, unsupported, unknown
    Provenance provenance;
};
```

`Provenance` should capture:

- Probe identifier.
- Evidence method.
- Timestamp.
- Stable issue code where relevant.

Example principle:

> If encoder enumeration fails, the fact remains **unknown because enumeration failed**, not **unsupported**.

Inference is allowed only when explicitly marked and should never outrank measured evidence.

## Strong domain objects

The model includes objects such as:

### `CpuCapability`

- Native architecture.
- Process architecture.
- Physical/logical core count.
- Performance/efficiency core information.
- SIMD features.
- Translation state.

### `MemoryCapability`

- Installed memory.
- Relevant platform limits.

### `GpuCapability`

- Snapshot-scoped typed ID.
- Vendor/device identity.
- Integrated/discrete/external/software classification.
- Memory.
- Supported graphics APIs.
- Hardware role.
- Power preference.

### `EncoderCapability`

- Associated GPU where discoverable.
- Codec.
- Backend.
- Hardware acceleration.
- Low-latency support.
- Input formats.
- Known resolution/FPS constraints.
- Zero-copy interoperability evidence.

### `CapturePathCapability`

- Native API.
- Supported source kinds.
- Permission state.
- GPU affinity.
- Output formats.
- Known interop paths.

### `DisplayCapability`

- GPU association where known.
- Pixel dimensions.
- Logical dimensions.
- Scale factor.
- Refresh rate as a rational value.
- HDR state.
- Current availability.

### `AudioEndpointCapability`

- Direction.
- Availability.
- Default roles.
- Channel counts.
- Advertised sample formats.

### `RuntimeState`

- Power source.
- Battery presence.
- Low-power mode.
- Thermal pressure.
- Memory pressure.
- Remote/headless session state.
- Active-device state.

## Multiple devices

Multiple GPUs, encoders, displays, and audio devices are modeled as ordinary vectors linked with typed IDs.

There is no schema-level assumption of a single "primary GPU."

## Refresh/change classification

```cpp
struct SnapshotUpdate {
    CapabilitySnapshot snapshot;
    ChangeSet changes;
};
```

`ChangeSet` can identify changes such as:

- GPU.
- Encoder.
- Display.
- Audio input.
- Audio output.
- Capture permission.
- Power.
- Thermal.
- Memory pressure.
- Session.

Consumers should be able to ignore irrelevant updates without manually diffing entire snapshots.

## Policy interface

```cpp
MediaPlan derive_media_plan(
    const CapabilitySnapshot& snapshot,
    const MediaDecisionRequest& request);
```

The policy receives explicit media-domain inputs and **does not depend on UI state**.

The request includes concepts such as:

- Source kind.
- Selected display where relevant.
- Latency class.
- Operating preference.
- Requested quality envelope.

Originally optional future peer/network inputs were considered, but this was later corrected: **local policy should not accept premature network/peer negotiation inputs in this milestone.**

## `MediaPlan`

Contains concepts such as:

- Operating profile.
- Ranked capture paths.
- Ranked encoder candidates.
- Codec preference order.
- GPU affinity.
- Anticipated transfer path.
- Local quality ceiling.
- Conservative starting profile.
- Resolution/FPS limits.
- Power/thermal downgrade behavior.
- Ordered fallback chain.
- Structured decision trace.

## Deterministic rule order

The approved general precedence is:

1. Reject structurally invalid/incompatible snapshots.
2. Apply hard request constraints.
3. Remove known-unsupported candidates.
4. Prefer known support over unknown support.
5. Preserve display → capture GPU → encoder GPU affinity where possible.
6. Prefer measured low-latency hardware paths and avoid cross-GPU copies.
7. Apply operating profile/power/thermal considerations.
8. Select a conservative starting profile within the known local ceiling.
9. Build fallbacks from remaining valid candidates.

No floating-point magic score should determine ties.

Candidates should be compared lexicographically using documented rule priority and stable IDs.

Unknown limits must not justify aggressive modes like 1440p60.

Unknown support may still allow a conservative candidate that later requires runtime validation.

## Explainability

Every meaningful decision should record:

- Selected candidate.
- Rule ID.
- Facts used.
- Higher-ranked alternatives considered.
- Stable rejection reason per alternative.
- Whether degraded/unknown evidence affected the result.
- Next fallback.

Example diagnostic statement:

> Intel H.264 selected because it shares the display adapter and supports measured low-latency hardware encoding; NVIDIA AV1 rejected because the required path would incur a cross-adapter transfer and the necessary compatibility evidence is incomplete.

## Additional constraints added during approval

- Advertised support and runtime validation must remain separate concepts.
- A runtime failure must not permanently mutate hardware capability facts.
- Codec support must be distinct from concrete mode support.
- Color/pixel-format/bit-depth/HDR properties must be representable.
- Zero-copy must be represented as a relationship/path, not a single encoder boolean.
- Transfer cost should distinguish native/same-adapter/cross-adapter/CPU-staging/unknown.
- Capture capability should remain source-specific.
- Requested quality, local achievable quality, and future negotiated quality are distinct concepts.
- Rational rates must be preserved exactly where possible (e.g. 59.94 is not silently normalized to 60).
- Software encoding must be an explicit candidate with explicit consequences.
- Stable IDs must only promise the scope the OS actually guarantees.
- Decision traces must be bounded.
- Policy rules should have an independent version from the capability schema.
- Serialization must be deterministic.
- Policy produces an **intended plan**, not a promise that activation will succeed.

The same:

```text
(snapshot, request, policy-version)
```

must always produce the same `MediaPlan`.

---

# 15. Design 3/3 — Probing, Diagnostics, Testing, Completion

The coding agent incorporated the previous additions.

## Important correction

`MediaPlan` represents an **intended local plan**, not a guarantee.

Advertised/probe-validated evidence stays in the snapshot.

Future real runtime activation attempts produce separate results and do not mutate hardware facts.

---

# 16. Probe Behavior

A shared orchestration service runs probes away from the UI thread and publishes one coherent snapshot generation.

Probe families fail independently.

Example:

> Audio probing failing must not invalidate otherwise good GPU/display evidence.

## Windows probe families

Proposed areas:

- Processor/memory/power/platform-role APIs.
- DXCore/DXGI adapter enumeration.
- Adapter LUIDs.
- Display Configuration APIs.
- Rational refresh-rate preservation.
- Media Foundation hardware-transform enumeration.
- MMDevice audio endpoint APIs.
- Runtime API availability checks for future capture paths such as Windows Graphics Capture.

## macOS probe families

Proposed areas:

- `sysctl`.
- `ProcessInfo`.
- Metal device enumeration.
- Registry identifiers where appropriate.
- CoreGraphics display information.
- VideoToolbox capability/session queries.
- CoreAudio endpoint enumeration.
- ScreenCaptureKit availability and permission status without prompting.
- Power state.
- Low-power mode.
- Memory pressure.
- Thermal APIs.

## Passive probing requirement

Startup probing must not:

- Trigger privacy prompts.
- Start capture.
- Start encoding workloads.
- Create persistent device changes.
- Cause user-visible intrusive side effects.

Relationships that cannot be determined reliably should remain `unknown`.

---

# 17. Mode and Transfer Modeling

Codec support and concrete modes are separate.

Example proposed type:

```cpp
struct EncoderModeCapability {
    Codec codec;
    ResolutionRange dimensions;
    RationalRateRange frame_rates;
    PixelFormat input_format;
    ChromaSubsampling chroma;
    Observed<std::uint8_t> bit_depth;
    ColorRange color_range;
    TransferFunction transfer_function;
    HdrMode hdr;
    LatencyMode latency;
    SupportEvidence evidence;
};
```

Exact mode limits remain unknown unless the platform can report them.

Rational values are reduced but not rounded into approximate integer refresh rates.

## Transfer path model

```cpp
struct TransferPathCapability {
    CapturePathId source;
    EncoderId destination;
    std::optional<GpuId> source_gpu;
    std::optional<GpuId> destination_gpu;
    TransferKind transfer;   // native resource, same-adapter copy,
                             // cross-adapter copy, CPU staging, unknown
    ConversionRequirement conversion;
    SupportEvidence evidence;
};
```

Zero-copy/transfer behavior is a **path relationship**, not a single encoder property.

Capture modes remain source-specific.

---

# 18. Quality Concepts

The design separates:

- `RequestedQuality` — caller intent.
- `LocalQualityEnvelope` — locally achievable bounds.
- `MediaPlan` — intended local starting choice and fallbacks.
- `NegotiatedQuality` — future result of network/peer/media negotiation, outside this milestone.

Software encoding is an ordinary explicit candidate, not an implied fallback.

---

# 19. Validation Rules

The validator should reject impossible/inconsistent states including:

- `known` evidence without a value.
- `unknown`/`unavailable` evidence that incorrectly carries a value.
- Duplicate snapshot-scoped IDs.
- References to missing GPUs/encoders/displays/capture paths.
- Invalid rational denominators.
- Non-positive dimensions.
- Contradictory ranges.
- Concrete modes incompatible with their parent codec declaration.
- Transfer edges with nonexistent endpoints.
- Contradictory support/evidence states.
- Unsupported capability-schema versions.
- Unsupported policy versions.

Snapshot IDs promise stability only within their declared scope.

No cross-reboot identity should be implied unless guaranteed by the OS/API.

---

# 20. Policy Identity and Bounded Decision Traces

Capability schema and policy versions are independent.

The policy is intended to remain a pure function:

```text
(snapshot, request, policy version) → identical MediaPlan
```

Decision traces are replaced per evaluation rather than accumulated.

The proposal bounded them approximately as:

- Maximum 32 ranked candidates per decision category.
- Maximum 8 reasons per candidate.
- Deterministic truncation metadata.
- Explicit omitted counts.

---

# 21. Diagnostics Workspace

Both native shells should expose a real engineering diagnostics workspace.

Expected information:

- Snapshot generation.
- Capability schema version.
- Policy version.
- Probe duration.
- Partial probe issues.
- Degraded-confidence indicators.
- CPU.
- Memory.
- Power.
- Thermal state.
- Session state.
- GPU/display topology.
- Exact scaling.
- Exact refresh rates.
- Encoder codecs.
- Advertised concrete modes.
- Capture-source capabilities.
- Permission state.
- Transfer-path costs.
- Required color conversions.
- Intended media plan.
- Local quality envelope.
- Ordered fallbacks.
- Expandable selection/rejection reasons.
- Manual refresh.
- Classified changes.
- Human-readable export.
- Canonical JSON export.

UI-specific expectations:

## Windows diagnostics UI

- Native WinUI 3 styling.
- Adaptive high-DPI layout.

## macOS diagnostics UI

- SwiftUI/AppKit conventions.
- Retina-aware layout.

Both should have:

- Deliberate typography.
- Deliberate spacing.
- Semantic status colors.
- Keyboard navigation.
- Dark/light adaptation.
- Accessible labels.

Reports should omit unnecessary persistent hardware serial numbers or overly identifying data.

---

# 22. Capability Report Tool

Planned CLI:

```text
catro-capability-report --format human
catro-capability-report --format json --output report.json
```

Reports should include:

- Snapshot.
- Probe issues.
- Policy revision.
- Representative request.
- Intended plan.
- Fallbacks.
- Bounded trace.

---

# 23. Deterministic Serialization Rules

Canonical JSON should use:

- Fixed schema/field ordering.
- Stable enum strings.
- Explicit unknown/unavailable states.
- Sorted arrays where source ordering is not meaningful.
- Reduced rational numerator/denominator objects.
- Integer quantities where possible.
- No locale-dependent formatting.
- UTF-8.
- One defined newline/indentation convention.
- No nondeterministic timestamps in golden fixtures unless explicitly fixed.

A mature pinned JSON library may be used for parsing/escaping.

However, **Catro owns canonical ordering and domain serialization rules**.

Determinism must not depend on:

- STL container iteration order.
- Pointer identity.
- OS enumeration order.
- Locale.
- Timezone.
- Wall-clock timing.

---

# 24. Build Structure

Agreed direction:

- Shared code: C++20.
- CMake presets.
- Windows shell: C++/WinRT + WinUI 3.
- Windows native Visual Studio/MSBuild toolchain for app shell.
- macOS shell: SwiftUI/AppKit + Objective-C++ bridge.
- Xcode for macOS shell.
- macOS deployment target: approximately macOS 13+.
- Apple Silicon + Intel builds.
- Windows 11 primary.
- Windows 10 22H2 x64 where supported.
- Initial Windows ARM64 packaging is outside this milestone.
- Strict warnings-as-errors for Catro-owned code.
- ASan/UBSan where supported.
- Pinned dependency versions.
- Recorded dependency licenses.

CMake is authoritative for:

- Shared core.
- Platform probe libraries.
- Tests.
- Report tool.

Native project files remain responsible for application shell and packaging metadata.

Explicitly excluded dependencies in this milestone:

- Qt.
- Electron.
- Browser runtime.
- Server component.
- Media transport dependency.

---

# 25. Test / Verification Strategy

Tests should cover:

- Snapshot invariants.
- Referential integrity.
- Schema version handling.
- Policy version handling.
- Deterministic serialization.
- Round trips.
- Refresh/change classification.
- Repeatable policy output.
- Bounded traces.
- Capture-to-encoder transfer ranking.
- Color/HDR/bit-depth preservation.
- Rational-rate preservation.
- Advertised vs validated evidence.
- Software encoder consequences.
- Partial probe results.
- Unknown facts.

Golden fixtures should include:

- High-end NVIDIA desktop.
- Integrated Intel laptop on battery.
- Hybrid-GPU Windows laptop.
- Apple Silicon MacBook under normal conditions.
- Apple Silicon MacBook under thermal pressure.
- Older Intel Mac with incomplete GPU affinity.
- Multiple encoders per GPU.
- Software-only encoder.
- Unknown codec-mode limits.
- Missing/broken drivers.
- Headless session.
- Remote session.
- No microphone.
- No audio output device.
- Mixed 59.94/60/120/144 Hz displays.
- Partial failures in every probe family.

Platform smoke tests should verify real probes produce structurally valid snapshots without prompting for permissions.

Important testing rule:

> Do not claim real hardware validation unless that hardware actually ran the test.

Synthetic fixtures are for exhaustive policy coverage.

Real hardware tests validate actual platform probes.

If GitHub-hosted CI cannot validate a specific hardware path, that requirement should remain explicit as a **hardware validation gate**, not be faked.

---

# 26. Probe Isolation / Hard Timeout Correction

After self-review of the spec, the coding agent reported one important correction:

> **All probe families now use isolated helpers so hard timeouts are enforceable.**

This came from a requirement added during review:

- A broken OS API or bad driver must not indefinitely block startup.
- Probe timeouts must be bounded.
- A coherent partial snapshot should still be publishable.
- Probe failures should become structured issues.

Additional desired behavior:

- Each probe has a stable probe ID.
- Each probe records duration.
- Each probe records outcome/failure classification.
- A crashed/hung/malformed/incompatible helper must not destabilize the main application.
- Isolated probe-helper IPC should be narrow, versioned, bounded, and failure-tolerant.
- Do not invent a generic plugin framework just to implement probe isolation.

---

# 27. Static vs Dynamic State Correction

Another reported spec correction:

> **Dynamic device/runtime state is now separated from static capabilities.**

Examples of dynamic state:

- Power source.
- Thermal pressure.
- Memory pressure.
- Device availability.
- Active device state.
- Session state.

Examples of mostly static capability data:

- CPU architecture.
- GPU inventory.
- Hardware encoder capabilities.
- Graphics API support.

They may be exposed via one snapshot service, but internally they should remain conceptually distinct and refresh independently where appropriate.

---

# 28. Local Policy Input Correction

The third reported self-review correction:

> **Local policy no longer accepts premature network/peer negotiation inputs.**

The capability milestone is about **local hardware capability and local intended policy**.

Network/peer constraints belong to later media negotiation.

This avoids contaminating local hardware policy with concepts from subsystems that do not exist yet.

---

# 29. Additional Engineering Principles Added During Review

These were explicitly requested and should remain authoritative.

## Platform support

Platform behavior should be capability-driven.

If an API is unavailable on an older OS/device:

- Report it cleanly.
- Do not introduce hacks into the shared domain model.

## Objective-C++ bridge

Keep it very narrow.

Swift/AppKit/Metal/CoreFoundation ownership semantics must not leak into shared C++.

## ABI strategy

Avoid unnecessary ABI-sensitive shared-library boundaries early.

Prefer source/module boundaries until a stable binary ABI is actually needed.

## JSON is not the domain model

Do not optimize C++ architecture for JSON convenience.

JSON is an export/diagnostics representation.

Strongly typed C++ remains authoritative.

## Passive discovery

No passive probe should:

- Prompt for privacy permissions.
- Start capture.
- Start encoder workloads.
- Change persistent device state.
- Cause user-visible system effects.

Any future intrusive operation should require a separate activation phase.

## Privacy

Diagnostics/export should not reveal more identifying hardware information than necessary for debugging.

## Policy regression discipline

Tests should lock not only selected plans but important:

- Rejection reasons.
- Fallback ordering.

Intentional policy-version changes should require:

- Corresponding fixture updates.
- Documented decision.

## Dependency discipline

Capability core should remain lightweight enough to reuse in:

- Headless tests.
- CLI tools.
- Future services/tools.

without linking UI frameworks.

---

# 30. Current Milestone Completion Criteria

The milestone is complete only when the following are true:

- Shared libraries build cleanly.
- Windows native shell builds cleanly.
- macOS native shell builds cleanly on targeted architectures.
- Deterministic fixtures pass.
- Pathological fixtures pass.
- Real platform snapshots pass validation.
- Both native diagnostics views show real capability/policy evidence.
- Human-readable reports work.
- Canonical JSON reports work and are regression-tested.
- Refreshes produce correct classified changes.
- Repeated identical evaluations produce byte-identical plans and traces.
- Vendor/model names are not silently treated as capability truth.
- Required architecture/build/schema/troubleshooting documentation exists.
- No prohibited future subsystem has leaked into the milestone.

---

# 31. Versioned Specification Status

The coding agent reported:

- Specification: **v1.0.0**
- Commit: `440f1e9`
- Local path:  
  `C:/Users/cenke/OneDrive/Desktop/catro/docs/superpowers/specs/2026-09-26-native-foundation-capabilities-design.md`

The agent also reported that the following checks passed:

- Placeholder checks.
- Consistency checks.
- Scope-leakage checks.
- Platform-coupling checks.
- Git whitespace checks.

Reported self-review fixes:

1. Probe helpers isolated for hard timeouts.
2. Dynamic device/runtime state separated from static capability state.
3. Local policy no longer accepts premature network/peer inputs.

Again: this is **reported state from the coding agent**, not independently verified here.

---

# 32. Latest Instruction to the Coding Agent — Implementation Plan Only

The current instruction is **not to start coding yet**.

The coding agent should first produce a complete implementation plan.

The approved response to the coding agent was essentially:

> Approved. Specification v1.0.0 is accepted as the architectural baseline for Catro’s native foundation milestone.
>
> The three self-review corrections are also correct and should remain authoritative:
>
> - potentially blocking probes must execute through isolation that allows real hard timeouts,
> - dynamic runtime/device state must remain conceptually separate from static hardware capabilities,
> - local capability policy must not prematurely depend on peer or network negotiation.
>
> Proceed to the implementation plan before writing product code.

---

# 33. Desired Implementation Plan Decomposition

The implementation plan should decompose the milestone into small, independently verifiable engineering slices.

A suggested dependency order was:

1. Repository/build foundation and CMake presets.
2. Core domain primitives, typed IDs, rational values, evidence/provenance types.
3. `CapabilitySnapshot` schema and invariants.
4. Deterministic serialization and schema/policy versioning.
5. Validator and pathological unit tests.
6. Capability fixture framework.
7. Pure deterministic policy engine.
8. `MediaPlan`, fallback graph, bounded decision traces.
9. Golden policy fixtures and regression tests.
10. Probe orchestration and isolated-helper IPC contract.
11. Windows probe implementation.
12. macOS probe implementation.
13. Snapshot refresh/change-classification service.
14. `catro-capability-report`.
15. Windows native diagnostics shell.
16. macOS native diagnostics shell.
17. Platform smoke tests.
18. CI/build verification.
19. Documentation and milestone acceptance checks.

This is **not** a requirement to create exactly 19 tasks.

The coding agent should optimize the real plan for coherent commit boundaries and independent verification.

---

# 34. Required Contents of Every Implementation Step

For each implementation slice, the plan should state:

- Goal.
- Implementation scope.
- Files/modules to create or modify.
- Dependencies on previous steps.
- Tests.
- Exact local verification method.
- Expected observable result.
- Failure conditions.
- Commit boundary.

Every slice should leave the repository:

- Buildable.
- Testable.
- Reviewable.

Avoid giant commits.

---

# 35. Additional Implementation-Plan Constraints

The implementation plan must respect all of these:

- Shared C++ core and tests should exist before native UI work.
- Platform dependencies stay outside the shared capability domain.
- Define Objective-C++ bridge only after the shared contract is stable enough.
- Probe-helper IPC must be narrow, versioned, bounded, failure-tolerant.
- Crashed/hung/malformed/incompatible helpers become structured issues.
- Do not create a generic plugin framework merely for helper isolation.
- OS enumeration order must not influence policy or serialization.
- Determinism tests should be added early.
- Synthetic fixtures provide exhaustive policy coverage.
- Real-machine tests validate actual hardware probing.
- CI hardware limitations must be documented honestly.
- Do not claim real NVIDIA/Intel hybrid/Apple Silicon/Intel Mac validation unless actually run.
- No compiler warnings from Catro-owned code.
- No server/auth/messaging/WebRTC/voice/real capture/real streaming implementation in this milestone.
- Do not add abstractions merely because a future milestone might theoretically need them.

---

# 36. Milestone-Level Verification Matrix Requirement

At the end of the implementation plan, the coding agent must include a verification matrix that maps:

```text
Specification completion criterion
→ implementation step
→ test/verification proving it
```

This is intended to make completion objective rather than subjective.

---

# 37. Implementation Plan Self-Review Requirement

Before coding begins, the coding agent must self-review the implementation plan for:

- Hidden platform coupling.
- Dependency cycles.
- Steps too large to verify independently.
- Tests introduced too late.
- Accidental scope expansion.
- Fake hardware validation.
- Premature abstractions.
- Nondeterministic behavior.
- Implementation occurring before contracts are defined.

Normal engineering choices should be resolved autonomously and documented rather than repeatedly asking the user for approval.

---

# 38. Original Master Build Prompt — Core Intent

The user asked for an English prompt for a coding agent that would not over-prescribe every library, but would force the important architectural constraints.

The resulting prompt defined Catro as a production-grade native desktop communication platform and established these top-level priorities:

1. Real-time quality.
2. Hardware efficiency.
3. Low latency.
4. Reliability.
5. Native OS integration.
6. Security.
7. Maintainability.
8. Premium UX.

It explicitly required:

- Modern C/C++ in performance-critical desktop code.
- No Electron/Chromium/browser-first desktop architecture.
- Cross-platform abstraction only when it does not materially harm native quality/performance.
- Native Windows/macOS shells.
- Hardware-aware behavior.
- Native media/audio/graphics paths.
- Strong performance engineering.
- Modern premium UI.
- Reproducible builds.
- Installers and terminal installation.
- GitHub Releases.
- CI/CD.
- Tests and diagnostics.
- Security as an architectural concern.

The prompt deliberately gave the coding agent autonomy over detailed choices such as:

- Exact internal architecture.
- Libraries.
- Frameworks.
- Threading model.
- Protocol implementation.
- Database later.
- SFU implementation later.
- Codec strategy later.
- Rendering approach.
- Packaging pipeline.
- Repository structure.

while keeping the native/C++/performance/quality requirements non-negotiable.

---

# 39. Original Vertical-Slice Philosophy

The original master prompt insisted that the coding agent should not try to build the whole platform as one giant prototype.

The desired long-term product sequence was:

1. Native application foundation.
2. Hardware capability detection.
3. Audio capture/playback.
4. Two-client low-latency voice call.
5. Native window/game capture.
6. Hardware video encoding.
7. Two-client screen stream.
8. Game/system audio streaming.
9. SFU integration.
10. Server/channel model.
11. Messaging.
12. Complete premium UI.
13. Installer/updater.
14. Cross-platform validation.
15. Profiling/optimization.
16. Release hardening.

The difficult native/media problems should be proven before huge amounts of product/UI code are written.

Benchmark important decisions instead of assuming which implementation is fastest.

---

# 40. Future Performance Engineering Philosophy

When later milestones arrive, engineering should continuously ask:

- Can the operating system perform this more efficiently?
- Can the GPU do this instead of the CPU?
- Can a memory copy be eliminated?
- Can this work move out of a real-time thread?
- Is there a mature protocol/library that already solves this?
- Does this abstraction measurably hurt latency?
- What happens on weak hardware?
- What happens on bad Wi-Fi?
- What happens when a device disappears?
- What happens when the network changes?
- What happens when the application crashes?

Prefer measurable engineering over cleverness for its own sake.

---

# 41. Future Threading / Runtime Performance Direction

The original product requirements emphasized careful design around:

- Thread ownership.
- Scheduling.
- Real-time audio threads.
- Capture threads.
- Network threads.
- Encode pipelines.
- Render threads.
- UI thread isolation.
- Lock contention.
- Memory allocation.
- Buffer reuse.
- Object lifetime.
- Cache behavior.
- Copy avoidance.
- Backpressure.
- Bounded queues.
- Async operations.
- SIMD opportunities.
- Hardware acceleration.
- Power consumption.
- Thermal behavior on laptops.

Avoid unnecessary allocations in real-time loops.

Avoid heavy work on the UI thread.

---

# 42. Future Observability Direction

Later product diagnostics should eventually be capable of showing metrics such as:

- RTT.
- Packet loss.
- Jitter.
- Voice bitrate.
- Video bitrate.
- Codec.
- Encoder.
- Resolution.
- FPS.
- Encode latency.
- Capture latency.
- Dropped frames.
- CPU load.
- GPU load where available.
- Network throughput.

The current foundation diagnostics milestone is a precursor to this broader observability system.

---

# 43. Future Security Direction

Security was defined as part of architecture, not a late add-on.

Future product work should account for:

- TLS.
- Secure authentication.
- Secure session handling.
- Password hashing.
- Rate limiting.
- Server-side authorization.
- Input validation.
- Safe signaling.
- Abuse-resistant APIs.
- Secret handling.
- Signed releases.
- Secure update verification.
- Windows Credential Manager or equivalent.
- macOS Keychain.
- Dependency auditing.
- Supply-chain awareness.

Never invent custom cryptography.

Never trust authorization decisions solely because the client says they are valid.

---

# 44. Product Capabilities Intended Eventually

The original project vision includes eventual support for:

- Accounts.
- Private servers/communities.
- Membership.
- Roles and permissions.
- Text channels.
- Voice channels.
- Direct/group conversations where appropriate.
- Presence.
- Online/offline state.
- Typing state.
- Voice activity.
- Mute/deafen.
- Per-user volume.
- Input/output device selection.
- Push-to-talk.
- Voice activation.
- High-quality microphone processing.
- Screen sharing.
- Individual-window sharing.
- Game sharing.
- Game audio capture.
- System audio capture where appropriate.
- Automatic game/process detection.
- Stream quality selection.
- Adaptive stream quality.
- Fullscreen stream viewing.
- Stream statistics.
- Connection statistics.
- Reconnection.
- Notifications.
- Settings.
- Automatic updates.
- Crash recovery.

These are **future product requirements**, not current-foundation implementation requirements.

---

# 45. Important Scope Discipline

A recurring theme throughout the conversation is:

> **Do not scaffold the whole platform prematurely.**

The user wants a deeply engineered product, but the correct strategy is to prove each hard system increment independently.

This is why the current foundation milestone deliberately spends serious effort on:

- Capability truth.
- Determinism.
- Provenance.
- Explainability.
- Testing.
- Diagnostics.
- Native boundaries.
- Platform correctness.

before media transport or flashy product features are built.

---

# 46. User Preference for Agent Behavior

The user repeatedly requested that the coding model:

- Make normal engineering decisions autonomously.
- Avoid stopping for low-level approval questions.
- Research when needed.
- Choose technically strongest options.
- Keep the architecture very high quality.
- Avoid shortcuts that damage future performance.
- Avoid shallow scaffolding.
- Prefer professional engineering over demo-quality output.
- Use a modern/premium UI approach.
- Keep C/C++ and native engineering central.

The agent should ask for user input only when a decision materially changes the product architecture or product-level behavior.

---

# 47. Current Immediate Next Step

**Do not begin product code yet.**

The immediate expected output from the coding agent is:

> A complete, self-reviewed, dependency-ordered implementation plan for the native-foundation milestone.

That plan should:

- Be broken into small verifiable slices.
- Define exact commit boundaries.
- Define tests and verification commands.
- Keep the repo buildable after each step.
- Include a milestone verification matrix.
- Be self-reviewed for scope, determinism, platform coupling, test timing, and fake hardware validation.

Only after the user approves that implementation plan should the agent begin implementation.

---

# 48. Suggested Handoff Instruction for a Fresh Agent

A fresh coding agent can be given this document together with the repository and told:

```text
Read this entire context file before making any architectural or implementation decisions.

Treat the reported Specification v1.0.0 and the architectural rules in this document as authoritative project context unless the repository's actual committed specification contains a direct contradiction. If the repository specification differs, surface the contradiction explicitly rather than silently choosing one.

Do not begin product code until you have inspected the current repository state and produced the implementation plan required by this document.

Preserve Catro's core constraints:
- native-first Windows/macOS application,
- modern C++ shared systems core,
- native platform shells,
- capability-driven hardware decisions,
- deterministic policy,
- explicit uncertainty/provenance,
- independently verifiable milestones,
- premium UI quality,
- no Electron/Qt/browser runtime,
- no scope leakage into server/auth/WebRTC/media streaming during the current foundation milestone.
```

---

# 49. Compact Architectural Summary

If only one section is read, this is the essence:

```text
CATRO
│
├─ Windows native app
│  ├─ WinUI 3
│  ├─ C++/WinRT
│  └─ Windows platform probes
│
├─ macOS native app
│  ├─ SwiftUI/AppKit
│  ├─ narrow Objective-C++ bridge
│  └─ macOS platform probes
│
├─ shared modern C++ core
│  ├─ strongly typed capability model
│  ├─ evidence/provenance
│  ├─ immutable snapshots
│  ├─ validation
│  ├─ deterministic policy
│  ├─ MediaPlan + ordered fallbacks
│  ├─ bounded explainability traces
│  └─ deterministic serialization
│
├─ probe orchestration
│  ├─ off-UI-thread
│  ├─ isolated helpers
│  ├─ hard timeouts
│  ├─ partial failure support
│  └─ classified refreshes
│
├─ engineering diagnostics
│  ├─ native Windows view
│  ├─ native macOS view
│  └─ human + canonical JSON report
│
└─ CURRENT MILESTONE EXCLUDES
   ├─ server
   ├─ auth
   ├─ messaging
   ├─ WebRTC
   ├─ real capture sessions
   ├─ voice pipeline
   ├─ production encoder activation
   └─ streaming transport
```

---

# 50. Final Project Principle

The entire project should be guided by this principle:

> **Catro is not a web app wrapped for desktop. It is a native systems application whose architecture should exploit each machine and operating system intelligently, deterministically, and measurably to deliver excellent communication quality with minimal unnecessary overhead.**

