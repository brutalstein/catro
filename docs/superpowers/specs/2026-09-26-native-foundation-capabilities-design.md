# Catro Native Foundation and Capability System Design

**Specification version:** 1.0.0

**Capability schema baseline:** 1.0

**Policy baseline:** 1.0.0

**Status:** Approved architectural baseline

**Date:** 2026-09-26

## 1. Purpose and scope

This milestone establishes Catro's long-term native client foundation and its hardware-capability subsystem. It produces two genuinely native desktop shells, a lightweight shared C++ core, passive platform probes, immutable normalized capability snapshots, deterministic local media planning, and useful engineering diagnostics.

Catro is centrally hosted first. The client must not hard-code future authentication, signaling, messaging, media routing, storage, or discovery around one deployment, but none of those services are implemented in this milestone.

The milestone succeeds when later audio, capture, encoding, WebRTC, diagnostics, and networking components can consume the same validated capability snapshot and intended media plan without querying operating-system APIs themselves.

## 2. Architecture and component boundaries

The product uses split-native shells over a shared modern C++ core:

```text
Windows WinUI 3 / C++/WinRT ---- Windows adapter --+
                                                   |
macOS SwiftUI/AppKit -------- Objective-C++ adapter +--> capability service
                                                   |          |
                                                   |          v
                                                   +--> immutable snapshot
                                                              |
                                         +--------------------+------------------+
                                         |                    |                  |
                                         v                    v                  v
                                     validator          policy engine      diagnostics model
```

The boundaries are:

- `core/capabilities`: strongly typed platform-independent domain model, validation, snapshot differencing, policy inputs, policy rules, media plans, and bounded decision traces.
- `core/reporting`: human-readable and canonical JSON projections. This layer depends on `core/capabilities`; the capability core does not depend on JSON.
- `platform/windows`: Windows probe implementations and process orchestration using native APIs.
- `platform/macos`: macOS probe implementations and process orchestration using native APIs.
- `apps/windows`: WinUI 3 shell, Windows lifecycle, and native diagnostics presentation.
- `apps/macos`: SwiftUI/AppKit shell, macOS lifecycle, and native diagnostics presentation.
- `tools/capability-probe`: internal passive probe helper used to isolate every probe family from the application process.
- `tools/capability-report`: headless human and machine-readable reporting tool.
- `tests`: core contract, policy, serialization, probe orchestration, platform smoke, and golden-fixture tests.

The shared core contains no Win32, WinRT, COM, Objective-C, Swift, AppKit, CoreFoundation, Metal, DirectX, or native UI types. Platform adapters translate native results into C++ domain values at their boundary.

Windows C++ code may consume the core's C++ types directly. macOS exposes only a narrow Objective-C++ presentation adapter to Swift. Swift, Objective-C reference counting, CoreFoundation ownership, and Metal objects never cross into the shared C++ API. The adapter returns native presentation models or owns conversions while the authoritative snapshot remains C++.

Internal boundaries are source-level and static-library boundaries. Catro does not introduce a stable binary ABI or ABI-sensitive shared-library surface in this milestone.

## 3. Capability domain model

### 3.1 Snapshot structure

The authoritative model is strongly typed C++, not a key/value bag and not the JSON representation:

```cpp
struct CapabilitySnapshot {
    SnapshotHeader header;
    PlatformIdentity platform;
    HardwareCapabilities hardware;
    DeviceInventory devices;
    RuntimeState runtime;
    std::vector<ProbeRecord> probes;
    std::vector<ProbeIssue> issues;
};
```

`SnapshotHeader` contains the schema identifier `catro.capabilities`, major and minor schema versions, a process-local generation number, capture time, and probe implementation revision. Generation numbers are monotonic only within one capability-service lifetime.

The major schema version changes only for incompatible domain changes. Minor changes are additive. Fixtures, exports, validation, and reporting always declare the schema version they use.

### 3.2 Typed identifiers and identity scope

The model defines distinct `GpuId`, `EncoderId`, `CapturePathId`, `DisplayId`, and `AudioEndpointId` types. Each ID declares its stability scope as one of:

- snapshot generation;
- capability-service process lifetime;
- operating-system session;
- persistent, only when the underlying API provides a documented persistent identity.

