# Graph Report - native-capability-foundation  (2026-09-27)

## Corpus Check
- 124 files · ~98,689 words
- Verdict: corpus is large enough that graph structure adds value.
- Unclassified: 11 file(s) not represented in the graph (top: (none) 3, .idl 2, .cmake 2)

## Summary
- 2330 nodes · 4558 edges · 170 communities (141 shown, 22 thin omitted)
- Extraction: 97% EXTRACTED · 3% INFERRED · 0% AMBIGUOUS · INFERRED: 131 edges (avg confidence: 0.84)
- Token cost: 0 input · 0 output

## Graph Freshness
- Built from commit: `427f5909`
- Run `git rev-parse HEAD` and compare to check if the graph is stale.
- Run `graphify update .` after code changes (no API cost).

## Community Hubs (Navigation)
- CapabilitySnapshot
- canonical_determinism_test.cpp
- Validator
- schema.hpp
- human_report.cpp
- Differ
- validation_test.cpp
- Observed
- reporting_test.cpp
- candidate_ranking.cpp
- EncoderModeCapability
- MediaCandidate
- array
- local_envelope_test.cpp
- policy_test.cpp
- RuntimeState
- TransferPathCapability
- TraceRecord
- Ranking
- LocalQualityEnvelope
- Dimensions
- policy_pathology_test.cpp
- snapshot_diff_test.cpp
- MediaDecisionRequest
- MediaPlan
- DiagnosticsView
- GpuCapability
- CapturePathCapability
- Rational
- DisplayCapability
- ProbeRecord
- CpuCapability
- EncoderCapability
- Enumeration
- Downgrade
- model.hpp
- operating_profile_test.cpp
- AudioEndpointCapability
- optional
- NativeAudioEndpoint
- derive_media_plan
- OsVersion
- catro_capabilities
- HardwareCapabilities
- FakeProbeExecutor
- DiagnosticsModel
- SnapshotHeader
- ProbeFragment
- ProcessProbe
- ProbeIssue
- EnumNames<caps::AudioDirection>
- CapabilityService
- EnumNames<caps::CaptureApi>
- EnumNames<caps::CapturePermission>
- EnumNames<caps::ChromaSubsampling>
- gpu_display_probe.cpp
- EnumNames<caps::CodecProfile>
- EnumNames<caps::ColorGamut>
- EnumNames<caps::ColorRange>
- probe_coordinator.cpp
- EnumNames<caps::Consequence>
- audio_probe.cpp
- EnumNames<caps::DecisionCategory>
- EnumNames<caps::DowngradeTrigger>
- EnumNames<caps::EncoderBackend>
- EnumNames<caps::EvidenceMethod>
- ProbeSchedule
- EnumNames<caps::IdentityScope>
- EnumNames<caps::ImplementationClass>
- EnumNames<caps::IssueCode>
- EnumNames<caps::Knowledge>
- EnumNames<caps::LatencyClass>
- EnumNames<caps::MemoryPressure>
- EnumNames<caps::OperatingPreference>
- EnumNames<caps::OperatingProfile>
- EnumNames<caps::OperatingSystem>
- EnumNames<caps::PixelFormat>
- NativeDisplay
- EnumNames<caps::PolicyRule>
- EnumNames<caps::PowerSource>
- ChangeSet
- string_view
- CatroCapabilitiesBridge
- EnumNames<caps::SimdFeature>
- ReportParseResult
- EnumNames<caps::Support>
- EnumNames<caps::ThermalPressure>
- AudioNotifications
- EnumNames<caps::TranslationState>
- Translator
- write_new_file
- ProbeSpec
- DiagnosticsViewModel.cpp
- ProfileDecision
- .capability
- Run
- RuntimeProbeFacts
- SnapshotUpdateResult
- enum_name
- CapabilityService::Impl
- windows_translation.cpp
- README.md
- ValidationReport
- CapabilityReport
- probe_coordinator_test.cpp
- NativeGpu
- GpuDisplayProbeFacts
- DiagnosticsView.swift
- NativeLuid
- DiagnosticsViewModel
- UserControl
- NativeAdapter
- EnumNames<caps::GpuKind>
- macos_translation.cpp
- EnumNames<caps::PlatformRole>
- EnumNames<caps::ProbeOutcome>
- reported
- EnumNames<caps::TransferKind>
- canonical_json.cpp
- Schema<caps::CandidateRef>
- Schema<caps::CapabilitySnapshot>
- Schema<caps::CapturePermissionState>
- encode
- App.xaml.cpp
- .window_procedure
- NativeAudioDevice
- CatroApp
- App
- Schema<caps::SupportFact>
- Schema<caps::TraceRecord>
- Schema<caps::TransferPathCapability>
- ParsedDocument
- NoticeBanner
- process_probe_executor_test.cpp
- string_view
- 0001: Split native shells over a shared C++ core
- 0002: Isolated, passive, budgeted probes
- CapabilityService::CapabilityService
- Kind
- Field
- .start
- Application
- EnumNames<caps::AudioRole>
- EnumNames<caps::Confidence>
- EnumNames<caps::CpuArchitecture>
- EnumNames<caps::HdrMode>
- EnumNames<caps::PlanStatus>
- EnumNames<caps::SourceKind>
- Catro.sln
- Schema<caps::AudioEndpointCapability>
- Schema<caps::AudioEndpointState>
- Schema<caps::CpuCapability>
- Schema<caps::EncoderCapability>
- Schema<caps::MediaPlan>
- Schema<caps::ProbeFragment>
- Schema<caps::ProbeIssue>
- Schema<caps::ProbeRecord>
- Schema<caps::Provenance>
- Schema<caps::SystemProbeFacts>
- bootstrap.sh script
- build.sh script
- run.sh script
- test.sh script

