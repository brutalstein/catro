# Graph Report - native-capability-foundation  (2026-09-27)

## Corpus Check
- 55 files · ~72,538 words
- Verdict: corpus is large enough that graph structure adds value.
- Unclassified: 5 file(s) not represented in the graph (top: (none) 3, .cmake 2)

## Summary
- 1448 nodes · 2782 edges · 132 communities (112 shown, 19 thin omitted)
- Extraction: 97% EXTRACTED · 3% INFERRED · 0% AMBIGUOUS · INFERRED: 93 edges (avg confidence: 0.85)
- Token cost: 0 input · 0 output

## Graph Freshness
- Built from commit: `dd02e413`
- Run `git rev-parse HEAD` and compare to check if the graph is stale.
- Run `graphify update .` after code changes (no API cost).

## Community Hubs (Navigation)
- capability_fixtures.cpp
- canonical_json.cpp
- Validator
- schema.hpp
- human_report.cpp
- snapshot_diff.cpp
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
- quality_point
- LocalQualityEnvelope
- Dimensions
- policy_pathology_test.cpp
- snapshot_diff_test.cpp
- MediaDecisionRequest
- MediaPlan
- CapturePathCapability
- GpuCapability
- EnvelopeBuilder
- Rational
- DisplayState
- ProbeRecord
- CpuCapability
- EncoderCapability
- derive_media_plan
- Downgrade
- model_test.cpp
- operating_profile_test.cpp
- AudioEndpointCapability
- model.hpp
- DisplayCapability
- PolicyVersion
- OsVersion
- catro_capabilities
- HardwareCapabilities
- FakeProbeExecutor
- DeviceInventory
- SnapshotHeader
- ProbeFragment
- CapturePermissionState
- ProbeIssue
- EnumNames<caps::AudioDirection>
- EnumNames<caps::CandidateOutcome>
- EnumNames<caps::CaptureApi>
- EnumNames<caps::CapturePermission>
- EnumNames<caps::ChromaSubsampling>
- EnumNames<caps::Codec>
- EnumNames<caps::CodecProfile>
- EnumNames<caps::ColorGamut>
- EnumNames<caps::ColorRange>
- probe_coordinator.cpp
- EnumNames<caps::Consequence>
- EnumNames<caps::Conversion>
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
- CapabilitySnapshot
- EnumNames<caps::PolicyRule>
- EnumNames<caps::PowerSource>
- ChangeSet
- EnumNames<caps::ReasonCode>
- rank_candidates
- EnumNames<caps::SimdFeature>
- ReportParseResult
- EnumNames<caps::Support>
- EnumNames<caps::ThermalPressure>
- EnumNames<caps::TransferFunction>
- EnumNames<caps::TranslationState>
- SystemProbeFacts
- Schema<caps::CategoryTruncation>
- ProbeSpec
- Schema<caps::Dimensions>
- ProfileDecision
- Schema<caps::LocalQualityEnvelope>
- build_snapshot
- RuntimeProbeFacts
- snapshot_diff.hpp
- string_view
- Schema<caps::ScopedId<Tag>>
- Schema<caps::StartingQuality>
- Native Foundation Implementation Plan
- probe_coordinator.hpp
- CapabilityReport
- probe_coordinator_test.cpp
- CandidateRef
- GpuDisplayProbeFacts
- SupportFact
- capability_fixtures.hpp
- fallback_encoders
- PlatformIdentity
- DimensionRange
- EnumNames<caps::GpuKind>
- EnumNames<caps::GraphicsApi>
- EnumNames<caps::PlatformRole>
- EnumNames<caps::ProbeOutcome>
- EnumNames<caps::ProfileRule>
- EnumNames<caps::TransferKind>
- known_value
- Schema<caps::CandidateRef>
- Schema<caps::CapabilitySnapshot>
- Schema<caps::CapturePermissionState>
- Schema<caps::DimensionRange>
- Schema<caps::MediaCandidate>
- Schema<caps::RationalRange>
- Schema<caps::RuntimeProbeFacts>
- Schema<caps::RuntimeState>
- Schema<caps::SchemaVersion>
- Schema<caps::SupportFact>
- Schema<caps::TraceRecord>
- Schema<caps::TransferPathCapability>