The model never promises cross-reboot identity based on pointer values, enumeration positions, display order, transient adapter indexes, or undocumented identifiers.

### 3.3 Hardware, inventory, and runtime domains

`HardwareCapabilities` contains mostly static facts:

- native and process CPU architecture;
- physical, logical, performance, and efficiency core counts when known;
- relevant SIMD capabilities;
- process translation state, including Rosetta where detectable;
- installed memory and documented platform limits.

`DeviceInventory` contains replaceable inventories:

- zero or more GPUs;
- zero or more encoder implementations and their concrete modes;
- zero or more source-specific capture paths;
- zero or more displays;
- zero or more audio input and output endpoints;
- zero or more capture-to-encoder transfer relationships.

`RuntimeState` remains conceptually separate from hardware facts:

- power source and battery presence;
- low-power state;
- thermal pressure;
- memory pressure;
- headless or remote-session state;
- current display modes, device availability, capture permission, and default audio roles keyed by typed device IDs.

### 3.4 GPU, display, encoder, capture, and audio objects

`GpuCapability` records a snapshot-scoped ID, native vendor and device identity where appropriate, integrated/discrete/external/software classification, memory facts, native graphics API support, adapter roles, and power preference. Product policy does not infer performance from a marketing name.

`DisplayCapability` records its GPU association when reliably known, supported physical and logical modes, scale factors, exact rational refresh rates, HDR support, and color characteristics. `RuntimeState` records the active mode, current scale, availability, and primary-display role. Multiple displays and mixed refresh rates are ordinary cases.

`EncoderCapability` identifies its backend, GPU association when known, hardware or software implementation class, and a collection of concrete `EncoderModeCapability` values. Codec support does not imply that all modes for that codec work.

Each concrete encoder mode can represent:

- codec and profile where advertised;
- minimum and maximum dimensions;
- exact rational frame-rate ranges;
- input pixel format and chroma subsampling;
- bit depth;
- full or limited color range;
- transfer function and HDR mode;
- latency mode;
- evidence for every advertised or validated constraint.

Unknown mode limits remain unknown. They do not justify aggressive local ceilings.

`CapturePathCapability` is source-specific. Display, window, and application/game capture modes may have different constraints even when they share an operating-system backend. It records supported source kinds, output formats, color characteristics, GPU affinity when known, and whether passive discovery can establish the fact. Current permission state lives in `RuntimeState`.

`AudioEndpointCapability` records direction, channel counts, and advertised sample formats. `RuntimeState` records presence and current default roles. This milestone enumerates endpoints only; it does not open devices or perform audio processing.

### 3.5 Transfer relationships

Zero-copy is a relationship, not an encoder boolean:

```cpp
struct TransferPathCapability {
    CapturePathId source;
    EncoderId destination;
    std::optional<GpuId> source_gpu;
    std::optional<GpuId> destination_gpu;
    TransferKind transfer;
    ConversionRequirement conversion;
    SupportEvidence evidence;
};
```

`TransferKind` distinguishes native/same-resource flow, same-adapter copy, cross-adapter copy, CPU staging, and unknown. `ConversionRequirement` describes required pixel-format, color-space, bit-depth, range, scaling, or HDR/SDR conversion without implementing those conversions.

## 4. Evidence and provenance semantics

Facts use typed knowledge and support states:

```cpp
enum class Knowledge { known, unknown, unavailable };
enum class Support { supported, unsupported, unknown };
enum class EvidenceMethod { measured, advertised, probe_validated, inferred, cached };
enum class Confidence { high, degraded };
```

An `Observed<T>` contains `Knowledge`, an optional typed value, and provenance. A `SupportFact` contains `Support` and provenance. Provenance includes a stable probe identifier, evidence method, confidence, observation generation, and a stable issue code when evidence is degraded or absent.

The meanings are:

- `advertised`: a documented platform or driver API reports support.
- `probe_validated`: a passive, non-intrusive query validated a specific fact without starting a production workload.
- `measured`: the probe directly measured a value such as memory capacity or refresh rate.
- `inferred`: Catro derived the value from other facts; it is always labeled and never outranks direct evidence.
- `cached`: a future cache supplied the fact. This milestone defines the evidence state but does not add a persistent capability cache.