## God Nodes (most connected - your core abstractions)
1. `CapabilitySnapshot` - 76 edges
2. `ProbeFragment` - 58 edges
3. `Observed` - 55 edges
4. `Provenance` - 46 edges
5. `Validator` - 40 edges
6. `valid_snapshot()` - 38 edges
7. `EncoderModeCapability` - 36 edges
8. `CapabilityService::Impl` - 33 edges
9. `MediaPlan` - 32 edges
10. `ProbeSpec` - 32 edges

## Surprising Connections (you probably didn't know these)
- `audio_endpoints()` --calls--> `endpoint`  [INFERRED]
  tests/fixtures/capability_fixtures.cpp → core/capabilities/include/catro/capabilities/model.hpp
- `hdr_request()` --references--> `MediaDecisionRequest`  [EXTRACTED]
  tests/capabilities/policy_test.cpp → core/capabilities/include/catro/capabilities/media_plan.hpp
- `enable_hdr_output()` --references--> `CapabilitySnapshot`  [EXTRACTED]
  tests/capabilities/local_envelope_test.cpp → core/capabilities/include/catro/capabilities/model.hpp
- `next_generation()` --references--> `CapabilitySnapshot`  [EXTRACTED]
  tests/capabilities/snapshot_diff_test.cpp → core/capabilities/include/catro/capabilities/model.hpp
- `collect_report()` --references--> `CapabilitySnapshot`  [INFERRED]
  tools/capability-report/main.cpp → core/capabilities/include/catro/capabilities/model.hpp

## Import Cycles
- None detected.

## Hyperedges (group relationships)
- **Platform Abstraction Layer** — platform_windows_capability_service, platform_macos_capability_service, tools_capability_probe [EXTRACTED 0.90]
- **Catro Core Architecture** — core_capabilities_catro_capabilities, core_reporting_catro_reporting, core_capabilities_model, core_capabilities_policy_engine [EXTRACTED 1.00]

## Communities (170 total, 22 thin omitted)

### Community 0 - "CapabilitySnapshot"
Cohesion: 0.05
Nodes (125): CapabilitySnapshot, devices, hardware, header, issues, platform, probes, runtime (+117 more)

### Community 1 - "canonical_determinism_test.cpp"
Cohesion: 0.11
Nodes (24): locale, Baseline, human, json, snapshot, baselines(), optional, path (+16 more)

### Community 2 - "Validator"
Cohesion: 0.08
Nodes (45): string, ScopedId, scope, value, at(), codec_of(), collect(), color_consistent() (+37 more)

### Community 3 - "schema.hpp"
Cohesion: 0.03
Nodes (66): EnumNames, Schema, Schema<caps::AudioProbeFacts>, fields, Schema<caps::CapturePathCapability>, fields, Schema<caps::CategoryTruncation>, fields (+58 more)

### Community 4 - "human_report.cpp"
Cohesion: 0.06
Nodes (71): bool_constant<!Record<T>>, FactState, optional, string, uint32_t, vector, PresentedRow, depth (+63 more)

### Community 5 - "Differ"
Cohesion: 0.20
Nodes (13): affected(), AudioEndpointId, ChangeDomain, Item, Tag, vector, diff_by_key(), Differ (+5 more)

### Community 6 - "validation_test.cpp"
Cohesion: 0.06
Nodes (33): "a reference with the right value but wrong scope is dangling", "a timed-out family yields a valid partial snapshot", "absent or degraded evidence must state why", "an unset observation has no provenance and is rejected", "backends and capture APIs must belong to the snapshot platform", "battery power without a battery is contradictory", string, T (+25 more)

### Community 7 - "Observed"
Cohesion: 0.06
Nodes (58): Confidence, EvidenceMethod, IssueCode, optional, string, T, Observed, provenance_ (+50 more)

### Community 8 - "reporting_test.cpp"
Cohesion: 0.13
Nodes (18): "canonical JSON has a fixed shape and declaration field order", size_t, string, string_view, edited(), "every fixture report round-trips byte for byte", "facts carry explicit knowledge, provenance, and stable enum names", has() (+10 more)