## God Nodes (most connected - your core abstractions)
1. `CapabilitySnapshot` - 74 edges
2. `ProbeFragment` - 44 edges
3. `Observed` - 43 edges
4. `Validator` - 40 edges
5. `valid_snapshot()` - 38 edges
6. `EncoderModeCapability` - 34 edges
7. `MediaPlan` - 32 edges
8. `text()` - 32 edges
9. `known()` - 32 edges
10. `MediaCandidate` - 30 edges

## Surprising Connections (you probably didn't know these)
- `audio_endpoints()` --calls--> `endpoint`  [INFERRED]
  tests/fixtures/capability_fixtures.cpp → core/capabilities/include/catro/capabilities/model.hpp
- `hdr_request()` --references--> `MediaDecisionRequest`  [EXTRACTED]
  tests/capabilities/policy_test.cpp → core/capabilities/include/catro/capabilities/media_plan.hpp
- `next_generation()` --references--> `CapabilitySnapshot`  [EXTRACTED]
  tests/capabilities/snapshot_diff_test.cpp → core/capabilities/include/catro/capabilities/model.hpp
- `envelope()` --calls--> `derive_local_envelope()`  [INFERRED]
  tests/capabilities/local_envelope_test.cpp → core/capabilities/src/local_envelope.cpp
- `plan_for()` --calls--> `derive_media_plan()`  [INFERRED]
  tests/capabilities/policy_pathology_test.cpp → core/capabilities/src/policy.cpp

## Import Cycles
- None detected.

## Hyperedges (group relationships)
- **Platform Abstraction Layer** — platform_windows_capability_service, platform_macos_capability_service, tools_capability_probe [EXTRACTED 0.90]
- **Catro Core Architecture** — core_capabilities_catro_capabilities, core_reporting_catro_reporting, core_capabilities_model, core_capabilities_policy_engine [EXTRACTED 1.00]

## Communities (132 total, 19 thin omitted)

### Community 0 - "capability_fixtures.cpp"
Cohesion: 0.06
Nodes (112): absent(), advertised(), apple_silicon_macbook(), audio_endpoints(), audio_states(), base_snapshot(), AudioEndpointId, CaptureApi (+104 more)

### Community 1 - "canonical_json.cpp"
Cohesion: 0.07
Nodes (56): canonical_order(), check_plan_bounds(), E, I, microseconds, optional, ReportErrorCode, string (+48 more)

### Community 2 - "Validator"
Cohesion: 0.08
Nodes (45): string, ScopedId, scope, value, at(), codec_of(), collect(), color_consistent() (+37 more)

### Community 3 - "schema.hpp"
Cohesion: 0.03
Nodes (64): EnumNames, Schema, Schema<caps::AudioEndpointCapability>, fields, Schema<caps::AudioEndpointState>, fields, Schema<caps::AudioProbeFacts>, fields (+56 more)

### Community 4 - "human_report.cpp"
Cohesion: 0.07
Nodes (52): bool_constant<!Record<T>>, E, I, int64_t, microseconds, optional, size_t, string (+44 more)

### Community 5 - "snapshot_diff.cpp"
Cohesion: 0.21
Nodes (13): affected(), AudioEndpointId, ChangeDomain, Item, Tag, vector, diff_by_key(), Differ (+5 more)

### Community 6 - "validation_test.cpp"
Cohesion: 0.05
Nodes (41): string, ValidationCode, vector, ValidationError, code, path, ValidationReport, errors (+33 more)

### Community 7 - "Observed"
Cohesion: 0.15
Nodes (15): Confidence, EvidenceMethod, IssueCode, optional, T, Observed, provenance_, value_ (+7 more)

### Community 8 - "reporting_test.cpp"
Cohesion: 0.13
Nodes (18): "canonical JSON has a fixed shape and declaration field order", size_t, string, string_view, edited(), "every fixture report round-trips byte for byte", "facts carry explicit knowledge, provenance, and stable enum names", has() (+10 more)