Runtime media activation is separate. A future media subsystem will emit per-attempt activation results such as success, rejected configuration, driver failure, resource exhaustion, or timeout. Those records refer to plan candidate IDs and conditions. They do not rewrite immutable hardware capability or turn one conditional runtime failure into permanent unsupported hardware.

This milestone does not activate production encoders or capture sessions. Consequently, advertised encoder support may remain advertised rather than probe-validated.

## 5. Platform probe contracts

Every probe has a stable identifier, family, passive/intrusive classification, hard budget, implementation revision, and declared output domains. Every scheduled probe produces a `ProbeRecord`; the parent synthesizes the record when a helper times out or terminates before returning output. Each record contains:

- stable probe identifier;
- monotonic duration;
- outcome;
- failure classification and native error code when safe to expose;
- whether output was complete or partial;
- count of facts produced;
- implementation revision.

Probe outcomes include success, partial, API unavailable, permission unavailable, timeout, operating-system failure, malformed output, and helper termination.

Passive discovery must not:

- display privacy or permission prompts;
- start screen, window, game, microphone, or system-audio capture;
- open a persistent audio stream;
- submit an encoder workload;
- change a device, power, display, or system setting;
- persist hardware identifiers or modify the machine.

Any future intrusive probe requires a separate explicit activation API and user-visible intent. It cannot be added to passive startup discovery.

### 5.1 Bounded publication and driver isolation

All passive probe families run in isolated `catro-capability-probe` helper processes. The parent coordinator launches probe families concurrently, accepts only complete validated fragments, and enforces hard deadlines by terminating an overdue helper. This applies even to nominally low-risk platform queries because an in-process thread cannot enforce a hard cancellation guarantee against a blocked operating-system call. A blocked API or driver therefore cannot indefinitely block the UI process, application startup, or snapshot publication.

Initial default budgets are:

| Probe family | Hard budget |
| --- | ---: |
| Platform/CPU/memory | 500 ms |
| Runtime power/thermal state | 500 ms |
| GPU and display inventory | 1,500 ms |
| Encoder advertisement | 1,500 ms |
| Audio endpoint inventory | 1,000 ms |

The first coherent snapshot has a 2,000 ms publication deadline because families execute concurrently. Completed families are merged; overdue families are marked unknown with timeout records. A manual or event-driven refresh may try them again. Budgets are internal versioned constants, not user preferences, and changes require probe-orchestration tests.

The helper protocol carries versioned domain fragments. JSON may be used as the process transport because it is already validated at the boundary, but it does not become the authoritative domain model. Malformed, incompatible, duplicate, or referentially invalid fragments are rejected and reported.

### 5.2 Windows passive probes

Windows probes use documented native APIs for:

- OS, processor topology, architecture, memory, power, and platform role;
- DXCore and DXGI adapter discovery, including adapter LUID relationships;
- Display Configuration data with exact rational refresh rates;
- Media Foundation hardware-transform enumeration for advertised encoders and modes;
- MMDevice enumeration for audio endpoints;
- runtime availability checks for Windows Graphics Capture and other future capture backends.

Windows 11 is the primary target. Windows 10 22H2 x64 is supported where required APIs are present. Missing APIs become explicit unavailable evidence in the Windows adapter; the shared core contains no Windows-version compatibility branches.

### 5.3 macOS passive probes

macOS probes use documented native APIs for:

- processor and architecture facts through `sysctl` and `ProcessInfo`;
- Metal device enumeration and supported GPU families;
- CoreGraphics display facts;
- VideoToolbox encoder advertisement and property queries;
- CoreAudio endpoint enumeration;
- ScreenCaptureKit availability and non-prompting permission status;
- low-power, power-source, thermal, and memory-pressure state.

The deployment baseline is macOS 13 or later. Release builds cover Apple Silicon and Intel. When public APIs cannot establish a reliable relationship, such as some display-to-GPU mappings on Intel Macs, the relationship remains unknown.

## 6. Snapshot lifecycle and refresh semantics