### Community 9 - "candidate_ranking.cpp"
Cohesion: 0.10
Nodes (38): mode_key(), affinity_penalty(), codec_order(), consequences(), array, Codec, Consequence, GpuId (+30 more)

### Community 10 - "EncoderModeCapability"
Cohesion: 0.07
Nodes (30): ChromaSubsampling, ColorRange, DimensionRange, maximum, minimum, EncoderModeCapability, bit_depth, chroma (+22 more)

### Community 11 - "MediaCandidate"
Cohesion: 0.09
Nodes (25): CandidateRef, capture, encoder, mode, CapturePathId, Codec, Consequence, EncoderId (+17 more)

### Community 12 - "array"
Cohesion: 0.07
Nodes (28): EnumNames<caps::CandidateOutcome>, last, names, EnumNames<caps::Codec>, last, names, EnumNames<caps::Conversion>, last (+20 more)

### Community 13 - "local_envelope_test.cpp"
Cohesion: 0.09
Nodes (21): "a known hardware path is bounded by the source display", "a missing requested display produces no envelope", "a timed-out encoder family leaves no encoder and degraded confidence", "capture limits bound the frame rate when known", EncoderId, ReasonCode, "denied capture permission leaves no capture path", enable_hdr_output() (+13 more)

### Community 14 - "policy_test.cpp"
Cohesion: 0.11
Nodes (19): "a coherent desktop selects its same-resource hardware path", "a same-adapter copy beats a cross-adapter copy", "a same-resource path beats a same-adapter copy", "Apple Silicon keeps unprovable encoder affinity explicit", contains(), EncoderId, ReasonCode, TransferKind (+11 more)

### Community 15 - "RuntimeState"
Cohesion: 0.14
Nodes (14): MemoryPressure, PowerSource, ThermalPressure, RuntimeState, audio_endpoints, battery_present, capture_permissions, displays (+6 more)

### Community 16 - "TransferPathCapability"
Cohesion: 0.12
Nodes (16): CapturePermission, Conversion, CapturePermissionState, path, permission, CapturePathId, EncoderId, TransferKind (+8 more)

### Community 17 - "TraceRecord"
Cohesion: 0.12
Nodes (19): CandidateOutcome, CategoryTruncation, category, omitted, DecisionTrace, records, truncated, PolicyRule (+11 more)

### Community 18 - "Ranking"
Cohesion: 0.33
Nodes (6): ReasonCode, vector, Ranking, failure, ranked, records

### Community 19 - "LocalQualityEnvelope"
Cohesion: 0.10
Nodes (20): Confidence, uint8_t, LocalQualityEnvelope, bit_depth, confidence, frame_rate, hdr, reasons (+12 more)

### Community 20 - "Dimensions"
Cohesion: 0.11
Nodes (28): Dimensions, height, width, better(), CapturePathId, optional, ReasonCode, uint64_t (+20 more)

### Community 21 - "policy_pathology_test.cpp"
Cohesion: 0.13
Nodes (14): "a missing GPU driver rejects the hardware encoder and falls back to software explicitly", "a missing microphone does not change the video plan", "a partial probe failure reports no viable path with degraded confidence", DisplayId, "headless sessions report no viable local path", "invalid input is rejected before any planning", "large inventories truncate deterministically within the trace bounds", "mixed refresh displays keep exact rational rates" (+6 more)

### Community 22 - "snapshot_diff_test.cpp"
Cohesion: 0.12
Nodes (15): "a display mode change reports the affected display", "a thermal transition reports only thermal", "added and removed devices are affected", "an audio default-role change reports only audio output", ChangeDomain, vector, domains_of(), "enumeration order alone is not a change" (+7 more)

### Community 23 - "MediaDecisionRequest"
Cohesion: 0.17
Nodes (12): DisplayId, LatencyClass, OperatingPreference, SourceKind, MediaDecisionRequest, display, latency, preference (+4 more)

### Community 24 - "MediaPlan"
Cohesion: 0.14
Nodes (14): MediaPlan, downgrades, envelope, fallbacks, policy_version, profile, reasons, request (+6 more)

### Community 25 - "DiagnosticsView"
Cohesion: 0.08
Nodes (55): application_directory(), badge(), badge_style(), ExportFormat, FactState, optional, path, string (+47 more)

### Community 26 - "GpuCapability"
Cohesion: 0.14
Nodes (14): GpuCapability, dedicated_memory, device_id, graphics_apis, id, kind, name, preferred_for_high_performance (+6 more)

### Community 27 - "CapturePathCapability"
Cohesion: 0.12
Nodes (22): SupportFact, provenance, status, CapturePathCapability, api, frame_rates, gpu, hdr_output (+14 more)