### Community 9 - "candidate_ranking.cpp"
Cohesion: 0.15
Nodes (22): codec_order(), array, Codec, optional, PolicyRule, ReasonCode, size_t, uint64_t (+14 more)

### Community 10 - "EncoderModeCapability"
Cohesion: 0.09
Nodes (26): ChromaSubsampling, ColorRange, EncoderModeCapability, bit_depth, chroma, codec, color_range, dimensions (+18 more)

### Community 11 - "MediaCandidate"
Cohesion: 0.11
Nodes (18): Codec, Consequence, GpuId, ImplementationClass, TransferKind, MediaCandidate, capture, codec (+10 more)

### Community 12 - "array"
Cohesion: 0.08
Nodes (25): EnumNames<caps::AudioRole>, last, names, EnumNames<caps::Confidence>, last, names, EnumNames<caps::CpuArchitecture>, last (+17 more)

### Community 13 - "local_envelope_test.cpp"
Cohesion: 0.10
Nodes (19): "a known hardware path is bounded by the source display", "a missing requested display produces no envelope", "a timed-out encoder family leaves no encoder and degraded confidence", "capture limits bound the frame rate when known", EncoderId, ReasonCode, "denied capture permission leaves no capture path", "exact 59.94 Hz survives the envelope" (+11 more)

### Community 14 - "policy_test.cpp"
Cohesion: 0.12
Nodes (15): "a coherent desktop selects its same-resource hardware path", "a same-adapter copy beats a cross-adapter copy", "a same-resource path beats a same-adapter copy", "Apple Silicon keeps unprovable encoder affinity explicit", TransferKind, "HDR modes are rejected when HDR is not requested or not achievable", hdr_request(), "HDR requests select an HDR-preserving path when the local envelope allows it" (+7 more)

### Community 15 - "RuntimeState"
Cohesion: 0.14
Nodes (14): MemoryPressure, PowerSource, ThermalPressure, RuntimeState, audio_endpoints, battery_present, capture_permissions, displays (+6 more)

### Community 16 - "TransferPathCapability"
Cohesion: 0.18
Nodes (11): Conversion, EncoderId, TransferKind, TransferPathCapability, conversions, destination, destination_gpu, evidence (+3 more)

### Community 17 - "TraceRecord"
Cohesion: 0.12
Nodes (18): CandidateOutcome, CategoryTruncation, category, omitted, DecisionTrace, records, truncated, PolicyRule (+10 more)

### Community 18 - "quality_point"
Cohesion: 0.16
Nodes (15): optional, quality_point(), ReasonCode, vector, QualityPoint, bound_rate, bound_size, degraded (+7 more)

### Community 19 - "LocalQualityEnvelope"
Cohesion: 0.12
Nodes (17): Confidence, uint8_t, LocalQualityEnvelope, bit_depth, confidence, frame_rate, hdr, reasons (+9 more)

### Community 20 - "Dimensions"
Cohesion: 0.23
Nodes (12): Dimensions, height, width, better(), ReasonCode, uint64_t, even(), fit() (+4 more)

### Community 21 - "policy_pathology_test.cpp"
Cohesion: 0.12
Nodes (15): "a missing GPU driver rejects the hardware encoder and falls back to software explicitly", "a missing microphone does not change the video plan", "a partial probe failure reports no viable path with degraded confidence", DisplayId, "headless sessions report no viable local path", "invalid input is rejected before any planning", "large inventories truncate deterministically within the trace bounds", "mixed refresh displays keep exact rational rates" (+7 more)

### Community 22 - "snapshot_diff_test.cpp"
Cohesion: 0.12
Nodes (15): "a display mode change reports the affected display", "a thermal transition reports only thermal", "added and removed devices are affected", "an audio default-role change reports only audio output", ChangeDomain, vector, domains_of(), "enumeration order alone is not a change" (+7 more)

### Community 23 - "MediaDecisionRequest"
Cohesion: 0.16
Nodes (13): DisplayId, LatencyClass, OperatingPreference, SourceKind, MediaDecisionRequest, display, latency, preference (+5 more)