The capability service publishes immutable generations. Generation 1 is the first coherent snapshot, even if one or more probe families timed out or failed. Consumers never observe a partially mutated snapshot.

Refresh triggers include:

- explicit diagnostic refresh;
- audio endpoint inventory or default-role change;
- display topology change;
- power-source or low-power change;
- thermal or memory-pressure change;
- session/headless state change;
- future GPU/device notifications supported by the platform.

The coordinator refreshes only affected families where safe, revalidates the merged snapshot, increments the generation, and publishes a `SnapshotUpdate` containing the replacement snapshot and `ChangeSet`.

`ChangeSet` classifies GPU, encoder, display, audio input, audio output, capture permission, power, thermal, memory pressure, platform/session, and validation changes. It includes affected typed IDs where their scope permits comparison. Event bursts are debounced at the platform-service boundary; they do not create an unbounded history in the core.

## 7. Validation invariants

Validation runs before policy evaluation, serialization, UI publication, and cross-process fragment merge. It reports stable typed validation errors.

Required invariants include:

- `known` observations contain exactly one value;
- `unknown` and `unavailable` observations contain no value;
- identifiers are unique in their domain and scope;
- every referenced GPU, display, encoder, and capture path exists;
- optional GPU references are absent rather than fabricated for software or unmappable implementations;
- rational denominators are positive and ratios are reduced;
- dimensions, channel counts, memory sizes, and frame-rate bounds are internally valid;
- minimum values do not exceed maximum values;
- encoder modes agree with their parent codec and implementation class;
- transfer endpoints and conversion declarations are valid;
- support, evidence, and confidence states do not contradict one another;
- schema and policy inputs use supported versions;
- snapshot generations are positive and monotonic within one service lifetime;
- policy candidate and trace identifiers are unique and bounded.

Snapshots containing structural contradictions are not published. Probe failures and missing evidence are valid partial snapshots, not structural contradictions.

## 8. Policy engine contract and rule precedence

The policy engine is a pure C++ function:

```cpp
MediaPlan derive_media_plan(
    const CapabilitySnapshot& snapshot,
    const MediaDecisionRequest& request,
    PolicyVersion policy_version);
```

The request contains media-domain facts rather than UI state: source kind, selected display when applicable, latency class, operating preference, and requested quality. This milestone's local policy does not accept network or peer constraints. A later negotiation layer consumes the local quality envelope and can only narrow it; it does not require changing the capability model or local policy contract.

Operating profile is a policy output derived from runtime facts and explicit user preference. It is not a mutable global mode. Profiles are performance, balanced, efficiency, thermal-constrained, and safe-local-envelope.

Policy rules are strongly typed declarative tables with stable rule IDs, documented priority, and a separately versioned policy revision. They do not use vendor or product names as performance proxies. An isolated compatibility table may be introduced later only for measured driver defects, with evidence and tests.

Rule precedence is:

1. Reject invalid snapshots, requests, and incompatible schema or policy versions.
2. Apply hard request constraints.
3. Remove known-unsupported candidates.
4. Prefer known supported evidence over unknown evidence.
5. Preserve selected display to capture GPU to encoder GPU affinity where known.
6. Prefer native/same-resource paths, then same-adapter copies, then more expensive transfers.
7. Prefer measured or probe-validated low-latency hardware modes over merely advertised or inferred modes.
8. Derive operating profile from explicit power, battery, thermal, memory, platform-role, and preference facts.
9. Apply the profile's local resolution, frame-rate, bit-depth, HDR, and power ceilings.
10. Select a conservative starting profile within the known local envelope.
11. Rank remaining valid candidates as ordered fallbacks.

Tie-breaking is lexicographic and stable. It never uses unordered container iteration, pointer identity, operating-system enumeration order, locale, or random values.

The same validated snapshot, request, and policy version always produce byte-identical canonical plans and traces.

Policy-version changes that intentionally alter selections, rejection reasons, or fallback order require corresponding fixture changes and a concise decision record in architecture documentation.

## 9. MediaPlan and fallback semantics

`MediaPlan` is the best intended local plan, not an activation guarantee. It contains:

- capability schema and policy versions;
- operating profile and facts that produced it;
- requested quality;
- achievable local quality envelope;
- selected capture, transfer, GPU, encoder, codec, and concrete-mode candidates;
- conservative starting quality;
- resolution and exact rational frame-rate limits;
- power and thermal downgrade behavior;
- ordered alternative candidates;
- bounded structured decision traces.

Negotiated quality is a future result constrained by peer and network state. It is not stored as locally achievable capability.

Software encoding is an explicit candidate with explicit CPU, power, latency, and quality consequences. `safe-local-envelope` does not silently select software. If no valid candidate exists, the plan reports no viable local path and explains why.

Future activation code will attempt the selected candidate, return a structured per-attempt result, and advance through the existing ordered fallback chain. An activation failure is associated with that attempt and its conditions. It does not contaminate or mutate the capability snapshot.

### 9.1 Explainability and bounds

Each meaningful decision records the selected candidate, stable rule ID, facts used, considered alternatives, stable rejection reason codes, confidence degradation, and next fallback.

Every decision category retains at most 32 ranked candidates and eight reasons per candidate. The complete plan retains at most 256 trace records. Deterministic truncation records the omitted count and never discards the selected candidate or the first rejection reason for a retained fallback. Re-evaluation replaces the trace; it never appends historical evaluations.

## 10. Diagnostics and reporting contract

Both native shells present an engineering diagnostics workspace containing:

- snapshot generation, schema version, policy version, capture time, and total probe duration;
- every probe's ID, duration, outcome, failure classification, and fact count;
- partial-probe issues and degraded-confidence indicators;
- processor, memory, power, thermal, and session state;
- GPU and display topology with exact scaling and rational refresh rates;
- encoder implementations and concrete advertised modes;
- source-specific capture capabilities and permission state;
- transfer costs and required color conversions;
- requested quality, local quality envelope, intended plan, and fallback order;
- expandable selection and rejection reasons;
- classified changes from the previous snapshot generation;
- manual refresh, human-readable copy/export, and canonical JSON export.

The Windows UI uses WinUI 3 adaptive layout, native typography, semantic colors, keyboard navigation, high-DPI behavior, accessible labels, and dark/light adaptation. The macOS UI uses SwiftUI/AppKit conventions, Retina-aware layout, keyboard navigation, accessible labels, and native appearance adaptation. Neither shell uses disposable default-control layouts or web content.

`catro-capability-report` supports human output and canonical JSON output. Both include the validated snapshot, probe records and issues, policy version, a representative explicit request, intended plan, fallback chain, and bounded trace.

## 11. Deterministic serialization rules

Serialization is a projection from the typed domain. The core capability model has no JSON dependency.

Canonical JSON follows these rules:

- fixed schema identifier and explicit schema/policy versions;
- fixed object field order controlled by Catro serializers;
- stable string encodings for enums and issue codes;
- explicit representations for unknown and unavailable values;
- deterministic sorting of collections whose order is not semantically meaningful;
- semantic ranking order preserved for candidates and fallbacks;
- reduced rational values serialized as integer numerator and denominator;
- byte and duration quantities serialized as integers with declared units;
- no floating-point formatting for exact facts;
- locale-independent UTF-8, escaping, indentation, and newline behavior;
- no pointer values, random IDs, address-derived values, or raw STL iteration order;
- serializers never consult the wall clock; recorded snapshot timestamps use one fixed UTC integer representation, and golden fixtures either fix or explicitly omit volatile timestamp fields;
- privacy-sensitive identifiers omitted or redacted before serialization.

Round trips preserve domain meaning, evidence, provenance, exact rates, color information, transfer relationships, rejection reasons, and fallback ordering.

## 12. Testing and fixture strategy

Core tests cover:

- all validation invariants and malformed-reference cases;
- schema and policy version compatibility;
- canonical serialization, round trips, and byte determinism;
- snapshot differencing and change classification;
- policy purity and repeated-output identity;
- transfer-cost and GPU-affinity ranking;
- source-specific capture constraints;
- color, bit-depth, HDR, conversion, and rational-rate preservation;
- advertised versus probe-validated evidence;
- explicit software-encoder consequences;
- bounded trace truncation;
- partial snapshot behavior.