### Community 28 - "Rational"
Cohesion: 0.17
Nodes (11): RequestedQuality, frame_rate, hdr, resolution, RationalRange, maximum, minimum, uint32_t (+3 more)

### Community 29 - "DisplayCapability"
Cohesion: 0.10
Nodes (22): DisplayCapability, bits_per_channel, gamut, gpu, hdr, id, modes, DisplayMode (+14 more)

### Community 30 - "ProbeRecord"
Cohesion: 0.15
Nodes (13): int64_t, microseconds, ProbeFamily, ProbeOutcome, ProbeRecord, duration, fact_count, family (+5 more)

### Community 31 - "CpuCapability"
Cohesion: 0.17
Nodes (12): CpuCapability, efficiency_cores, logical_cores, native_architecture, performance_cores, physical_cores, process_architecture, simd (+4 more)

### Community 32 - "EncoderCapability"
Cohesion: 0.11
Nodes (19): DeviceInventory, audio_endpoints, capture_paths, displays, encoders, gpus, transfer_paths, EncoderCapability (+11 more)

### Community 33 - "Enumeration"
Cohesion: 0.08
Nodes (36): GUID, IDXGIAdapter1, IMFActivate, Adapters, hardware, with_outputs, add_issue(), Codec (+28 more)

### Community 34 - "Downgrade"
Cohesion: 0.33
Nodes (6): Downgrade, profile, quality, trigger, OperatingProfile, DowngradeTrigger

### Community 35 - "model.hpp"
Cohesion: 0.11
Nodes (15): AudioEndpointIdTag, CapturePathIdTag, DisplayIdTag, EncoderIdTag, GpuIdTag, "a snapshot models multiple GPUs without a primary-GPU assumption", "default observations have no provenance so validation can reject them", "device identifiers are distinct types with explicit scope" (+7 more)

### Community 36 - "operating_profile_test.cpp"
Cohesion: 0.18
Nodes (10): "an unknown power source yields the safe local envelope", "battery alone derives efficiency under automatic preference", "battery plus low-power mode derives efficiency", "critical thermal pressure constrains every preference", "explicit preference outranks power source", "fair thermal pressure tempers performance to balanced", "headless, remote, and critical-memory sessions use the safe local envelope", "mains power derives performance only for a known desktop role" (+2 more)

### Community 37 - "AudioEndpointCapability"
Cohesion: 0.13
Nodes (16): AudioEndpointCapability, channels, direction, id, name, sample_formats, sample_rate_hz, AudioEndpointState (+8 more)

### Community 38 - "optional"
Cohesion: 0.18
Nodes (5): string, vector, optional, atomic, map

### Community 39 - "NativeAudioEndpoint"
Cohesion: 0.07
Nodes (33): NativeEndpointState, AudioDirection, AudioRole, Codec, optional, PixelFormat, SampleFormat, string (+25 more)

### Community 40 - "derive_media_plan"
Cohesion: 0.15
Nodes (17): is_supported(), PolicyVersion, major, minor, patch, SchemaVersion, major, minor (+9 more)

### Community 41 - "OsVersion"
Cohesion: 0.22
Nodes (9): OperatingSystem, uint32_t, OsVersion, build, major, minor, PlatformIdentity, os (+1 more)

### Community 42 - "catro_capabilities"
Cohesion: 0.25
Nodes (8): catro_capabilities, Capability Domain Model, Policy Engine, catro_reporting, macOS Capability Service, Windows Capability Service, catro-capability-probe, catro-capability-report

### Community 43 - "HardwareCapabilities"
Cohesion: 0.18
Nodes (11): Bytes, value, HardwareCapabilities, cpu, installed_memory, platform_role, uint64_t, SystemProbeFacts (+3 more)

### Community 44 - "FakeProbeExecutor"
Cohesion: 0.13
Nodes (18): Behavior, fragment, never_completes, ready_after, FakeProbeExecutor, behaviors_, launched_, terminated_ (+10 more)

### Community 45 - "DiagnosticsModel"
Cohesion: 0.08
Nodes (30): DiagnosticsModel, changes, detail, generation, headline, probes, sections, tone (+22 more)

### Community 46 - "SnapshotHeader"
Cohesion: 0.25
Nodes (8): string, UtcTimestamp, SnapshotHeader, captured_at, generation, probe_revision, schema_id, schema_version

### Community 47 - "ProbeFragment"
Cohesion: 0.08
Nodes (27): int64_t, microseconds, optional, ProbeOutcome, ProbeFragment, audio, duration, encoders (+19 more)

### Community 48 - "ProcessProbe"
Cohesion: 0.10
Nodes (22): atomic_bool, HANDLE, microseconds, ProbeOutcome, string, thread, time_point, unique_ptr (+14 more)

### Community 49 - "ProbeIssue"
Cohesion: 0.17
Nodes (16): IssueCode, ProbeIssue, code, probe_id, AudioProbeFacts, endpoints, states, EncoderProbeFacts (+8 more)

