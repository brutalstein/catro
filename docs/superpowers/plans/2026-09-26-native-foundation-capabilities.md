# Catro Native Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build Catro's native Windows/macOS application foundation, passive hardware-capability service, deterministic local media policy, diagnostics workspace, and reporting tools without implementing any media or service subsystem.

**Architecture:** A platform-independent C++20 core owns the typed capability model, validation, snapshot differencing, policy, and plans. Windows and macOS helper processes perform bounded passive native probes; native shells own lifecycle and diagnostics presentation. JSON is a reporting and helper-transport projection outside the authoritative capability core.

**Tech Stack:** C++20, CMake 3.28+, Catch2 3.15.3, nlohmann/json 3.12.0, Windows App SDK 2.5.1 with C++/WinRT and WinUI 3, SwiftUI/AppKit with a narrow Objective-C++ bridge, Xcode, MSBuild, GitHub Actions.

**Spec:** `docs/superpowers/specs/2026-09-26-native-foundation-capabilities-design.md`

## Global Constraints

- Capability schema is `catro.capabilities` version 1.0; policy version is 1.0.0.
- Use C++20. The shared capability core must not include or link native UI, OS, networking, capture, encoder, or WebRTC frameworks.
- Windows 11 x64 is primary; Windows 10 22H2 x64 behavior is capability-driven. Pin stable Windows App SDK 2.5.1.
- macOS deployment target is 13.0; build and test both arm64 and x86_64.
- Pin Catch2 to commit `95d8a61b089317bec800c7cc4c64064cbcb3802d` and nlohmann/json to commit `65ee68451d8eb2b5f3a30b410476ab83deb3289b`.
- Passive discovery must not prompt, capture, open persistent devices, submit encoder workloads, mutate system state, or persist hardware identifiers.
- Every scheduled probe produces a timed record. Isolated helpers enforce the specification's per-family budgets and 2,000 ms first-publication deadline.
- Unknown and unavailable evidence remain explicit. Vendor/model names must not substitute for capability evidence.
- The same validated snapshot, request, and policy version must produce byte-identical canonical plans and traces.
- No Qt, Electron, Chromium, embedded browser, or web UI/runtime dependency.
- Do not implement authentication, servers, messaging, WebRTC, live capture, audio processing, production encoder activation, negotiation, packaging, updating, or full product navigation.

## Review Focus

- A helper that hangs forever must be terminated and produce a coherent partial snapshot before the 2,000 ms deadline; Tasks 6, 7, and 12 pin this behavior.
- Invalid evidence or dangling GPU/display/encoder references must never reach policy or diagnostics; Task 2 pins every structural invariant.
- OS enumeration order, locale, timestamps, and hash-container order must not change canonical reports or plans; Tasks 4 and 5 pin byte determinism.
- Partial and unknown evidence must lower confidence, preserve rejection reasons, and keep fallback order deterministic; Tasks 3 and 4 pin pathological cases.
- Missing APIs, permissions, devices, or drivers must yield unavailable/unknown evidence without prompts or crashes; Tasks 7–9 and 12–13 pin platform behavior.

---

## Planned File Structure

```text
CMakeLists.txt                         C++ build entry point
CMakePresets.json                     Windows/macOS configure, build, and test presets
cmake/Dependencies.cmake              Pinned reporting/test dependencies
cmake/Warnings.cmake                  Strict Catro-owned warning policy

core/capabilities/                    Authoritative platform-neutral model
  include/catro/capabilities/
    version.hpp                       Schema and policy version types/constants
    ids.hpp                           Strong scoped device/candidate IDs
    rational.hpp                      Exact normalized rational values
    evidence.hpp                      Knowledge, support, provenance, observations
    model.hpp                         Snapshot and typed capability domains
    validation.hpp                    Structural validation contract
    snapshot_diff.hpp                 Classified immutable snapshot changes
    media_plan.hpp                    Local plan, fallbacks, bounded traces
    policy.hpp                        Pure policy request and entry point
    probe.hpp                         Probe schedules, fragments, and executor contract
    probe_coordinator.hpp             Shared concurrent schedule/merge logic
  src/                                Matching implementations

core/reporting/                       Non-authoritative report projections
  include/catro/reporting/
    report.hpp                        Snapshot/request/plan report aggregate
    canonical_json.hpp                Deterministic JSON encode/decode
    human_report.hpp                  Human-readable diagnostics report
  src/

platform/windows/                     Win32/WinRT/Media Foundation/MMDevice probes
platform/macos/                       Objective-C++/Apple framework probes
tools/capability-probe/               Isolated internal probe helper executable
tools/capability-report/              Headless diagnostics/export executable
tests/capabilities/                   Core and reporting tests
tests/fixtures/                       Golden pathological machine reports
tests/helpers/                        Fake and hanging probe helpers
tests/windows/                        Windows native contracts
tests/macos/                          macOS native contracts
apps/windows/Catro/                   WinUI 3 shell and diagnostics view
apps/macos/Catro/                     SwiftUI/AppKit shell and bridge
scripts/                              Non-mutating build/test/run entry points
.github/workflows/ci.yml              Windows, Apple Silicon, and Intel gates
docs/                                 Architecture, build, and troubleshooting docs
```

### Task 1: Build Foundation and Authoritative Domain Types

**Files:**
- Create: `.gitignore`
- Create: `CMakeLists.txt`
- Create: `CMakePresets.json`
- Create: `cmake/Dependencies.cmake`
- Create: `cmake/Warnings.cmake`
- Create: `core/capabilities/CMakeLists.txt`
- Create: `core/capabilities/include/catro/capabilities/version.hpp`
- Create: `core/capabilities/include/catro/capabilities/ids.hpp`
- Create: `core/capabilities/include/catro/capabilities/rational.hpp`
- Create: `core/capabilities/include/catro/capabilities/evidence.hpp`
- Create: `core/capabilities/include/catro/capabilities/model.hpp`
- Create: `core/capabilities/src/rational.cpp`
- Create: `tests/CMakeLists.txt`
- Create: `tests/capabilities/model_test.cpp`