### Community 24 - "MediaPlan"
Cohesion: 0.14
Nodes (14): MediaPlan, downgrades, envelope, fallbacks, policy_version, profile, reasons, request (+6 more)

### Community 25 - "CapturePathCapability"
Cohesion: 0.14
Nodes (14): CapturePathCapability, api, frame_rates, gpu, hdr_output, id, output_formats, source (+6 more)

### Community 26 - "GpuCapability"
Cohesion: 0.14
Nodes (14): GpuCapability, dedicated_memory, device_id, graphics_apis, id, kind, name, preferred_for_high_performance (+6 more)

### Community 27 - "EnvelopeBuilder"
Cohesion: 0.23
Nodes (10): CapturePathId, EncoderId, vector, EnvelopeBuilder, ceiling_, permission_denied(), preserves_hdr(), source_display() (+2 more)

### Community 28 - "Rational"
Cohesion: 0.21
Nodes (8): RequestedQuality, frame_rate, hdr, resolution, uint32_t, operator<=>(), Rational, strong_ordering

### Community 29 - "DisplayState"
Cohesion: 0.17
Nodes (12): DisplayMode, logical, pixels, refresh_rate, DisplayState, active_mode, display, hdr_enabled (+4 more)

### Community 30 - "ProbeRecord"
Cohesion: 0.15
Nodes (13): int64_t, microseconds, ProbeFamily, ProbeOutcome, ProbeRecord, duration, fact_count, family (+5 more)

### Community 31 - "CpuCapability"
Cohesion: 0.17
Nodes (12): CpuCapability, efficiency_cores, logical_cores, native_architecture, performance_cores, physical_cores, process_architecture, simd (+4 more)

### Community 32 - "EncoderCapability"
Cohesion: 0.17
Nodes (12): EncoderCapability, backend, codec, gpu, id, implementation, modes, name (+4 more)

### Community 33 - "derive_media_plan"
Cohesion: 0.31
Nodes (8): ceiling_for(), bound_trace(), derive_media_plan(), Ceiling, box, frame_rate, hdr, within()

### Community 34 - "Downgrade"
Cohesion: 0.33
Nodes (6): Downgrade, profile, quality, trigger, OperatingProfile, DowngradeTrigger

### Community 35 - "model_test.cpp"
Cohesion: 0.12
Nodes (15): AudioEndpointIdTag, CapturePathIdTag, DisplayIdTag, EncoderIdTag, GpuIdTag, "a snapshot models multiple GPUs without a primary-GPU assumption", "default observations have no provenance so validation can reject them", "device identifiers are distinct types with explicit scope" (+7 more)

### Community 36 - "operating_profile_test.cpp"
Cohesion: 0.18
Nodes (10): "an unknown power source yields the safe local envelope", "battery alone derives efficiency under automatic preference", "battery plus low-power mode derives efficiency", "critical thermal pressure constrains every preference", "explicit preference outranks power source", "fair thermal pressure tempers performance to balanced", "headless, remote, and critical-memory sessions use the safe local envelope", "mains power derives performance only for a known desktop role" (+2 more)

### Community 37 - "AudioEndpointCapability"
Cohesion: 0.13
Nodes (16): AudioDirection, AudioRole, AudioEndpointCapability, channels, direction, id, name, sample_formats (+8 more)

### Community 38 - "model.hpp"
Cohesion: 0.32
Nodes (3): string, vector, map

### Community 39 - "DisplayCapability"
Cohesion: 0.20
Nodes (10): DisplayCapability, bits_per_channel, gamut, gpu, hdr, id, modes, ColorGamut (+2 more)

### Community 40 - "PolicyVersion"
Cohesion: 0.20
Nodes (12): is_supported(), PolicyVersion, major, minor, patch, SchemaVersion, major, minor (+4 more)

### Community 41 - "OsVersion"
Cohesion: 0.40
Nodes (5): uint32_t, OsVersion, build, major, minor