### Community 51 - "EnumNames<caps::AudioDirection>"
Cohesion: 0.67
Nodes (3): EnumNames<caps::AudioDirection>, last, names

### Community 52 - "CapabilityService"
Cohesion: 0.07
Nodes (24): ProbeExecutor, start, CapabilityService, impl_, refresh, start, stop, Impl (+16 more)

### Community 53 - "EnumNames<caps::CaptureApi>"
Cohesion: 0.67
Nodes (3): EnumNames<caps::CaptureApi>, last, names

### Community 54 - "EnumNames<caps::CapturePermission>"
Cohesion: 0.67
Nodes (3): EnumNames<caps::CapturePermission>, last, names

### Community 55 - "EnumNames<caps::ChromaSubsampling>"
Cohesion: 0.67
Nodes (3): EnumNames<caps::ChromaSubsampling>, last, names

### Community 56 - "gpu_display_probe.cpp"
Cohesion: 0.15
Nodes (26): DISPLAYCONFIG_PATH_TARGET_INFO, DXGI_GPU_PREFERENCE, IDXGIFactory6, NativeCaptureApis, add_dxcore_properties(), add_issue(), int64_t, IssueCode (+18 more)

### Community 57 - "EnumNames<caps::CodecProfile>"
Cohesion: 0.67
Nodes (3): EnumNames<caps::CodecProfile>, last, names

### Community 58 - "EnumNames<caps::ColorGamut>"
Cohesion: 0.67
Nodes (3): EnumNames<caps::ColorGamut>, last, names

### Community 59 - "EnumNames<caps::ColorRange>"
Cohesion: 0.67
Nodes (3): EnumNames<caps::ColorRange>, last, names

### Community 60 - "probe_coordinator.cpp"
Cohesion: 0.12
Nodes (32): add_issue(), budget_for(), build_snapshot(), IssueCode, milliseconds, OperatingSystem, optional, ProbeDomain (+24 more)

### Community 61 - "EnumNames<caps::Consequence>"
Cohesion: 0.67
Nodes (3): EnumNames<caps::Consequence>, last, names

### Community 62 - "audio_probe.cpp"
Cohesion: 0.16
Nodes (26): IMMDevice, IMMDeviceEnumerator, add_issue(), AudioRole, int64_t, IssueCode, map, optional (+18 more)

### Community 63 - "EnumNames<caps::DecisionCategory>"
Cohesion: 0.67
Nodes (3): EnumNames<caps::DecisionCategory>, last, names

### Community 64 - "EnumNames<caps::DowngradeTrigger>"
Cohesion: 0.67
Nodes (3): EnumNames<caps::DowngradeTrigger>, last, names

### Community 65 - "EnumNames<caps::EncoderBackend>"
Cohesion: 0.67
Nodes (3): EnumNames<caps::EncoderBackend>, last, names

### Community 66 - "EnumNames<caps::EvidenceMethod>"
Cohesion: 0.67
Nodes (3): EnumNames<caps::EvidenceMethod>, last, names

### Community 67 - "ProbeSchedule"
Cohesion: 0.11
Nodes (19): milliseconds, OperatingSystem, ProbeFamily, RefreshReason, uint32_t, uint64_t, UtcTimestamp, vector (+11 more)

### Community 68 - "EnumNames<caps::IdentityScope>"
Cohesion: 0.67
Nodes (3): EnumNames<caps::IdentityScope>, last, names

### Community 69 - "EnumNames<caps::ImplementationClass>"
Cohesion: 0.67
Nodes (3): EnumNames<caps::ImplementationClass>, last, names

### Community 70 - "EnumNames<caps::IssueCode>"
Cohesion: 0.67
Nodes (3): EnumNames<caps::IssueCode>, last, names

### Community 71 - "EnumNames<caps::Knowledge>"
Cohesion: 0.67
Nodes (3): EnumNames<caps::Knowledge>, last, names

### Community 72 - "EnumNames<caps::LatencyClass>"
Cohesion: 0.67
Nodes (3): EnumNames<caps::LatencyClass>, last, names

### Community 73 - "EnumNames<caps::MemoryPressure>"
Cohesion: 0.67
Nodes (3): EnumNames<caps::MemoryPressure>, last, names

### Community 74 - "EnumNames<caps::OperatingPreference>"
Cohesion: 0.67
Nodes (3): EnumNames<caps::OperatingPreference>, last, names

### Community 75 - "EnumNames<caps::OperatingProfile>"
Cohesion: 0.67
Nodes (3): EnumNames<caps::OperatingProfile>, last, names

### Community 76 - "EnumNames<caps::OperatingSystem>"
Cohesion: 0.67
Nodes (3): EnumNames<caps::OperatingSystem>, last, names

### Community 77 - "EnumNames<caps::PixelFormat>"
Cohesion: 0.67
Nodes (3): EnumNames<caps::PixelFormat>, last, names