**Interfaces:**
- Produces: `SchemaVersion`, `PolicyVersion`, `ScopedId<Tag>`, `Rational`, `Observed<T>`, `SupportFact`, stable `ProbeRecord`/`ProbeIssue` domain values, `CapabilitySnapshot`, and all typed CPU/GPU/display/encoder/capture/audio/runtime records described by the spec.
- Consumes: Only the C++ standard library.

- [ ] **Step 1: Add the root CMake build and pinned dependencies**

Set `cmake_minimum_required(VERSION 3.28)`, require C++20 without compiler extensions, add `CATRO_BUILD_TESTS`, and fetch the exact dependency commits from Global Constraints. Apply warnings-as-errors only to Catro targets, not third-party code.

- [ ] **Step 2: Write failing domain tests**

```cpp
TEST_CASE("rational preserves 59.94 exactly") {
    const Rational rate{60000, 1001};
    REQUIRE(rate.numerator() == 60000);
    REQUIRE(rate.denominator() == 1001);
}

TEST_CASE("known evidence requires an explicit typed value") {
    const auto memory = Observed<Bytes>::known(Bytes{16_GiB}, measured("windows.system.v1"));
    REQUIRE(memory.value() == Bytes{16_GiB});
    REQUIRE(memory.provenance().method == EvidenceMethod::measured);
}
```

- [ ] **Step 3: Run the build to verify the tests fail**

Run: `cmake --preset windows-msvc && cmake --build --preset windows-debug`

Expected: compilation fails because the capability headers and types are not implemented.

- [ ] **Step 4: Implement the minimal strongly typed domain model**

Implement normalized `Rational` with a positive denominator, distinct ID tag types, explicit identity scope, evidence/provenance values, static hardware, replaceable inventory, runtime state, concrete encoder modes, color characteristics, and transfer relationships. Do not add serialization or platform headers.

- [ ] **Step 5: Run focused tests**

Run: `ctest --preset windows-debug -R catro_model --output-on-failure`

Expected: all model tests pass.

- [ ] **Step 6: Commit**

```bash
git add .gitignore CMakeLists.txt CMakePresets.json cmake core/capabilities tests
git commit -m "feat: add typed capability domain"
```

### Task 2: Snapshot Validation and Classified Changes

**Files:**
- Create: `core/capabilities/include/catro/capabilities/validation.hpp`
- Create: `core/capabilities/include/catro/capabilities/snapshot_diff.hpp`
- Create: `core/capabilities/src/validation.cpp`
- Create: `core/capabilities/src/snapshot_diff.cpp`
- Create: `tests/capabilities/validation_test.cpp`
- Create: `tests/capabilities/snapshot_diff_test.cpp`
- Modify: `core/capabilities/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `CapabilitySnapshot` and typed IDs from Task 1.
- Produces: `ValidationReport validate(const CapabilitySnapshot&)`, `ChangeSet diff_snapshots(const CapabilitySnapshot&, const CapabilitySnapshot&)`, and `SnapshotUpdate { CapabilitySnapshot snapshot; ChangeSet changes; }`.

- [ ] **Step 1: Write failing validation tests for impossible states**

```cpp
TEST_CASE("encoder cannot reference a missing GPU") {
    auto snapshot = valid_snapshot();
    snapshot.devices.encoders.front().gpu = GpuId{"missing", IdentityScope::snapshot};
    REQUIRE(validate(snapshot).contains(ValidationCode::missing_gpu_reference));
}

TEST_CASE("unknown observations cannot carry values") {
    auto snapshot = valid_snapshot();
    snapshot.hardware.installed_memory = invalid_unknown_with_value(Bytes{1});
    REQUIRE(validate(snapshot).contains(ValidationCode::unexpected_value));
}
```

- [ ] **Step 2: Write failing change-classification tests**

Assert that an audio default-role change reports only `ChangeDomain::audio_output`, while a thermal transition reports only `ChangeDomain::thermal`; verify affected IDs and unchanged-domain exclusion.

- [ ] **Step 3: Run tests to verify failure**

Run: `ctest --preset windows-debug -R "catro_(validation|snapshot_diff)" --output-on-failure`

Expected: tests fail because validation and differencing are undefined.

- [ ] **Step 4: Implement all specification invariants**

Return stable validation codes; do not throw for user- or probe-supplied invalid data. Validate evidence/value consistency, rational values, unique IDs, references, ranges, mode/codec agreement, transfer endpoints, schema versions, and bounded identifiers.

- [ ] **Step 5: Implement deterministic snapshot differencing**

Compare sorted typed IDs rather than inventory order. Produce a bitset of domains and stable sorted affected IDs without retaining history. Construct `SnapshotUpdate` only after replacement-snapshot validation succeeds.

- [ ] **Step 6: Run focused tests**

Run: `ctest --preset windows-debug -R "catro_(validation|snapshot_diff)" --output-on-failure`

Expected: all validation and differencing tests pass.

- [ ] **Step 7: Commit**

```bash
git add core/capabilities tests/capabilities tests/CMakeLists.txt
git commit -m "feat: validate and classify capability snapshots"
```

### Task 3: Operating Profile and Local Quality Envelope

**Files:**
- Create: `core/capabilities/include/catro/capabilities/media_plan.hpp`
- Create: `core/capabilities/include/catro/capabilities/policy.hpp`
- Create: `core/capabilities/src/operating_profile.cpp`
- Create: `core/capabilities/src/local_envelope.cpp`
- Create: `tests/fixtures/capability_fixtures.hpp`
- Create: `tests/fixtures/capability_fixtures.cpp`
- Create: `tests/capabilities/operating_profile_test.cpp`
- Create: `tests/capabilities/local_envelope_test.cpp`
- Modify: `core/capabilities/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: validated snapshots, `OperatingPreference`, `RequestedQuality`, and `MediaDecisionRequest`.
- Produces: `OperatingProfile derive_operating_profile(...)` and `LocalQualityEnvelope derive_local_envelope(...)`.