Golden policy fixtures include:

- a high-performance NVIDIA desktop;
- an integrated Intel laptop on battery;
- a hybrid-GPU Windows laptop;
- an Apple Silicon MacBook under normal and thermal pressure;
- an older Intel Mac with incomplete GPU affinity evidence;
- multiple encoders attached to one or more GPUs;
- software-only encoding;
- unknown codec mode limits;
- missing or broken drivers;
- headless and remote sessions;
- no microphone or no output endpoint;
- mixed 59.94, 60, 120, and 144 Hz displays;
- partial failure and timeout in every probe family.

Fixtures assert the selected plan, important rejection reasons, transfer decisions, and complete fallback order. Intentional policy changes update the policy version, affected fixtures, and the documented decision together.

Probe orchestration tests use deterministic fake helpers to prove concurrent completion, hard timeouts, process termination, malformed-fragment rejection, global-deadline publication, and coherent partial snapshots. Platform smoke tests execute passive probes on Windows, Apple Silicon macOS, and Intel macOS, then validate their snapshots without prompting or changing system state.

The shared core is tested with strict warnings, address sanitization, and undefined-behavior sanitization where supported. Platform adapters receive focused native contract tests. Native shells receive view-model and launch/build smoke coverage; this milestone does not add brittle pixel-perfect UI automation.

## 13. Repository and build boundaries

The initial repository layout is:

```text
/apps/windows
/apps/macos
/core/capabilities
/core/reporting
/platform/windows
/platform/macos
/tools/capability-probe
/tools/capability-report
/tests/capabilities
/tests/fixtures
/docs/architecture
/docs/superpowers/specs
/scripts
/cmake
```

Shared libraries, platform probe libraries, tools, and tests use C++20 and CMake presets. CMake is authoritative for shared C++ targets. Windows uses the native Visual Studio/MSBuild and C++/WinRT toolchain for its WinUI 3 application. macOS uses Xcode for its SwiftUI/AppKit application and the narrow Objective-C++ adapter.

The capability core links no UI, networking, capture, WebRTC, or platform framework. Reporting dependencies do not propagate into the core. Third-party versions and integrity information are pinned, licenses are recorded, and dependencies are limited to demonstrated needs.

Windows compilation uses strict conformance and warnings-as-errors for Catro code. Apple compilation uses equivalent strict Clang diagnostics. CMake presets expose configure, build, test, sanitizer, and report-tool workflows without silently installing or modifying developer tooling.

Release-oriented macOS builds target macOS 13 or later for Apple Silicon and Intel. Windows 11 x64 is primary; Windows 10 22H2 x64 is capability-driven. Windows ARM64 packaging is outside this milestone.

## 14. Platform-specific implementation notes

### 14.1 Windows

The WinUI shell owns application lifecycle, windows, themes, accessibility, and UI-thread dispatch. Native probes and helper-process control live below the shell. COM apartments and Media Foundation startup/shutdown are contained in Windows implementation units and never appear in core headers.

Adapter LUIDs provide relationships where documented. Hybrid-GPU machines are not collapsed into a single preferred GPU. Missing Windows 10 APIs become unavailable facts; the Windows adapter does not emulate support or contaminate shared policy with OS-version checks.

### 14.2 macOS

Swift owns the app lifecycle and SwiftUI/AppKit view hierarchy. Objective-C++ owns all conversion between C++ values and Swift-facing presentation models. CoreFoundation retain/release rules, Objective-C ARC, Swift ownership, Metal objects, and native callbacks terminate at that adapter.

Apple Silicon unified memory is represented from measured platform facts rather than forced into discrete-GPU assumptions. Intel Mac GPU/display relationships remain unknown when public APIs cannot prove them. Universal release builds and architecture-specific smoke tests are distinct verification gates.

## 15. Security and privacy considerations

Capability discovery follows least privilege and data minimization:

- no passive probe prompts for protected access;
- no secrets, user content, account state, window titles, application lists, captured frames, audio samples, or message data enter snapshots;
- persistent device serial numbers, EDIDs, raw registry paths, and other unnecessary fingerprinting data are excluded;
- device display names are included only when they materially aid debugging and are redacted from default bug-report export when more identifying than necessary;
- native error details are normalized to stable categories, with raw codes included only when safe and useful;
- helper-process output is size-bounded, version-checked, parsed as untrusted input, and validated before merge;
- helper executables inherit no more environment or filesystem authority than required;
- canonical reports clearly mark redaction and never imply anonymity.