### Community 78 - "NativeDisplay"
Cohesion: 0.08
Nodes (27): ColorGamut, optional, uint32_t, uint8_t, vector, NativeCaptureApis, access_granted, screen_capture_kit (+19 more)

### Community 79 - "EnumNames<caps::PolicyRule>"
Cohesion: 0.67
Nodes (3): EnumNames<caps::PolicyRule>, last, names

### Community 80 - "EnumNames<caps::PowerSource>"
Cohesion: 0.67
Nodes (3): EnumNames<caps::PowerSource>, last, names

### Community 81 - "ChangeSet"
Cohesion: 0.11
Nodes (16): bitset, ChangeSet, audio_endpoints, capture_paths, displays, domains, encoders, gpus (+8 more)

### Community 82 - "string_view"
Cohesion: 0.10
Nodes (19): condition_variable, string_view, mutex, runtime_spec(), system_spec(), "the Windows capability service publishes generations and stops cleanly", "the Windows runtime probe reports power and session facts without inventing state", "the Windows system probe reports measured topology and explicit uncertainty" (+11 more)

### Community 83 - "CatroCapabilitiesBridge"
Cohesion: 0.15
Nodes (20): CatroCapabilitiesBridge, -exportReportWithFormat, -init, -initWithProbeURLNS_DESIGNATED_INITIALIZER, -refresh, -startWithHandler, -stop, CatroDiagnostics (+12 more)

### Community 84 - "EnumNames<caps::SimdFeature>"
Cohesion: 0.67
Nodes (3): EnumNames<caps::SimdFeature>, last, names

### Community 85 - "ReportParseResult"
Cohesion: 0.14
Nodes (13): FragmentParseResult, error, fragment, optional, ReportErrorCode, string, ReportParseError, code (+5 more)

### Community 86 - "EnumNames<caps::Support>"
Cohesion: 0.67
Nodes (3): EnumNames<caps::Support>, last, names

### Community 87 - "EnumNames<caps::ThermalPressure>"
Cohesion: 0.67
Nodes (3): EnumNames<caps::ThermalPressure>, last, names

### Community 88 - "AudioNotifications"
Cohesion: 0.13
Nodes (14): EDataFlow, ERole, IMMNotificationClient, LPCWSTR, AudioNotifications, references_, STDMETHODCALLTYPE, window_ (+6 more)

### Community 89 - "EnumNames<caps::TranslationState>"
Cohesion: 0.67
Nodes (3): EnumNames<caps::TranslationState>, last, names

### Community 90 - "Translator"
Cohesion: 0.20
Nodes (11): CaptureApi, IssueCode, NativeDisplay, optional, PixelFormat, SourceKind, T, reported() (+3 more)

### Community 91 - "write_new_file"
Cohesion: 0.16
Nodes (21): ostream, ReportCollector, optional, path, ReportFormat, span, string, string_view (+13 more)

### Community 92 - "ProbeSpec"
Cohesion: 0.11
Nodes (18): milliseconds, ProbeDomain, ProbeFamily, string, uint32_t, ProbeSpec, access, family (+10 more)

### Community 93 - "DiagnosticsViewModel.cpp"
Cohesion: 0.17
Nodes (20): build_diagnostics(), change_lines(), array, ExportFormat, microseconds, ProbeOutcome, string, string_view (+12 more)

### Community 94 - "ProfileDecision"
Cohesion: 0.23
Nodes (11): ProfileDecision, profile, rule, OperatingPreference, optional, T, derive_operating_profile(), known_true() (+3 more)

### Community 95 - ".capability"
Cohesion: 0.27
Nodes (10): bounded_text(), GpuId, NativeEncoder, SourceKind, string, uint64_t, gpu_id(), hex16() (+2 more)

### Community 96 - "Run"
Cohesion: 0.15
Nodes (11): RunningProbe, terminate, wait_until, time_point, unique_ptr, Run, fragment, process (+3 more)

### Community 97 - "RuntimeProbeFacts"
Cohesion: 0.18
Nodes (11): MemoryPressure, PowerSource, ThermalPressure, RuntimeProbeFacts, battery_present, headless, low_power_mode, memory_pressure (+3 more)

### Community 98 - "SnapshotUpdateResult"
Cohesion: 0.20
Nodes (9): optional, SnapshotUpdate, changes, snapshot, SnapshotUpdateResult, update, validation, diff_snapshots() (+1 more)

### Community 99 - "enum_name"
Cohesion: 0.50
Nodes (4): enum_from(), enum_name(), E, optional

### Community 100 - "CapabilityService::Impl"
Cohesion: 0.13
Nodes (18): ComPtr, CapabilityService::Impl, audio_notifications, callback, current, executor, lifecycle_mutex, pending (+10 more)

### Community 101 - "windows_translation.cpp"
Cohesion: 0.20
Nodes (14): codec_name(), Codec, DisplayId, EncoderId, GpuId, NativeEncoder, set, string (+6 more)