- [ ] **Step 1: Add representative fixture builders**

Create builders for NVIDIA desktop, Intel battery laptop, hybrid-GPU laptop, normal/thermal Apple Silicon MacBook, older Intel Mac, headless session, and partial-probe failure. Builders must assign stable test IDs and explicit evidence.

- [ ] **Step 2: Write failing profile tests**

```cpp
TEST_CASE("battery plus low-power mode derives efficiency") {
    REQUIRE(derive_operating_profile(intel_laptop_on_battery(), auto_preference())
            == OperatingProfile::efficiency);
}

TEST_CASE("serious thermal pressure overrides performance preference") {
    REQUIRE(derive_operating_profile(hot_apple_silicon(), performance_preference())
            == OperatingProfile::thermal_constrained);
}
```

- [ ] **Step 3: Write failing local-envelope tests**

Assert that unknown encoder limits cannot authorize 1440p60, exact 60000/1001 rates survive, HDR requires compatible capture/mode/transfer evidence, and headless machines produce no display envelope.

- [ ] **Step 4: Run tests to verify failure**

Run: `ctest --preset windows-debug -R "catro_(operating_profile|local_envelope)" --output-on-failure`

Expected: tests fail because policy primitives are undefined.

- [ ] **Step 5: Implement pure profile and envelope derivation**

Use explicit facts and lexicographic rules. Keep requested quality distinct from local achievable quality. Do not accept peer or network inputs and do not infer performance from device names.

- [ ] **Step 6: Run focused tests**

Run: `ctest --preset windows-debug -R "catro_(operating_profile|local_envelope)" --output-on-failure`

Expected: all profile and envelope tests pass.

- [ ] **Step 7: Commit**

```bash
git add core/capabilities tests
git commit -m "feat: derive local capability envelopes"
```

### Task 4: Candidate Ranking, Fallbacks, and Explainability