Future upload of reports requires explicit user action and a separate security review. This milestone writes only user-requested local files.

## 16. Explicit non-goals

This milestone does not implement:

- media capture sessions or captured frames;
- production encoder creation, workloads, or runtime activation;
- microphone capture, playback, DSP, echo cancellation, or voice activity detection;
- WebRTC, RTP, ICE, STUN, TURN, congestion control, or SFU integration;
- accounts, authentication, authorization, servers, databases, messaging, presence, or community models;
- central service discovery or private-deployment management;
- negotiated peer/network quality;
- persistent device compatibility blacklists or capability caches;
- full product navigation or channel/voice-room interfaces;
- installers, auto-update, signing, notarization, Homebrew, or WinGet distribution;
- federation, multi-region control planes, or production telemetry;
- Qt, Electron, Chromium, embedded browser, or web-based UI/runtime layers;
- Windows ARM64 release support.

Interfaces are limited to what the foundation requires. Future subsystem implementations do not enter this milestone under the guise of extensibility.

## 17. Definition of done

The milestone is complete only when:

1. Shared libraries, platform adapters, tools, and native shells build cleanly on Windows x64, Apple Silicon macOS, and Intel macOS targets.
2. All core, serialization, policy, timeout, partial-probe, change-classification, and pathological-fixture tests pass.
3. Real passive platform probes produce structurally valid snapshots without privacy prompts or system changes.
4. Slow or blocked probe helpers cannot delay first snapshot publication beyond the global deadline.
5. Both native diagnostics views display real capability evidence, provenance, probe timing, policy output, rejection reasons, and fallback ordering.
6. Human-readable and canonical JSON reports are generated and regression-tested.
7. Identical snapshot, request, and policy versions produce byte-identical canonical plans and traces.
8. No capability is silently inferred from a vendor or model name.
9. Windows 10 and older Intel Mac limitations appear as explicit unavailable or unknown evidence rather than shared-core compatibility hacks.
10. The repository includes concise architecture, schema, build, developer setup, and troubleshooting documentation for this milestone.
11. Strict compiler diagnostics and supported sanitizers pass for Catro-owned code.
12. No explicit non-goal has leaked into product code or dependencies.

Any unavailable hardware or CI environment is reported as an unverified gate; it cannot be represented as passing evidence.

## 18. Known risks and intentionally deferred questions

### Known risks

- Operating-system and driver APIs may advertise incomplete or inaccurate encoder mode limits. The model preserves the evidence level, and later runtime activation remains authoritative for one attempt.
- Mapping a capture source, display, GPU, and encoder may be incomplete on some Intel Macs or hybrid systems. Unknown relationships reduce policy confidence rather than triggering guesses.
- Hard probe deadlines require correctly packaged and signed helper processes, especially inside a macOS application bundle.
- Helper-process startup cost may be material on older hardware. Probe timings are visible, budgets are tested, and families run concurrently.
- WinUI 3 C++ and Xcode/CMake integration create two native build paths. The shared C++ targets remain authoritative and both platform builds are explicit gates.
- Intel macOS CI capacity may be less available than Apple Silicon capacity. Intel support remains unverified until an actual Intel build and smoke run completes.
- Canonical JSON is useful for regression evidence but can increase fingerprinting risk. Default exports minimize identifiers and remain local.

### Intentionally deferred questions

- How runtime activation results influence retries within one media session.
- Whether measured driver defects justify a versioned compatibility table.
- How peer codec support and network estimation constrain the local plan.
- How negotiated quality feeds live adaptation.
- Which capture and encoder APIs win real latency and power benchmarks.
- Whether a persistent capability cache is worthwhile after startup measurements exist.
- Windows ARM64 release timing.
- Installer, updater, signing, notarization, and distribution design.

These questions require later media, deployment, or measured-performance milestones. They do not change the foundation boundaries defined here.