### Community 102 - "README.md"
Cohesion: 0.05
Nodes (36): Before a change, Checks, Commits, Contributing, Rules the codebase depends on, Dependencies, Platform frameworks, Source dependencies (+28 more)

### Community 103 - "ValidationReport"
Cohesion: 0.11
Nodes (18): optional, SnapshotPublication, snapshot, validation, string, ValidationCode, vector, ValidationError (+10 more)

### Community 104 - "CapabilityReport"
Cohesion: 0.24
Nodes (11): CapabilityReport, plan, snapshot, make_report(), canonical_order(), to_canonical_json(), require_identical_output(), vector (+3 more)

### Community 105 - "probe_coordinator_test.cpp"
Cohesion: 0.20
Nodes (10): "a cross-fragment dangling reference rejects the referencing fragment", "a fragment with duplicate IDs is rejected without poisoning publication", "all probe helpers start before the coordinator waits", complete_all(), ProbeFamily, "every terminal probe outcome produces one record", fragment_for(), "the global deadline publishes explicit unknowns for a blocked family" (+2 more)

### Community 106 - "NativeGpu"
Cohesion: 0.12
Nodes (18): Codec, string, uint64_t, NativeEncoder, codec, encoder_id, gpu, hardware (+10 more)

### Community 107 - "GpuDisplayProbeFacts"
Cohesion: 0.28
Nodes (8): GpuDisplayProbeFacts, capture_paths, capture_permissions, display_states, displays, gpus, NativeGpuDisplay, translate_gpu_display()

### Community 108 - "DiagnosticsView.swift"
Cohesion: 0.21
Nodes (16): Badge, .body, CatroFactState, .marker, CatroTone, .color, .name, .detail (+8 more)

### Community 109 - "NativeLuid"
Cohesion: 0.12
Nodes (17): uint32_t, uint8_t, NativeDisplay, adapter, bits_per_channel, dpi, hdr_enabled, hdr_supported (+9 more)

### Community 110 - "DiagnosticsViewModel"
Cohesion: 0.25
Nodes (9): .body, DiagnosticsView, .body, DiagnosticsViewModel, Notice, String, CatroExportFormat, Identifiable (+1 more)

### Community 111 - "UserControl"
Cohesion: 0.15
Nodes (14): DiagnosticsView, ExportButton, Navigation, Notice, RefreshButton, Rows, SectionTitle, StatusLine (+6 more)

### Community 112 - "NativeAdapter"
Cohesion: 0.13
Nodes (15): uint64_t, NativeAdapter, dedicated_memory, description, detachable, device_id, direct3d11, direct3d12 (+7 more)

### Community 113 - "EnumNames<caps::GpuKind>"
Cohesion: 0.67
Nodes (3): EnumNames<caps::GpuKind>, last, names

### Community 114 - "macos_translation.cpp"
Cohesion: 0.29
Nodes (12): codec_name(), Codec, EncoderId, set, string_view, vector, encoder_id(), identifier() (+4 more)

### Community 115 - "EnumNames<caps::PlatformRole>"
Cohesion: 0.67
Nodes (3): EnumNames<caps::PlatformRole>, last, names

### Community 116 - "EnumNames<caps::ProbeOutcome>"
Cohesion: 0.67
Nodes (3): EnumNames<caps::ProbeOutcome>, last, names

### Community 117 - "reported"
Cohesion: 0.18
Nodes (11): DisplayId, IssueCode, NativeDisplay, NativeGpuDisplay, optional, T, uint32_t, display_id() (+3 more)

### Community 118 - "EnumNames<caps::TransferKind>"
Cohesion: 0.67
Nodes (3): EnumNames<caps::TransferKind>, last, names

### Community 119 - "canonical_json.cpp"
Cohesion: 0.51
Nodes (11): check_plan_bounds(), span, string, decode(), decode_quantity(), decode_report(), expect_object(), fail() (+3 more)

### Community 123 - "encode"
Cohesion: 0.19
Nodes (13): E, I, microseconds, T, UtcTimestamp, vector, encode(), quantity() (+5 more)

### Community 124 - "App.xaml.cpp"
Cohesion: 0.20
Nodes (5): OnLaunched, MainWindow, InitializeComponent, LaunchActivatedEventArgs, MainWindowT

### Community 125 - ".window_procedure"
Cohesion: 0.20
Nodes (5): HWND, LPARAM, LRESULT, UINT, WPARAM

### Community 126 - "NativeAudioDevice"
Cohesion: 0.18
Nodes (11): AudioDirection, SampleFormat, NativeAudioDevice, alive, channels, direction, is_default, name (+3 more)

### Community 127 - "CatroApp"
Cohesion: 0.22
Nodes (6): App, AppKit, CatroApp, Scene, SwiftUI, UniformTypeIdentifiers