### Community 42 - "catro_capabilities"
Cohesion: 0.25
Nodes (8): catro_capabilities, Capability Domain Model, Policy Engine, catro_reporting, macOS Capability Service, Windows Capability Service, catro-capability-probe, catro-capability-report

### Community 43 - "HardwareCapabilities"
Cohesion: 0.25
Nodes (8): Bytes, value, HardwareCapabilities, cpu, installed_memory, platform_role, uint64_t, PlatformRole

### Community 44 - "FakeProbeExecutor"
Cohesion: 0.10
Nodes (22): RunningProbe, terminate, wait_until, Behavior, fragment, never_completes, ready_after, FakeProbeExecutor (+14 more)

### Community 45 - "DeviceInventory"
Cohesion: 0.29
Nodes (7): DeviceInventory, audio_endpoints, capture_paths, displays, encoders, gpus, transfer_paths

### Community 46 - "SnapshotHeader"
Cohesion: 0.29
Nodes (7): UtcTimestamp, SnapshotHeader, captured_at, generation, probe_revision, schema_id, schema_version

### Community 47 - "ProbeFragment"
Cohesion: 0.09
Nodes (25): AudioProbeFacts, endpoints, states, EncoderProbeFacts, encoders, int64_t, microseconds, optional (+17 more)

### Community 48 - "CapturePermissionState"
Cohesion: 0.40
Nodes (5): CapturePermission, CapturePermissionState, path, permission, CapturePathId

### Community 49 - "ProbeIssue"
Cohesion: 0.40
Nodes (5): IssueCode, string, ProbeIssue, code, probe_id

### Community 51 - "EnumNames<caps::AudioDirection>"
Cohesion: 0.67
Nodes (3): EnumNames<caps::AudioDirection>, last, names

### Community 52 - "EnumNames<caps::CandidateOutcome>"
Cohesion: 0.67
Nodes (3): EnumNames<caps::CandidateOutcome>, last, names

### Community 53 - "EnumNames<caps::CaptureApi>"
Cohesion: 0.67
Nodes (3): EnumNames<caps::CaptureApi>, last, names

### Community 54 - "EnumNames<caps::CapturePermission>"
Cohesion: 0.67
Nodes (3): EnumNames<caps::CapturePermission>, last, names

### Community 55 - "EnumNames<caps::ChromaSubsampling>"
Cohesion: 0.67
Nodes (3): EnumNames<caps::ChromaSubsampling>, last, names

### Community 56 - "EnumNames<caps::Codec>"
Cohesion: 0.67
Nodes (3): EnumNames<caps::Codec>, last, names

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
Cohesion: 0.17
Nodes (20): budget_for(), clear_payload(), collect_snapshot(), microseconds, milliseconds, optional, ProbeDomain, ProbeFamily (+12 more)

### Community 61 - "EnumNames<caps::Consequence>"
Cohesion: 0.67
Nodes (3): EnumNames<caps::Consequence>, last, names

### Community 62 - "EnumNames<caps::Conversion>"
Cohesion: 0.67
Nodes (3): EnumNames<caps::Conversion>, last, names

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
Nodes (20): milliseconds, OperatingSystem, uint32_t, uint64_t, UtcTimestamp, vector, ProbeSchedule, captured_at (+12 more)

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

### Community 78 - "CapabilitySnapshot"
Cohesion: 0.14
Nodes (16): CapabilitySnapshot, devices, hardware, header, issues, platform, probes, runtime (+8 more)

### Community 79 - "EnumNames<caps::PolicyRule>"
Cohesion: 0.67
Nodes (3): EnumNames<caps::PolicyRule>, last, names

### Community 80 - "EnumNames<caps::PowerSource>"
Cohesion: 0.67
Nodes (3): EnumNames<caps::PowerSource>, last, names

### Community 81 - "ChangeSet"
Cohesion: 0.12
Nodes (15): ChangeSet, audio_endpoints, capture_paths, displays, domains, encoders, gpus, AudioEndpointId (+7 more)

### Community 82 - "EnumNames<caps::ReasonCode>"
Cohesion: 0.67
Nodes (3): EnumNames<caps::ReasonCode>, last, names