**Files:**
- Create: `core/capabilities/src/candidate_ranking.cpp`
- Create: `core/capabilities/src/policy.cpp`
- Create: `tests/capabilities/policy_test.cpp`
- Create: `tests/capabilities/policy_pathology_test.cpp`
- Modify: `core/capabilities/include/catro/capabilities/media_plan.hpp`
- Modify: `core/capabilities/include/catro/capabilities/policy.hpp`
- Modify: `core/capabilities/CMakeLists.txt`
- Modify: `tests/fixtures/capability_fixtures.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: Task 3 profile/envelope functions and typed capture, transfer, GPU, encoder, codec, and mode candidates.
- Produces: `MediaPlan derive_media_plan(const CapabilitySnapshot&, const MediaDecisionRequest&, PolicyVersion)` with selected candidates, ordered fallbacks, and bounded `DecisionTrace`.

- [ ] **Step 1: Write failing ranking tests**

Assert native/same-resource beats same-adapter copy, same-adapter beats cross-adapter, known low-latency hardware beats advertised unknown modes, and software encoding appears only as an explicit candidate with CPU/power consequences.

- [ ] **Step 2: Write failing rejection and fallback tests**

```cpp
TEST_CASE("hybrid GPU rejection explains cross-adapter cost") {
    const auto plan = derive_media_plan(hybrid_laptop(), display_request(), kPolicyVersion);
    REQUIRE(plan.selected.transfer.kind == TransferKind::same_resource);
    REQUIRE(plan.trace.has_rejection(ReasonCode::cross_adapter_transfer));
    REQUIRE(plan.fallbacks == expected_hybrid_fallback_order());
}
```

Cover missing drivers, software-only encoders, unknown codec limits, no microphone, remote/headless sessions, mixed refresh displays, and partial probe failure. Assert rejection reasons as well as selection.

- [ ] **Step 3: Write failing determinism and trace-bound tests**

Shuffle every unordered input inventory and require identical semantic plans. Generate more than 32 candidates and eight reasons per candidate; require deterministic truncation, omitted counts, retained selection, and no more than 256 records.

- [ ] **Step 4: Run tests to verify failure**

Run: `ctest --preset windows-debug -R "catro_policy" --output-on-failure`

Expected: tests fail because ranking and full planning are undefined.

- [ ] **Step 5: Implement stable typed rule tables and plan assembly**

Use stable rule and reason codes, lexicographic comparison, sorted typed IDs, and the exact rule precedence in the spec. Return an explicit no-viable-path plan rather than inventing software fallback.

- [ ] **Step 6: Run focused tests**

Run: `ctest --preset windows-debug -R "catro_policy" --output-on-failure`

Expected: all policy and pathology tests pass.

- [ ] **Step 7: Commit**

```bash
git add core/capabilities tests
git commit -m "feat: rank capability plans with explainable fallbacks"
```

### Task 5: Canonical JSON, Human Reports, and Golden Fixtures

**Files:**
- Create: `core/reporting/CMakeLists.txt`
- Create: `core/reporting/include/catro/reporting/report.hpp`
- Create: `core/reporting/include/catro/reporting/canonical_json.hpp`
- Create: `core/reporting/include/catro/reporting/human_report.hpp`
- Create: `core/reporting/src/canonical_json.cpp`
- Create: `core/reporting/src/human_report.cpp`
- Create: `tests/capabilities/reporting_test.cpp`
- Create: `tests/capabilities/canonical_determinism_test.cpp`
- Create: `tests/fixtures/*.json`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: validated snapshot, explicit request, and `MediaPlan`.
- Produces: `std::string to_canonical_json(const CapabilityReport&)`, `ReportParseResult parse_report(std::string_view)`, and `std::string to_human_report(const CapabilityReport&, RedactionMode)`.

- [ ] **Step 1: Write failing canonical serialization tests**

Require stable field order, enum strings, explicit unknown/unavailable values, normalized rationals, sorted non-semantic inventories, preserved fallback ranking, UTF-8/LF output, and no serialization-time clock reads.

- [ ] **Step 2: Write failing permutation and locale tests**

Serialize semantically identical reports with shuffled inventories under at least the C and Turkish locales; require byte-identical output. Fix snapshot timestamps in the input and verify the serializer does not replace them.

- [ ] **Step 3: Write failing round-trip and malformed-input tests**

Round-trip all fixture builders. Reject incompatible schema versions, overlong helper output, duplicate IDs, invalid enums, and contradictory observations before returning a domain report.

- [ ] **Step 4: Run tests to verify failure**

Run: `ctest --preset windows-debug -R "catro_(reporting|canonical)" --output-on-failure`

Expected: tests fail because reporting is undefined.

- [ ] **Step 5: Implement reporting as a separate dependency boundary**

Link nlohmann/json privately. Build ordered objects explicitly; never serialize domain containers directly. Apply default privacy redaction in human and bug-report projections.

- [ ] **Step 6: Materialize canonical golden fixtures**

Generate and check in one canonical report for every pathological fixture listed in the spec. Reparse and reserialize each file byte-for-byte in tests.

- [ ] **Step 7: Run focused tests**

Run: `ctest --preset windows-debug -R "catro_(reporting|canonical)" --output-on-failure`

Expected: all reporting, round-trip, malformed-input, and golden tests pass.

- [ ] **Step 8: Commit**

```bash
git add CMakeLists.txt core/reporting tests
git commit -m "feat: add deterministic capability reports"
```

### Task 6: Probe Fragments and Concurrent Snapshot Coordination

**Files:**
- Create: `core/capabilities/include/catro/capabilities/probe.hpp`
- Create: `core/capabilities/include/catro/capabilities/probe_coordinator.hpp`
- Create: `core/capabilities/src/probe_coordinator.cpp`
- Create: `tests/helpers/fake_probe_executor.hpp`
- Create: `tests/capabilities/probe_coordinator_test.cpp`
- Modify: `core/capabilities/CMakeLists.txt`
- Modify: `core/reporting/include/catro/reporting/canonical_json.hpp`
- Modify: `core/reporting/src/canonical_json.cpp`
- Modify: `tests/capabilities/reporting_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `ProbeSpec`, `ProbeExecutor`, validated `ProbeFragment`, per-family hard budgets, and global publication budget.
- Produces: `RefreshReason`, deterministic probe-fragment encoding/decoding, and `SnapshotPublication collect_snapshot(const ProbeSchedule&, ProbeExecutor&)`, including a coherent snapshot, scheduled-probe records, and timeout/partial issues.

- [ ] **Step 1: Write failing schedule tests**

Define the five stable family IDs and budgets from the spec. Require concurrent completion and a `ProbeRecord` for success, partial output, API unavailable, permission unavailable, malformed output, helper termination, and timeout.

- [ ] **Step 2: Write failing partial-publication tests**

Use a fake executor that completes four families and never completes the fifth. Require publication by the global budget, explicit unknown facts for the missing family, and a synthesized timeout record.

- [ ] **Step 3: Write failing merge tests**

Reject duplicate IDs and cross-fragment dangling references. Accept a structurally valid partial snapshot and preserve each fact's probe provenance.

- [ ] **Step 4: Run tests to verify failure**

Run: `ctest --preset windows-debug -R catro_probe_coordinator --output-on-failure`

Expected: tests fail because coordinator contracts are undefined.

- [ ] **Step 5: Implement the minimal shared coordinator**

The coordinator owns scheduling, deadlines, merge order, validation, and immutable generation publication. `ProbeExecutor` owns process creation and hard termination; it has real Windows and macOS implementations plus the fake test implementation. Extend reporting with a size-bounded canonical `ProbeFragment` transport without adding JSON to the capability target's link interface.

- [ ] **Step 6: Run focused tests**

Run: `ctest --preset windows-debug -R catro_probe_coordinator --output-on-failure`

Expected: all schedule, partial-publication, and merge tests pass.

- [ ] **Step 7: Commit**

```bash
git add core/capabilities tests
git commit -m "feat: coordinate bounded capability probes"
```

### Task 7: Windows Process Isolation, System Probe, and Runtime Probe

**Files:**
- Create: `platform/windows/CMakeLists.txt`
- Create: `platform/windows/include/catro/platform/windows/capability_service.hpp`
- Create: `platform/windows/src/capability_service.cpp`
- Create: `platform/windows/src/process_probe_executor.cpp`
- Create: `platform/windows/src/probe_dispatch.cpp`
- Create: `platform/windows/src/probes/system_probe.cpp`
- Create: `platform/windows/src/probes/runtime_probe.cpp`
- Create: `tools/capability-probe/CMakeLists.txt`
- Create: `tools/capability-probe/main.cpp`
- Create: `tests/helpers/hanging_probe_helper.cpp`
- Create: `tests/windows/process_probe_executor_test.cpp`
- Create: `tests/windows/system_runtime_probe_test.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: Task 6 `ProbeExecutor`, probe-family dispatch, and Task 2 `SnapshotUpdate`/`ChangeSet`.
- Produces: Windows `CapabilityService::start(UpdateCallback)`, `refresh(RefreshReason)`, and `stop()`, plus helper families `windows.system.v1` and `windows.runtime.v1`.

- [ ] **Step 1: Write failing hard-termination test**

Launch `hanging_probe_helper`, assign it to a Job Object with kill-on-close semantics, require timeout by the configured budget, and verify the child no longer exists after the executor returns.

- [ ] **Step 2: Write failing Windows system/runtime contract tests**

Require native/process architecture, CPU topology, memory, OS build, power source, battery/platform role, remote-session state, and monotonic probe timing. Unsupported facts must be unknown or unavailable, never fabricated.

- [ ] **Step 3: Run tests to verify failure**

Run: `ctest --preset windows-debug -R "catro_windows_(process|system_runtime)" --output-on-failure`

Expected: tests fail because Windows execution and probes are absent.

- [ ] **Step 4: Implement bounded Windows child execution**

Use `CreateProcessW`, inherited anonymous pipes with a 1 MiB output cap, a Job Object configured for termination on close, `WaitForSingleObject`, explicit handle ownership, and canonical fragment parsing. Never execute through a shell.

- [ ] **Step 5: Implement observational system and runtime probes**

Use documented Windows APIs. Do not use registry marketing-name tables or request permissions. Record native error categories and monotonic duration.

- [ ] **Step 6: Implement debounced runtime refresh notifications**

Translate native power, session, and runtime-state notifications into explicit `RefreshReason` values. Refresh affected families off the UI thread, publish immutable `SnapshotUpdate` values, and make `stop()` unregister every callback safely.

- [ ] **Step 7: Run focused Windows tests**

Run: `ctest --preset windows-debug -R "catro_windows_(process|system_runtime)" --output-on-failure`

Expected: timeout, cleanup, live system, and runtime tests pass.

- [ ] **Step 8: Commit**

```bash
git add CMakeLists.txt platform/windows tools/capability-probe tests
git commit -m "feat: add isolated Windows capability probes"
```

### Task 8: Windows GPU, Display, Capture-API, and Transfer Evidence

**Files:**
- Create: `platform/windows/src/probes/gpu_display_probe.cpp`
- Create: `platform/windows/src/windows_translation.cpp`
- Create: `tests/windows/gpu_display_probe_test.cpp`
- Modify: `platform/windows/src/probe_dispatch.cpp`
- Modify: `platform/windows/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces: helper family `windows.gpu_display.v1` with GPUs, displays, exact active modes, Windows Graphics Capture availability, source-specific capture paths, and only provable GPU/display relationships.
- Consumes: DXCore, DXGI, Display Configuration, and runtime API-availability checks.

- [ ] **Step 1: Write failing translation tests with synthetic native records**

Verify multiple GPUs, software-adapter exclusion from hardware claims, hybrid-GPU preservation, exact 60000/1001 rates, stable process-local IDs, and unknown display affinity when an adapter LUID cannot be matched.

- [ ] **Step 2: Write failing live smoke test**

Require a valid fragment on the current Windows runner. Headless runners may report zero displays, but must record the session state and must not crash or invent a primary display.

- [ ] **Step 3: Run tests to verify failure**

Run: `ctest --preset windows-debug -R catro_windows_gpu_display --output-on-failure`

Expected: tests fail because the probe is absent.

- [ ] **Step 4: Implement passive GPU/display/API discovery**

Use DXCore/DXGI and QueryDisplayConfig-family APIs. Preserve LUID-based relationships only when documented. Check capture API availability without creating a capture item or triggering a picker.

- [ ] **Step 5: Add display-topology refresh subscription**

Register native display-change notification at service start, debounce bursts into `RefreshReason::display`, and unregister it at service stop. Extend tests to require a display-only `ChangeSet` for a synthetic topology event.

- [ ] **Step 6: Run focused tests**

Run: `ctest --preset windows-debug -R catro_windows_gpu_display --output-on-failure`

Expected: synthetic translation and live smoke tests pass.

- [ ] **Step 7: Commit**

```bash
git add platform/windows tests
git commit -m "feat: probe Windows GPU and display capabilities"
```

### Task 9: Windows Encoder and Audio Advertisement

**Files:**
- Create: `platform/windows/src/probes/encoder_probe.cpp`
- Create: `platform/windows/src/probes/audio_probe.cpp`
- Create: `platform/windows/src/transfer_evidence.cpp`
- Create: `tests/windows/encoder_probe_test.cpp`
- Create: `tests/windows/audio_probe_test.cpp`
- Create: `tests/windows/transfer_evidence_test.cpp`
- Modify: `platform/windows/src/probe_dispatch.cpp`
- Modify: `platform/windows/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces: `windows.encoders.v1` and `windows.audio.v1`, plus conservative transfer relationships derived only from provable adapter/resource-format evidence.
- Consumes: Media Foundation enumeration and MMDevice endpoint APIs.

- [ ] **Step 1: Write failing encoder translation tests**

Cover multiple encoders per GPU, hardware versus software flags, codec support separate from concrete modes, unknown limits, bit depth/color/HDR facts, and absence of runtime-validation claims.

- [ ] **Step 2: Write failing audio tests**

Cover no microphone, no output device, disabled/unavailable endpoints, default-role changes, advertised channel/sample formats, and stable failure classification without opening an audio stream.

- [ ] **Step 3: Write failing transfer-evidence tests**

Require unknown transfer cost unless adapter/resource interoperability is proved. Never infer zero-copy solely from matching vendor names or shared codec support.

- [ ] **Step 4: Run tests to verify failure**

Run: `ctest --preset windows-debug -R "catro_windows_(encoder|audio|transfer)" --output-on-failure`

Expected: tests fail because these probes are absent.

- [ ] **Step 5: Implement passive Media Foundation and MMDevice enumeration**

Enumerate advertised MFTs and endpoint properties without activating an encoder, opening a stream, or changing device state. Normalize native failures and preserve partial output.

- [ ] **Step 6: Add audio-endpoint refresh subscription**

Register `IMMNotificationClient` for inventory/default-role changes, debounce to the affected input/output family, and verify callback lifetime and unregistration. Encoder inventory remains manual/startup refresh because passive Windows APIs do not provide a reliable general encoder-change event.

- [ ] **Step 7: Run focused and aggregate Windows tests**

Run: `ctest --preset windows-debug -R "catro_(windows|probe|policy)" --output-on-failure`

Expected: all Windows probes, coordinator, validation, and policy tests pass.

- [ ] **Step 8: Commit**

```bash
git add platform/windows tests
git commit -m "feat: probe Windows encoder and audio capabilities"
```

### Task 10: Capability Report Command-Line Tool

**Files:**
- Create: `tools/capability-report/CMakeLists.txt`
- Create: `tools/capability-report/main.cpp`
- Create: `tests/capabilities/capability_report_cli_test.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: the current platform `CapabilityService`, an explicit representative display-stream request, policy 1.0.0, and reporting projections.
- Produces: `catro-capability-report --format human|json [--output <path>]` with default privacy redaction.

- [ ] **Step 1: Write failing CLI tests**

Require human output on stdout, canonical JSON parseability, deterministic output for a fixed injected report, atomic file output, a nonzero exit for invalid arguments, and successful partial-report output with explicit issues.

- [ ] **Step 2: Run tests to verify failure**

Run: `ctest --preset windows-debug -R catro_capability_report_cli --output-on-failure`

Expected: tests fail because the tool is absent.

- [ ] **Step 3: Implement a minimal standard-library argument parser and output path**

Do not add a CLI dependency. Refuse an existing output target, write a temporary sibling file, then atomically rename it into place; remove only the temporary file on failure.

- [ ] **Step 4: Run the real report tool**

Run: `out/build/windows-msvc/Debug/catro-capability-report.exe --format human`

Expected: a useful report containing probe timing, evidence, local plan, rejections, and fallback order; no prompts or system changes.

- [ ] **Step 5: Run focused tests**

Run: `ctest --preset windows-debug -R catro_capability_report_cli --output-on-failure`

Expected: all CLI tests pass.

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt tools/capability-report tests
git commit -m "feat: add capability report tool"
```

### Task 11: Modern Windows Native Shell and Diagnostics Workspace

**Files:**
- Create: `apps/windows/Catro.sln`
- Create: `apps/windows/Catro/Catro.vcxproj`
- Create: `apps/windows/Catro/Catro.vcxproj.filters`
- Create: `apps/windows/Catro/Package.appxmanifest`
- Create: `apps/windows/Catro/packages.config`
- Create: `apps/windows/Catro/App.xaml`
- Create: `apps/windows/Catro/App.xaml.h`
- Create: `apps/windows/Catro/App.xaml.cpp`
- Create: `apps/windows/Catro/MainWindow.xaml`
- Create: `apps/windows/Catro/MainWindow.xaml.h`
- Create: `apps/windows/Catro/MainWindow.xaml.cpp`
- Create: `apps/windows/Catro/Diagnostics/DiagnosticsView.xaml`
- Create: `apps/windows/Catro/Diagnostics/DiagnosticsView.xaml.h`
- Create: `apps/windows/Catro/Diagnostics/DiagnosticsView.xaml.cpp`
- Create: `apps/windows/Catro/Diagnostics/DiagnosticsViewModel.hpp`
- Create: `apps/windows/Catro/Diagnostics/DiagnosticsViewModel.cpp`
- Create: `tests/windows/diagnostics_view_model_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: immutable snapshots, `SnapshotUpdate`, `CapabilityReport`, and native Windows capability service.
- Produces: a WinUI 3 application showing probe health, system/device evidence, topology, intended plan, rejection reasons, fallbacks, refresh changes, and export actions.

- [ ] **Step 1: Write failing view-model tests**

Assert section ordering, unknown/degraded badges, exact rational rendering, bounded trace presentation, classified refresh changes, and export command output from a pathological fixture.

- [ ] **Step 2: Run tests to verify failure**

Run: `ctest --preset windows-debug -R catro_windows_diagnostics --output-on-failure`

Expected: tests fail because the view model is absent.

- [ ] **Step 3: Add the packaged C++/WinRT project**

Pin Windows App SDK 2.5.1. Link prebuilt CMake static libraries through an explicit `CatroCoreRoot` MSBuild property. Keep COM, WinRT, and XAML types outside core headers.

- [ ] **Step 4: Implement the diagnostics view model and modern native layout**

Use a responsive navigation/sidebar layout, native typography, semantic status colors, keyboard navigation, accessible labels, dark/light resources, and high-DPI behavior. Do not add product navigation or disposable channel UI.

- [ ] **Step 5: Build the Windows shell**

Run: `msbuild apps/windows/Catro.sln /restore /p:Configuration=Debug /p:Platform=x64 /p:CatroCoreRoot=<absolute-cmake-build-dir>`

Expected: the packaged native application compiles and links successfully.

- [ ] **Step 6: Launch smoke-test the Windows shell**

Run the Debug application from its MSIX deployment output.

Expected: the app launches without prompting and displays real local evidence or explicit partial-probe issues.

- [ ] **Step 7: Run view-model tests**

Run: `ctest --preset windows-debug -R catro_windows_diagnostics --output-on-failure`

Expected: all Windows diagnostics tests pass.

- [ ] **Step 8: Commit**

```bash
git add apps/windows tests
git commit -m "feat: add native Windows diagnostics shell"
```

### Task 12: macOS Process Isolation, System Probe, and Runtime Probe

**Files:**
- Create: `platform/macos/CMakeLists.txt`
- Create: `platform/macos/include/catro/platform/macos/capability_service.hpp`
- Create: `platform/macos/src/capability_service.mm`
- Create: `platform/macos/src/process_probe_executor.mm`
- Create: `platform/macos/src/probe_dispatch.mm`
- Create: `platform/macos/src/probes/system_probe.mm`
- Create: `platform/macos/src/probes/runtime_probe.mm`
- Create: `tests/macos/process_probe_executor_test.mm`
- Create: `tests/macos/system_runtime_probe_test.mm`
- Modify: `CMakeLists.txt`
- Modify: `tools/capability-probe/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces: macOS `CapabilityService::start(UpdateCallback)`, `refresh(RefreshReason)`, and `stop()`, plus helper families `macos.system.v1` and `macos.runtime.v1`.
- Consumes: Task 6 coordinator, `posix_spawn`, pipes, process groups, `sysctl`, `ProcessInfo`, and passive power/thermal APIs.

- [ ] **Step 1: Write failing macOS hard-termination test**

Spawn the hanging helper in its own process group, require deadline enforcement with `poll`/`waitpid`, terminate the group, and verify it is reaped.

- [ ] **Step 2: Write failing system/runtime tests**

Require native/process architecture, Rosetta state when discoverable, CPU topology, memory, low-power/thermal/memory-pressure state, power source, stable probe records, and honest unknown values.

- [ ] **Step 3: Run tests to verify failure**

Run: `ctest --preset macos-debug -R "catro_macos_(process|system_runtime)" --output-on-failure`

Expected: tests fail because macOS execution and probes are absent.

- [ ] **Step 4: Implement bounded macOS child execution**

Use `posix_spawn`, nonblocking pipes capped at 1 MiB, process groups, monotonic deadlines, `kill`/`waitpid`, and explicit Objective-C/CoreFoundation ownership within `.mm` files.

- [ ] **Step 5: Implement observational system and runtime probes**

Use `sysctl`, `ProcessInfo`, and passive power/thermal APIs without prompting or changing system state. Preserve Rosetta and architecture uncertainty explicitly.

- [ ] **Step 6: Implement debounced runtime refresh notifications**

Translate power, thermal, memory-pressure, and session notifications into explicit refresh reasons. Run probes off the main thread, publish immutable updates, and remove all observers during `stop()`.

- [ ] **Step 7: Run focused tests on arm64 and Intel runners**

Run: `ctest --preset macos-debug -R "catro_macos_(process|system_runtime)" --output-on-failure`

Expected: timeout, cleanup, live system, Rosetta/architecture, and runtime tests pass on their applicable architectures.

- [ ] **Step 8: Commit**

```bash
git add CMakeLists.txt platform/macos tools/capability-probe tests
git commit -m "feat: add isolated macOS capability probes"
```

### Task 13: macOS GPU, Display, Encoder, Audio, and Transfer Evidence

**Files:**
- Create: `platform/macos/src/probes/gpu_display_probe.mm`
- Create: `platform/macos/src/probes/encoder_probe.mm`
- Create: `platform/macos/src/probes/audio_probe.mm`
- Create: `platform/macos/src/transfer_evidence.mm`
- Create: `platform/macos/src/macos_translation.mm`
- Create: `tests/macos/gpu_display_probe_test.mm`
- Create: `tests/macos/encoder_probe_test.mm`
- Create: `tests/macos/audio_probe_test.mm`
- Create: `tests/macos/transfer_evidence_test.mm`
- Modify: `platform/macos/src/probe_dispatch.mm`
- Modify: `platform/macos/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces: `macos.gpu_display.v1`, `macos.encoders.v1`, and `macos.audio.v1`, including Metal, CoreGraphics, ScreenCaptureKit, VideoToolbox, CoreAudio, and only provable transfer evidence.

- [ ] **Step 1: Write failing synthetic translation tests**

Cover Apple Silicon unified memory, Intel integrated/discrete GPUs, unknown display affinity, exact rational rates, HDR/color facts, multiple encoders, unknown mode limits, and no audio devices.

- [ ] **Step 2: Write failing observational-safety tests**

Require no ScreenCaptureKit picker or privacy prompt, no `SCStream`, no `VTCompressionSession`, and no CoreAudio device opening during helper execution. APIs unavailable on macOS 13 must produce unavailable evidence.

- [ ] **Step 3: Run tests to verify failure**

Run: `ctest --preset macos-debug -R "catro_macos_(gpu|encoder|audio|transfer)" --output-on-failure`

Expected: tests fail because the probe families are absent.

- [ ] **Step 4: Implement passive GPU, display, and capture-API discovery**

Use Metal enumeration, CoreGraphics display queries, and non-prompting ScreenCaptureKit availability/permission checks. Leave relationships unknown when public APIs cannot prove them.

- [ ] **Step 5: Implement passive encoder, audio, and transfer evidence**

Use VideoToolbox advertisement/property queries and CoreAudio property enumeration without creating compression sessions or opening endpoints. Emit transfer relationships only when public evidence proves them.

- [ ] **Step 6: Add display and audio refresh subscriptions**

Use CoreGraphics reconfiguration callbacks and CoreAudio property listeners, debounce events by domain, and remove callbacks during service stop. Extend native tests to prove affected-domain classification without retaining Objective-C/CoreFoundation objects in core types.

- [ ] **Step 7: Run macOS aggregate tests and report tool**

Run: `ctest --preset macos-debug --output-on-failure`

Run: `out/build/macos-debug/catro-capability-report --format human`

Expected: all core/macOS tests pass and the report is useful without prompts or device activation.

- [ ] **Step 8: Commit**

```bash
git add platform/macos tests
git commit -m "feat: probe macOS media hardware capabilities"
```

### Task 14: Modern macOS Native Shell and Narrow Objective-C++ Bridge

**Files:**
- Create: `apps/macos/Catro/Catro.xcodeproj/project.pbxproj`
- Create: `apps/macos/Catro/Catro/Info.plist`
- Create: `apps/macos/Catro/Catro/CatroApp.swift`
- Create: `apps/macos/Catro/Catro/Diagnostics/DiagnosticsView.swift`
- Create: `apps/macos/Catro/Catro/Diagnostics/DiagnosticsModel.swift`
- Create: `apps/macos/Catro/Catro/Diagnostics/DiagnosticsViewModel.swift`
- Create: `apps/macos/Catro/Catro/Bridge/CatroCapabilitiesBridge.h`
- Create: `apps/macos/Catro/Catro/Bridge/CatroCapabilitiesBridge.mm`
- Create: `apps/macos/Catro/CatroTests/DiagnosticsModelTests.swift`

**Interfaces:**
- Consumes: macOS capability service and reporting projections inside Objective-C++.
- Produces: an Objective-C header exposing only lifecycle-safe bridge methods and copied `NSData`/`NSString` report projections; Swift decodes a typed diagnostics model and owns the UI.

- [ ] **Step 1: Write failing Swift model tests**

Decode golden reports and assert section ordering, provenance, unknown/degraded states, exact rational display, policy rejection reasons, fallback order, and classified refresh changes.

- [ ] **Step 2: Run tests to verify failure**

Run: `xcodebuild test -project apps/macos/Catro/Catro.xcodeproj -scheme Catro -destination 'platform=macOS'`

Expected: tests fail because the app and bridge are absent.

- [ ] **Step 3: Implement the narrow Objective-C++ bridge**

The public header contains no C++ type, template, exception, pointer ownership, Metal object, or CoreFoundation reference. The `.mm` implementation owns the C++ service and returns copied canonical report data plus refresh completion/error values. The Xcode target links the CMake-built universal static libraries through one explicit `CatroCoreRoot` build setting populated by the build script.

- [ ] **Step 4: Implement the typed Swift model and modern diagnostics workspace**

Use native SwiftUI/AppKit layout, typography, semantic states, Retina behavior, keyboard navigation, accessibility, dark/light adaptation, refresh, change summaries, and export. Do not add product navigation or capture permissions.

- [ ] **Step 5: Build and test the Apple Silicon shell**

Run arm64: `xcodebuild test -project apps/macos/Catro/Catro.xcodeproj -scheme Catro -destination 'platform=macOS' ARCHS=arm64`

Expected: Swift and bridge tests pass on Apple Silicon.

- [ ] **Step 6: Build the Intel shell**

Run: `xcodebuild build -project apps/macos/Catro/Catro.xcodeproj -scheme Catro -configuration Debug ARCHS=x86_64 ONLY_ACTIVE_ARCH=NO`

Expected: the Intel target compiles and links with no bridge ownership warnings.

- [ ] **Step 7: Commit**

```bash
git add apps/macos
git commit -m "feat: add native macOS diagnostics shell"
```

### Task 15: Developer Workflows, CI, Documentation, and Final Gates

**Files:**
- Create: `scripts/bootstrap.ps1`
- Create: `scripts/build.ps1`
- Create: `scripts/test.ps1`
- Create: `scripts/run.ps1`
- Create: `scripts/bootstrap.sh`
- Create: `scripts/build.sh`
- Create: `scripts/test.sh`
- Create: `scripts/run.sh`
- Create: `.github/workflows/ci.yml`
- Create: `README.md`
- Create: `DEPENDENCIES.md`
- Create: `CONTRIBUTING.md`
- Create: `SECURITY.md`
- Create: `docs/building.md`
- Create: `docs/troubleshooting.md`
- Create: `docs/architecture/capability-system.md`
- Create: `docs/architecture/decisions/0001-split-native-shells.md`
- Create: `docs/architecture/decisions/0002-isolated-passive-probes.md`
- Modify: `.gitignore`

**Interfaces:**
- Consumes: every build, test, tool, and app target from Tasks 1–14.
- Produces: non-mutating developer commands and fresh verification evidence on Windows x64, Apple Silicon macOS, and Intel macOS.

- [ ] **Step 1: Write workflow smoke checks before scripts**

Define expected commands: bootstrap only checks prerequisites; build configures and compiles; test runs focused and aggregate tests; run launches the local native shell. None installs tools or modifies system settings.

- [ ] **Step 2: Implement non-mutating developer scripts**

Implement prerequisite checks and exact configure/build/test/run orchestration for both platforms. Scripts must fail with actionable missing-tool messages and never install dependencies or alter system settings.

- [ ] **Step 3: Write concise project documentation**

Document prerequisites, exact dependency pins, platform support, architecture boundaries, schema/policy versioning, probe budgets, privacy behavior, common failures, and how to collect a redacted report.

- [ ] **Step 4: Add CI matrices**

Use `windows-2025-vs2026`, `macos-15` (arm64), and `macos-15-intel`. Build core, helper, report tool, and native shell; run all applicable tests. Add sanitizer jobs for the shared core where supported. Do not claim runtime GPU/encoder availability on hosted runners.

- [ ] **Step 5: Run the complete local Windows gate**

Run: `./scripts/bootstrap.ps1`

Run: `./scripts/build.ps1 -Configuration Debug`

Run: `./scripts/test.ps1 -Configuration Debug`

Run: `out/build/windows-msvc/Debug/catro-capability-report.exe --format json --output out/capability-report.json`

Expected: all commands succeed, report reparses canonically, and no passive operation prompts or mutates the system.

- [ ] **Step 6: Run complete macOS gates on both GitHub-hosted architectures**

Expected: core, helper, report tool, native shell, Swift tests, platform smoke tests, and applicable sanitizers pass on `macos-15` and `macos-15-intel`.

- [ ] **Step 7: Audit milestone scope and dependencies**

Search the final tree for prohibited UI/runtime dependencies and media/service implementations. Verify the capability core's link interface contains only the standard library. Compare every Definition of Done item in the spec with fresh command or CI evidence and record unavailable gates explicitly.

- [ ] **Step 8: Refresh the project graph**

Run: `graphify update .`

Expected: the knowledge graph reflects all new C++/Objective-C++/Swift/build files without extraction errors.

- [ ] **Step 9: Commit**

```bash
git add scripts .github README.md DEPENDENCIES.md CONTRIBUTING.md SECURITY.md docs .gitignore
git commit -m "build: verify native capability foundation"
```

- [ ] **Step 10: Run final fresh verification**

Re-run the complete local Windows gate and inspect the three CI jobs from clean checkouts. Do not mark the milestone complete if either Mac architecture, Windows shell, timeout behavior, canonical determinism, or scope audit lacks fresh passing evidence.