### Community 128 - "App"
Cohesion: 0.25
Nodes (7): App, window_, AppTitleBar, MainWindow, Window, AppT, Grid

### Community 132 - "ParsedDocument"
Cohesion: 0.25
Nodes (8): optional, ReportErrorCode, Failure, code, path, ParsedDocument, document, error

### Community 133 - "NoticeBanner"
Cohesion: 0.33
Nodes (6): NoticeBanner, .body, .color, .symbol, Color, Void

### Community 134 - "process_probe_executor_test.cpp"
Cohesion: 0.33
Nodes (5): "a timed-out helper and its descendants are terminated by the job", DWORD, optional, path, read_pid()

### Community 135 - "string_view"
Cohesion: 0.60
Nodes (5): size_t, string_view, parse_document(), parse_probe_fragment(), parse_report()

### Community 136 - "0001: Split native shells over a shared C++ core"
Cohesion: 0.40
Nodes (4): 0001: Split native shells over a shared C++ core, Consequences, Context, Decision

### Community 137 - "0002: Isolated, passive, budgeted probes"
Cohesion: 0.40
Nodes (4): 0002: Isolated, passive, budgeted probes, Consequences, Context, Decision

### Community 138 - "CapabilityService::CapabilityService"
Cohesion: 0.40
Nodes (3): CapabilityService::CapabilityService(), CapabilityService::stop(), path

### Community 139 - "Kind"
Cohesion: 0.50
Nodes (4): Kind, failure, information, success

### Community 140 - "Field"
Cohesion: 0.50
Nodes (4): Field, name, S, M

### Community 141 - ".start"
Cohesion: 0.67
Nodes (3): CapabilityService::start(), thread, UpdateCallback

### Community 143 - "EnumNames<caps::AudioRole>"
Cohesion: 0.67
Nodes (3): EnumNames<caps::AudioRole>, last, names

### Community 144 - "EnumNames<caps::Confidence>"
Cohesion: 0.67
Nodes (3): EnumNames<caps::Confidence>, last, names

### Community 145 - "EnumNames<caps::CpuArchitecture>"
Cohesion: 0.67
Nodes (3): EnumNames<caps::CpuArchitecture>, last, names

### Community 146 - "EnumNames<caps::HdrMode>"
Cohesion: 0.67
Nodes (3): EnumNames<caps::HdrMode>, last, names

### Community 147 - "EnumNames<caps::PlanStatus>"
Cohesion: 0.67
Nodes (3): EnumNames<caps::PlanStatus>, last, names

### Community 148 - "EnumNames<caps::SourceKind>"
Cohesion: 0.67
Nodes (3): EnumNames<caps::SourceKind>, last, names

## Knowledge Gaps
- **874 isolated node(s):** `id`, `title`, `keys`, `depth`, `label` (+869 more)
  These have ≤1 connection - possible missing edges or undocumented components. (Counts symbols only; 1211 node(s) total have ≤1 connection when file, concept and rationale nodes are included.)
- **22 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `CapabilitySnapshot` connect `CapabilitySnapshot` to `canonical_determinism_test.cpp`, `Validator`, `Differ`, `candidate_ranking.cpp`, `local_envelope_test.cpp`, `policy_test.cpp`, `RuntimeState`, `LocalQualityEnvelope`, `Dimensions`, `snapshot_diff_test.cpp`, `CapturePathCapability`, `ProbeRecord`, `EncoderCapability`, `model.hpp`, `AudioEndpointCapability`, `derive_media_plan`, `OsVersion`, `HardwareCapabilities`, `SnapshotHeader`, `ProbeIssue`, `probe_coordinator.cpp`, `ProbeSchedule`, `string_view`, `ProfileDecision`, `SnapshotUpdateResult`, `CapabilityService::Impl`, `ValidationReport`, `CapabilityReport`, `probe_coordinator_test.cpp`?**
  _High betweenness centrality (0.123) - this node is a cross-community bridge._
- **Why does `Validator` connect `Validator` to `CapabilitySnapshot`, `ValidationReport`?**
  _High betweenness centrality (0.048) - this node is a cross-community bridge._
- **What connects `id`, `title`, `keys` to the rest of the system?**
  _874 weakly-connected nodes found - possible documentation gaps or missing edges._
- **Should `CapabilitySnapshot` be split into smaller, more focused modules?**
  _Cohesion score 0.05411823522059742 - nodes in this community are weakly interconnected._
- **Should `canonical_determinism_test.cpp` be split into smaller, more focused modules?**
  _Cohesion score 0.1076923076923077 - nodes in this community are weakly interconnected._
- **Should `Validator` be split into smaller, more focused modules?**
  _Cohesion score 0.08231569425599276 - nodes in this community are weakly interconnected._
- **Should `schema.hpp` be split into smaller, more focused modules?**
  _Cohesion score 0.029850746268656716 - nodes in this community are weakly interconnected._