### Community 83 - "rank_candidates"
Cohesion: 0.16
Nodes (15): mode_key(), affinity_penalty(), consequences(), Consequence, GpuId, ImplementationClass, OperatingProfile, TransferKind (+7 more)

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

### Community 88 - "EnumNames<caps::TransferFunction>"
Cohesion: 0.67
Nodes (3): EnumNames<caps::TransferFunction>, last, names

### Community 89 - "EnumNames<caps::TranslationState>"
Cohesion: 0.67
Nodes (3): EnumNames<caps::TranslationState>, last, names

### Community 90 - "SystemProbeFacts"
Cohesion: 0.22
Nodes (14): SystemProbeFacts, hardware, platform, IssueCode, OperatingSystem, string, string_view, T (+6 more)

### Community 92 - "ProbeSpec"
Cohesion: 0.15
Nodes (13): milliseconds, ProbeDomain, ProbeFamily, string, uint32_t, ProbeSpec, access, family (+5 more)

### Community 94 - "ProfileDecision"
Cohesion: 0.23
Nodes (11): ProfileDecision, profile, rule, OperatingPreference, optional, T, derive_operating_profile(), known_true() (+3 more)

### Community 96 - "build_snapshot"
Cohesion: 0.18
Nodes (12): add_issue(), build_snapshot(), time_point, uint32_t, unique_ptr, vector, fact_count(), Run (+4 more)

### Community 97 - "RuntimeProbeFacts"
Cohesion: 0.18
Nodes (11): MemoryPressure, PowerSource, ThermalPressure, RuntimeProbeFacts, battery_present, headless, low_power_mode, memory_pressure (+3 more)

### Community 98 - "snapshot_diff.hpp"
Cohesion: 0.25
Nodes (8): bitset, optional, SnapshotUpdate, changes, snapshot, SnapshotUpdateResult, update, validation

### Community 99 - "string_view"
Cohesion: 0.25
Nodes (9): enum_from(), enum_name(), Field, name, S, E, optional, string_view (+1 more)

### Community 103 - "probe_coordinator.hpp"
Cohesion: 0.25
Nodes (6): optional, ProbeExecutor, start, SnapshotPublication, snapshot, validation

### Community 104 - "CapabilityReport"
Cohesion: 0.32
Nodes (8): CapabilityReport, plan, snapshot, make_report(), vector, every_report(), hdr_request(), report_for()

### Community 105 - "probe_coordinator_test.cpp"
Cohesion: 0.25
Nodes (7): "a cross-fragment dangling reference rejects the referencing fragment", "a fragment with duplicate IDs is rejected without poisoning publication", "all probe helpers start before the coordinator waits", "every terminal probe outcome produces one record", "the global deadline publishes explicit unknowns for a blocked family", "the initial schedule fixes stable probe IDs and hard budgets", "valid fragments merge without rewriting fact provenance"

### Community 106 - "CandidateRef"
Cohesion: 0.29
Nodes (7): CandidateRef, capture, encoder, mode, CapturePathId, EncoderId, optional

### Community 107 - "GpuDisplayProbeFacts"
Cohesion: 0.29
Nodes (7): GpuDisplayProbeFacts, capture_paths, capture_permissions, display_states, displays, gpus, transfer_paths

### Community 108 - "SupportFact"
Cohesion: 0.33
Nodes (6): SupportFact, provenance, status, LatencyClass, latency_penalty(), Support

### Community 109 - "capability_fixtures.hpp"
Cohesion: 0.53
Nodes (5): IssueCode, string_view, T, unavailable(), unknown()

### Community 110 - "fallback_encoders"
Cohesion: 0.40
Nodes (5): contains(), EncoderId, ReasonCode, vector, fallback_encoders()

### Community 111 - "PlatformIdentity"
Cohesion: 0.50
Nodes (4): OperatingSystem, PlatformIdentity, os, version

### Community 112 - "DimensionRange"
Cohesion: 0.67
Nodes (3): DimensionRange, maximum, minimum

### Community 113 - "EnumNames<caps::GpuKind>"
Cohesion: 0.67
Nodes (3): EnumNames<caps::GpuKind>, last, names

### Community 114 - "EnumNames<caps::GraphicsApi>"
Cohesion: 0.67
Nodes (3): EnumNames<caps::GraphicsApi>, last, names

### Community 115 - "EnumNames<caps::PlatformRole>"
Cohesion: 0.67
Nodes (3): EnumNames<caps::PlatformRole>, last, names

### Community 116 - "EnumNames<caps::ProbeOutcome>"
Cohesion: 0.67
Nodes (3): EnumNames<caps::ProbeOutcome>, last, names

### Community 117 - "EnumNames<caps::ProfileRule>"
Cohesion: 0.67
Nodes (3): EnumNames<caps::ProfileRule>, last, names

### Community 118 - "EnumNames<caps::TransferKind>"
Cohesion: 0.67
Nodes (3): EnumNames<caps::TransferKind>, last, names

## Knowledge Gaps
- **629 isolated node(s):** `probe_id`, `method`, `confidence`, `issue`, `value_` (+624 more)
  These have ≤1 connection - possible missing edges or undocumented components. (Counts symbols only; 819 node(s) total have ≤1 connection when file, concept and rationale nodes are included.)
- **19 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `CapabilitySnapshot` connect `CapabilitySnapshot` to `capability_fixtures.cpp`, `canonical_json.cpp`, `Validator`, `snapshot_diff.cpp`, `local_envelope_test.cpp`, `policy_test.cpp`, `RuntimeState`, `LocalQualityEnvelope`, `policy_pathology_test.cpp`, `snapshot_diff_test.cpp`, `MediaDecisionRequest`, `EnvelopeBuilder`, `ProbeRecord`, `derive_media_plan`, `AudioEndpointCapability`, `model.hpp`, `PolicyVersion`, `HardwareCapabilities`, `DeviceInventory`, `SnapshotHeader`, `ProbeIssue`, `ProbeSchedule`, `rank_candidates`, `ProfileDecision`, `build_snapshot`, `snapshot_diff.hpp`, `probe_coordinator.hpp`, `CapabilityReport`, `PlatformIdentity`?**
  _High betweenness centrality (0.164) - this node is a cross-community bridge._
- **Why does `Observed` connect `Observed` to `capability_fixtures.cpp`, `canonical_json.cpp`, `Validator`, `human_report.cpp`, `validation_test.cpp`, `EncoderModeCapability`, `RuntimeState`, `TransferPathCapability`, `CapturePathCapability`, `GpuCapability`, `DisplayState`, `CpuCapability`, `EncoderCapability`, `AudioEndpointCapability`, `DisplayCapability`, `HardwareCapabilities`, `CapturePermissionState`, `SystemProbeFacts`, `ProfileDecision`, `RuntimeProbeFacts`, `capability_fixtures.hpp`, `PlatformIdentity`, `known_value`?**
  _High betweenness centrality (0.064) - this node is a cross-community bridge._
- **Why does `ProbeFragment` connect `ProbeFragment` to `build_snapshot`, `RuntimeProbeFacts`, `canonical_json.cpp`, `model.hpp`, `PolicyVersion`, `GpuDisplayProbeFacts`, `FakeProbeExecutor`, `probe_coordinator.cpp`, `CapabilitySnapshot`, `ProbeIssue`, `ReportParseResult`, `SystemProbeFacts`, `ProbeSpec`?**
  _High betweenness centrality (0.055) - this node is a cross-community bridge._
- **What connects `probe_id`, `method`, `confidence` to the rest of the system?**
  _629 weakly-connected nodes found - possible documentation gaps or missing edges._
- **Should `capability_fixtures.cpp` be split into smaller, more focused modules?**
  _Cohesion score 0.06226295828065739 - nodes in this community are weakly interconnected._
- **Should `canonical_json.cpp` be split into smaller, more focused modules?**
  _Cohesion score 0.07401129943502825 - nodes in this community are weakly interconnected._
- **Should `Validator` be split into smaller, more focused modules?**
  _Cohesion score 0.08231569425599276 - nodes in this community are weakly interconnected._