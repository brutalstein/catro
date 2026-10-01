# Graph Report - catro  (2026-10-01)

## Corpus Check
- 338 files · ~267,407 words
- Verdict: corpus is large enough that graph structure adds value.
- Unclassified: 22 file(s) not represented in the graph (top: (none) 5, .idl 5, .cmake 2)

## Summary
- 7081 nodes · 12598 edges · 458 communities (407 shown, 40 thin omitted)
- Extraction: 96% EXTRACTED · 4% INFERRED · 0% AMBIGUOUS · INFERRED: 490 edges (avg confidence: 0.84)
- Token cost: 0 input · 0 output

## Graph Freshness
- Built from commit: `3a332cec`
- Run `git rev-parse HEAD` and compare to check if the graph is stale.
- Run `graphify update .` after code changes (no API cost).

## Community Hubs (Navigation)
- capability_fixtures.cpp
- canonical_determinism_test.cpp
- Validator
- schema.hpp
- PresentedRow
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
- WindowsScreenShareRuntime::Impl
- ServerView
- TraceRecord
- SpscRing
- ValidationReport
- Dimensions
- policy_pathology_test.cpp
- snapshot_diff_test.cpp
- WindowsGraphicsCapture::Impl
- MediaDecisionRequest
- DiagnosticsView
- GpuCapability
- EnvelopeBuilder
- Rational
- ScreenCaptureStatistics
- WasapiStream
- CpuCapability
- VideoEncoderStatistics
- Enumeration
- MediaPlan
- model_test.cpp
- ScreenShareSnapshot
- RuntimeState
- string
- NativeAudioEndpoint
- AudioStatistics
- Platform
- catro_capabilities
- directory_client.cpp
- FakeProbeExecutor
- DiagnosticsModel
- DirectoryServer
- ProbeFragment
- ProcessProbe
- AudioView
- WindowsH264HardwareEncoder::Impl
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
- directory.cpp
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
- CatroCapabilitiesBridge.h
- EnumNames<caps::SimdFeature>
- ReportParseResult
- EnumNames<caps::Support>
- EnumNames<caps::ThermalPressure>
- VideoDecoderStatistics
- EnumNames<caps::TranslationState>
- Translator
- report_cli_test.cpp
- MacVideoPresenter
- DiagnosticsViewModel.cpp
- operating_profile_test.cpp
- .capability
- Run
- ~Impl
- SnapshotUpdateResult
- enum_name
- CapabilityService::Impl
- room_voice_runtime_test.cpp
- README.md
- CapabilitySnapshot
- EncoderConfig
- probe_coordinator_test.cpp
- NativeGpu
- GpuDisplayProbeFacts
- NoticeBanner
- DisplayCapability
- DiagnosticsViewModel
- UserControl
- NativeAdapter
- EnumNames<caps::GpuKind>
- VoicePipeline
- EnumNames<caps::PlatformRole>
- EnumNames<caps::ProbeOutcome>
- AGENTS.md
- EnumNames<caps::TransferKind>
- canonical_json.cpp
- Schema<caps::CandidateRef>
- Schema<caps::CapabilitySnapshot>
- FakeRenderStream
- text
- MainWindow
- .window_procedure
- NativeAudioDevice
- LevelRow
- Window
- Schema<caps::SupportFact>
- Schema<caps::TraceRecord>
- Schema<caps::TransferPathCapability>
- directory
- ServerView.Screen.cpp
- CapturePathCapability
- RoomRuntime
- 0001: Split native shells over a shared C++ core
- 0002: Isolated, passive, budgeted probes
- capability_service.cpp
- ProbeSchedule
- ScreenShareConfig
- ProbeIssue
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
- ScreenCaptureStatistics
- UdpEndpoint
- .websocket
- wmain
- testing.T
- WorkspaceSnapshot
- ExternalAudioSession
- JitterBuffer
- VoiceRuntimeHost
- DuplicationBundle
- main
- CatroVoiceRuntimeConfig
- .run_stream_audio_receiver
- .run_receiver
- voice_runtime.cpp
- ServerView::StartVoice
- community/model.hpp
- local_state.cpp
- audio_test.cpp
- model.cpp
- RtpPacketSlice
- D3D11CompositionVideoPresenter::Impl
- video_encoder.cpp
- FakePlatform
- room_mesh_transport.cpp
- rtp_h264.cpp
- H264DecoderStatistics
- run_voice_peer
- EntropySource
- RenderBridge
- screen_capture.cpp
- ProcessLoopbackAudioCapture
- HardwareEncoderStatistics
- windows/translation_test.cpp
- UserControl
- human_report.cpp
- CaptureBridge
- Server Code discovery and approval-based join requests — design
- UserControl
- directory.go
- ui_policy_test.cpp
- DiagnosticsView.xaml.cpp
- Windows/macOS production parity and distribution readiness — design
- DirectoryClient
- .start
- ShellState
- RoomMeshTransport::Impl
- jitter.cpp
- VoicePipelineStatistics
- Cross-platform releases and one-command installers — design
- process_probe_executor.cpp
- Platform
- pipeline_test.cpp
- ServerView.Directory.cpp
- VoicePipelineConfig
- udp_transport_test.cpp
- HardwareEncoderError
- system_probe.cpp
- RuntimeShape
- Activate
- jitter_test.cpp
- video_peer.cpp
- VideoPeerOptions
- NetworkStatistics
- byte
- VoicePeerControl
- DirectoryMessage
- ActivationHandler
- H264RtpReassembler
- rtp_h264_test.cpp
- Oracle Free Production Rooms — Design
- VideoReceiveSource
- Peer
- PlayoutFrame
- JitterStatistics
- audio_bridge.cpp
- ScreenCaptureError
- WindowsH264D3D11Decoder::Impl
- macos/translation_test.cpp
- VoicePeerOptions
- CatroRoomRuntimeSnapshot
- room_runtime.cpp
- CaptureSource
- geometry.hpp
- H264DecoderError
- HardwareEncoderConfig
- .run
- parse
- milliseconds
- AudioViewModel
- CatroRoomRuntimeConfig
- GpuCaptureFrame
- CaptureSink
- Button
- LocalQualityEnvelope
- NativeDisplay
- decode_local_state
- UdpPeerSocket
- RenderBridgeStatistics
- write_packet_header
- VoicePipeline::RemoteStream
- Review Focus
- InitializeComponent
- StreamInfo
- RoomMeshTransport
- StartingQuality
- Native UI stabilization and modular product-shell design
- CachedInputView
- AudioNotifications
- DecodedGpuFrame
- StreamAudioStatistics
- create_decoder_device
- EncodeCheckOptions
- realtime_bridge_test.cpp
- PacketSendContext
- Mode
- WindowsScreenShareRuntime
- PacketContext
- run_audio_check
- DirectoryError
- CaptureBridgeStatistics
- verify.sh script
- ParsedBaseUrl
- Bounded persistent text channels — design
- Oracle Free production acceptance record
- joinTestRoomPeer
- Kind
- activate_h264_d3d11_decoder
- GpuShape
- voice-peer/main.cpp
- UpdateVoiceUi
- LocalState
- size_t
- App
- Review Focus
- Audio Capture and Playback — Design
- load_or_create_credential_bytes
- CapabilityService
- .start
- room_runtime_test.cpp
- AudioSessionView
- .present
- 14. Design 2/3 — Capability Schema and Policy
- Oracle Free production operations
- Two-Client Low-Latency Voice — Design
- Authenticated server member roster — design
- DirectoryCancellationSource
- VideoPresenterStatistics
- runtime_probe.cpp
- video_decoder.cpp
- Datagram
- RoomTransportCallbacks
- Review Focus
- DirectoryHttpRequest
- LocalStateError
- EncodedAccessUnit
- VideoPresenterConfig
- ProcessLoopbackAudioCapture::Impl
- MainWindow.Directory.cpp
- audio-check/main.cpp
- AudioPlatform
- Border
- SettingsView
- InviteCode
- Decoder
- VideoEncoderConfig
- RoomMeshConfig
- 29. Additional Engineering Principles Added During Review
- Strong domain objects
- Voice + Stream Interaction Reference
- Server Code discovery and approval-based join requests — implementation plan
- CatroCapabilitiesBridge
- string_view
- Differ
- WasapiStreamAudioRenderer::Impl
- SequenceEntropy
- voice_test.cpp
- SequenceEntropy
- video-peer/main.cpp
- VoicePacketView
- CodecError
- 13. Design 1/3 — Foundation Boundaries
- 9. High-Level Media Research Conclusions
- Windows H.264 hardware encode slice
- Review Focus
- video_presenter.cpp
- diagnostics_test.cpp
- directory_test.cpp
- MacH264HardwareDecoder
- macos-installer-test.sh
- MacH264HardwareEncoder
- rtc_room_transport_test.cpp
- ServerView::BeginScreenShare
- audio_platform_test.cpp
- CatroDirectorySessionDelegate
- Oracle Free production bundle
- Windows screen capture slice
- Building
- Native UI foundation
- Native UI stabilization validation
- Voice channel runtime
- LocalStateError
- arguments
- parse_video_peer_arguments
- ComPtr
- Grid
- CodecError
- H264PacketizeResult
- Capability system
- Personal identity and server state
- Two-Client Low-Latency Voice — Implementation Plan
- Bounded persistent text channels — implementation plan
- Authenticated server member roster — implementation plan
- Voice peer validation
- H264DecoderConfig
- H264RtpConfig
- HandleCloser
- process_loopback_audio.cpp
- Catro
- Catro signaling and server directory
- ProbeNames
- windows-installer-test.ps1
- CaptureBridge::trim_backlog
- Contributing
- CatroScreenStreamOutput
- string
- text.hpp
- H.264 RTP packetization slice
- Two-client H.264 screen streaming
- HWND
- presentation_state_test.cpp
- TempStatePath
- ScreenCaptureError
- process_probe_executor_test.cpp
- windows/local_state_test.cpp
- screen_capture_test.cpp
- screen_runtime_test.cpp
- capability-report/main.cpp
- .detail
- NavigationEntry
- StackPanel
- system_runtime_probe_test.cpp
- 16. Probe Behavior
- 3. Business/Hosting Direction
- Troubleshooting
- frame_period
- shell_model_test.cpp
- ScreenCaptureNativeAdapter
- windows_keyboard_smoke.ps1
- video_peer_test.cpp
- geometry_test.cpp
- Security
- ChannelHeaderRow
- ChannelsColumn
- LocalShareSwapChainPanel
- MemberList
- ServerOwnerIcon
- EnumNames<caps::ProfileRule>
- video_decoder_test.cpp
- restore.sh
- 21. Diagnostics Workspace
- 5. Non-Negotiable Native Requirement
- 8. Distribution Requirements
- StreamAudioError
- SystemEntropy::fill
- size_t
- name
- install-macos.sh
- package-macos.sh
- macos/voice_runtime_test.cpp
- video_encoder_test.cpp
- windows/voice_runtime_test.cpp
- Composer
- H264RtpReassembler::mark_damage
- 15. Design 3/3 — Probing, Diagnostics, Testing, Completion
- 17. Mode and Transfer Modeling
- 2026-09-27-audio-engine.md
- 2026-09-30-windows-macos-production-parity/progress.md
- 2026-10-01-native-ui-stabilization/progress.md
- directory_cancellation_test.cpp
- video_presenter_test.cpp
- github.com/brutalstein/catro/services/signaling
- .statistics
- capability-probe/main.cpp

## God Nodes (most connected - your core abstractions)
1. `ServerView` - 128 edges
2. `WindowsScreenShareRuntime::Impl` - 108 edges
3. `WindowsGraphicsCapture::Impl` - 84 edges
4. `CapabilitySnapshot` - 79 edges
5. `WindowsH264HardwareEncoder::Impl` - 65 edges
6. `UserControl` - 61 edges
7. `ProbeFragment` - 58 edges
8. `Observed` - 57 edges
9. `VoicePipeline` - 56 edges
10. `ScreenShareSnapshot` - 54 edges

## Surprising Connections (you probably didn't know these)
- `run_voice_peer()` --calls--> `statistics`  [INFERRED]
  tools/voice-peer/voice_peer.cpp → core/audio/include/catro/audio/external_session.hpp
- `emit_packet()` --calls--> `sink`  [INFERRED]
  core/video/src/rtp_h264.cpp → tests/audio/audio_test.cpp
- `valid_config()` --references--> `CatroRoomRuntimeConfig`  [EXTRACTED]
  tests/rtc/room_runtime_test.cpp → apps/room-runtime/windows/include/catro/room_runtime.h
- `OnStart` --calls--> `Failure`  [INFERRED]
  apps/windows/Catro/Audio/AudioView.xaml.h → core/reporting/src/canonical_json.cpp
- `Refresh` --calls--> `Failure`  [INFERRED]
  apps/windows/Catro/Audio/AudioView.xaml.h → core/reporting/src/canonical_json.cpp

## Import Cycles
- None detected.

## Hyperedges (group relationships)
- **Platform Abstraction Layer** — platform_windows_capability_service, platform_macos_capability_service, tools_capability_probe [EXTRACTED 0.90]
- **Catro Core Architecture** — core_capabilities_catro_capabilities, core_reporting_catro_reporting, core_capabilities_model, core_capabilities_policy_engine [EXTRACTED 1.00]

## Communities (458 total, 40 thin omitted)

### Community 0 - "capability_fixtures.cpp"
Cohesion: 0.12
Nodes (72): advertised(), apple_silicon_macbook(), audio_endpoints(), audio_states(), base_snapshot(), AudioEndpointId, CaptureApi, CapturePathId (+64 more)

### Community 1 - "canonical_determinism_test.cpp"
Cohesion: 0.11
Nodes (26): make_report(), locale, Baseline, human, json, snapshot, baselines(), optional (+18 more)

### Community 2 - "Validator"
Cohesion: 0.08
Nodes (48): string, ScopedId, scope, value, DimensionRange, maximum, minimum, at() (+40 more)

### Community 3 - "schema.hpp"
Cohesion: 0.03
Nodes (68): EnumNames, Schema, Schema<caps::AudioProbeFacts>, fields, Schema<caps::CapturePathCapability>, fields, Schema<caps::CapturePermissionState>, fields (+60 more)

### Community 4 - "PresentedRow"
Cohesion: 0.12
Nodes (26): FactState, optional, string, uint32_t, vector, PresentedRow, depth, label (+18 more)

### Community 5 - "snapshot_diff.cpp"
Cohesion: 0.21
Nodes (11): affected(), Item, Tag, vector, diff_by_key(), diff_snapshots(), make_snapshot_update(), probe_state() (+3 more)

### Community 6 - "validation_test.cpp"
Cohesion: 0.06
Nodes (31): "a reference with the right value but wrong scope is dangling", "a timed-out family yields a valid partial snapshot", "absent or degraded evidence must state why", "an unset observation has no provenance and is rejected", "backends and capture APIs must belong to the snapshot platform", "battery power without a battery is contradictory", T, "device names are bounded, well-formed UTF-8" (+23 more)

### Community 7 - "Observed"
Cohesion: 0.09
Nodes (29): Confidence, EvidenceMethod, IssueCode, optional, string, T, Observed, provenance_ (+21 more)

### Community 8 - "reporting_test.cpp"
Cohesion: 0.10
Nodes (25): CapabilityReport, plan, snapshot, "canonical JSON has a fixed shape and declaration field order", size_t, string, string_view, vector (+17 more)

### Community 9 - "candidate_ranking.cpp"
Cohesion: 0.12
Nodes (29): codec_order(), consequences(), array, Codec, Consequence, ImplementationClass, OperatingProfile, optional (+21 more)

### Community 10 - "EncoderModeCapability"
Cohesion: 0.04
Nodes (64): ChromaSubsampling, ColorRange, Conversion, SupportFact, provenance, status, value, EncoderCapability (+56 more)

### Community 11 - "MediaCandidate"
Cohesion: 0.09
Nodes (24): CandidateRef, capture, encoder, mode, CapturePathId, Consequence, EncoderId, GpuId (+16 more)

### Community 12 - "array"
Cohesion: 0.07
Nodes (28): EnumNames<caps::AudioDirection>, last, names, EnumNames<caps::CandidateOutcome>, last, names, EnumNames<caps::Codec>, last (+20 more)

### Community 13 - "local_envelope_test.cpp"
Cohesion: 0.10
Nodes (20): "a known hardware path is bounded by the source display", "a missing requested display produces no envelope", "a timed-out encoder family leaves no encoder and degraded confidence", "capture limits bound the frame rate when known", EncoderId, ReasonCode, "denied capture permission leaves no capture path", enable_hdr_output() (+12 more)

### Community 14 - "policy_test.cpp"
Cohesion: 0.11
Nodes (19): "a coherent desktop selects its same-resource hardware path", "a same-adapter copy beats a cross-adapter copy", "a same-resource path beats a same-adapter copy", "Apple Silicon keeps unprovable encoder affinity explicit", contains(), EncoderId, ReasonCode, TransferKind (+11 more)

### Community 15 - "WindowsScreenShareRuntime::Impl"
Cohesion: 0.03
Nodes (70): condition_variable, mutex, ScreenShareState, unique_ptr, WindowsScreenShareRuntime::Impl, backpressure_events_, capture_contention_drops_, encoded_height_ (+62 more)

### Community 16 - "ServerView"
Cohesion: 0.03
Nodes (79): CatroRoomRuntimeHandle, CatroVoiceRuntimeHandle, ComPtr, DispatcherQueueTimer, IDXGISwapChain1, optional, string, uint64_t (+71 more)

### Community 17 - "TraceRecord"
Cohesion: 0.12
Nodes (19): CandidateOutcome, CategoryTruncation, category, omitted, DecisionTrace, records, truncated, PolicyRule (+11 more)

### Community 18 - "SpscRing"
Cohesion: 0.05
Nodes (37): atomic, uint32_t, uint64_t, MonitorPipe, high_water_, primed_, ring_, target_ (+29 more)

### Community 19 - "ValidationReport"
Cohesion: 0.13
Nodes (14): optional, SnapshotPublication, snapshot, validation, string, ValidationCode, vector, ValidationError (+6 more)

### Community 20 - "Dimensions"
Cohesion: 0.09
Nodes (34): Dimensions, height, width, better(), ceiling_for(), optional, ReasonCode, uint64_t (+26 more)

### Community 21 - "policy_pathology_test.cpp"
Cohesion: 0.13
Nodes (14): "a missing GPU driver rejects the hardware encoder and falls back to software explicitly", "a missing microphone does not change the video plan", "a partial probe failure reports no viable path with degraded confidence", DisplayId, "headless sessions report no viable local path", "invalid input is rejected before any planning", "large inventories truncate deterministically within the trace bounds", "mixed refresh displays keep exact rational rates" (+6 more)

### Community 22 - "snapshot_diff_test.cpp"
Cohesion: 0.12
Nodes (15): "a display mode change reports the affected display", "a thermal transition reports only thermal", "added and removed devices are affected", "an audio default-role change reports only audio output", ChangeDomain, vector, domains_of(), "enumeration order alone is not a change" (+7 more)

### Community 23 - "WindowsGraphicsCapture::Impl"
Cohesion: 0.04
Nodes (54): Direct3D11CaptureFramePool, event_token, GraphicsCaptureSession, array, atomic, atomic_bool, DXGI_FORMAT, ID3D11Texture2D (+46 more)

### Community 24 - "MediaDecisionRequest"
Cohesion: 0.12
Nodes (16): DisplayId, LatencyClass, OperatingPreference, SourceKind, MediaDecisionRequest, display, latency, preference (+8 more)

### Community 25 - "DiagnosticsView"
Cohesion: 0.11
Nodes (28): application_directory(), ExportFormat, IInspectable, path, RoutedEventArgs, DiagnosticsView, Announce, Apply (+20 more)

### Community 26 - "GpuCapability"
Cohesion: 0.05
Nodes (42): Bytes, GpuCapability, dedicated_memory, device_id, graphics_apis, id, kind, name (+34 more)

### Community 27 - "EnvelopeBuilder"
Cohesion: 0.20
Nodes (11): CapturePathId, EncoderId, vector, EnvelopeBuilder, ceiling_, permission_denied(), preserves_hdr(), T (+3 more)

### Community 28 - "Rational"
Cohesion: 0.14
Nodes (13): DisplayState, active_mode, display, hdr_enabled, primary, scale, DisplayId, uint32_t (+5 more)

### Community 29 - "ScreenCaptureStatistics"
Cohesion: 0.05
Nodes (48): CaptureEnumerationResult, error, sources, CaptureSource, application_name, height, kind, native_id (+40 more)

### Community 30 - "WasapiStream"
Cohesion: 0.06
Nodes (43): AudioError, code, native_code, int64_t, AudioEngine::fail(), uint64_t, IAudioCaptureClient, IAudioRenderClient (+35 more)

### Community 31 - "CpuCapability"
Cohesion: 0.12
Nodes (17): CpuCapability, efficiency_cores, logical_cores, native_architecture, performance_cores, physical_cores, process_architecture, simd (+9 more)

### Community 32 - "VideoEncoderStatistics"
Cohesion: 0.06
Nodes (35): EncodedAccessUnit, bytes, keyframe, pts_100ns, sequence, byte, int64_t, optional (+27 more)

### Community 33 - "Enumeration"
Cohesion: 0.08
Nodes (34): Adapters, hardware, with_outputs, add_issue(), Codec, GUID, HRESULT, IDXGIAdapter1 (+26 more)

### Community 34 - "MediaPlan"
Cohesion: 0.09
Nodes (30): MediaPlan, downgrades, envelope, fallbacks, policy_version, profile, reasons, request (+22 more)

### Community 35 - "model_test.cpp"
Cohesion: 0.12
Nodes (15): AudioEndpointIdTag, CapturePathIdTag, DisplayIdTag, EncoderIdTag, GpuIdTag, "a snapshot models multiple GPUs without a primary-GPU assumption", "default observations have no provenance so validation can reject them", "device identifiers are distinct types with explicit scope" (+7 more)

### Community 36 - "ScreenShareSnapshot"
Cohesion: 0.04
Nodes (52): ScreenShareState, uint64_t, ScreenShareSnapshot, backpressure_events, capture_contention_drops, encoded_height, encoded_width, encoder_input_failures (+44 more)

### Community 37 - "RuntimeState"
Cohesion: 0.06
Nodes (37): AudioEndpointCapability, channels, direction, id, name, sample_formats, sample_rate_hz, AudioEndpointState (+29 more)

### Community 38 - "string"
Cohesion: 0.09
Nodes (11): AudioDeviceChoice, id, label, AudioEndpointId, optional, string, vector, CatroAudioDevice (+3 more)

### Community 39 - "NativeAudioEndpoint"
Cohesion: 0.07
Nodes (33): NativeEndpointState, AudioDirection, AudioRole, Codec, optional, PixelFormat, SampleFormat, string (+25 more)

### Community 40 - "AudioStatistics"
Cohesion: 0.05
Nodes (46): AudioEngine, error_, fail, generation_, lifecycle_mutex_, mutex_, on_failure_, session_ (+38 more)

### Community 41 - "Platform"
Cohesion: 0.07
Nodes (40): BlockingPlatform, capture_opened, capture_started, release_capture, start_order, atomic_bool, AudioEndpointId, latch (+32 more)

### Community 42 - "catro_capabilities"
Cohesion: 0.25
Nodes (8): catro_capabilities, Capability Domain Model, Policy Engine, catro_reporting, macOS Capability Service, Windows Capability Service, catro-capability-probe, catro-capability-report

### Community 43 - "directory_client.cpp"
Cohesion: 0.14
Nodes (49): DirectoryServiceConfig, allow_insecure_http, api_base_url, accept_directory_invite(), cancel_directory_join_request(), DirectoryErrorCode, DirectoryInviteResult, DirectoryJoinRequestResult (+41 more)

### Community 44 - "FakeProbeExecutor"
Cohesion: 0.11
Nodes (20): Behavior, fragment, never_completes, ready_after, FakeProbeExecutor, behaviors_, launched_, terminated_ (+12 more)

### Community 45 - "DiagnosticsModel"
Cohesion: 0.07
Nodes (34): DiagnosticsModel, changes, detail, generation, headline, probes, sections, tone (+26 more)

### Community 46 - "DirectoryServer"
Cohesion: 0.05
Nodes (47): DirectoryInvite, code, expires, server, DirectoryJoinRequest, created_at, expires_at, id (+39 more)

### Community 47 - "ProbeFragment"
Cohesion: 0.05
Nodes (42): int64_t, microseconds, optional, ProbeDomain, ProbeFamily, ProbeOutcome, string, uint32_t (+34 more)

### Community 48 - "ProcessProbe"
Cohesion: 0.09
Nodes (23): atomic_bool, HANDLE, microseconds, optional, ProbeOutcome, string, thread, time_point (+15 more)

### Community 49 - "AudioView"
Cohesion: 0.09
Nodes (35): AudioView, engine_, InitializeComponent, inputs_, Mode, OnModeChanged, OnStart, OnStop (+27 more)

### Community 51 - "WindowsH264HardwareEncoder::Impl"
Cohesion: 0.04
Nodes (45): IMFMediaEventGenerator, MediaEventType, MFT_OUTPUT_STREAM_INFO, ID3D11Device, ID3D11DeviceContext, ID3D11VideoContext, ID3D11VideoContext1, ID3D11VideoDevice (+37 more)

### Community 52 - "CapabilityService"
Cohesion: 0.13
Nodes (12): ProbeExecutor, start, CapabilityService, impl_, refresh, start, stop, path (+4 more)

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
Nodes (25): DISPLAYCONFIG_PATH_TARGET_INFO, DXGI_GPU_PREFERENCE, IDXGIFactory6, NativeCaptureApis, add_dxcore_properties(), add_issue(), int64_t, IssueCode (+17 more)

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
Cohesion: 0.07
Nodes (50): MemoryPressure, PowerSource, ThermalPressure, RuntimeProbeFacts, battery_present, headless, low_power_mode, memory_pressure (+42 more)

### Community 61 - "EnumNames<caps::Consequence>"
Cohesion: 0.67
Nodes (3): EnumNames<caps::Consequence>, last, names

### Community 62 - "audio_probe.cpp"
Cohesion: 0.17
Nodes (24): IMMDevice, IMMDeviceEnumerator, add_issue(), AudioRole, int64_t, IssueCode, map, optional (+16 more)

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

### Community 67 - "directory.cpp"
Cohesion: 0.11
Nodes (43): ascii_space(), DirectoryInviteResult, DirectoryJoinRequestResult, DirectoryJoinRequestsResult, DirectoryMembersResult, DirectoryMessageResult, DirectoryMessagesResult, DirectoryServerLookupResult (+35 more)

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
Nodes (29): ColorGamut, optional, uint32_t, uint8_t, vector, NativeCaptureApis, access_granted, screen_capture_kit (+21 more)

### Community 79 - "EnumNames<caps::PolicyRule>"
Cohesion: 0.67
Nodes (3): EnumNames<caps::PolicyRule>, last, names

### Community 80 - "EnumNames<caps::PowerSource>"
Cohesion: 0.67
Nodes (3): EnumNames<caps::PowerSource>, last, names

### Community 81 - "ChangeSet"
Cohesion: 0.12
Nodes (15): ChangeSet, audio_endpoints, capture_paths, displays, domains, encoders, gpus, AudioEndpointId (+7 more)

### Community 82 - "string_view"
Cohesion: 0.06
Nodes (23): ChannelSpec, id, kind, name, string_view, atomic, mutex, thread (+15 more)

### Community 83 - "CatroCapabilitiesBridge.h"
Cohesion: 0.16
Nodes (22): CatroAudioBridge, -session, -startWithModeinputoutput, -stop, CatroAudioDevice, -init, -initWithChoice, CatroAudioSession (+14 more)

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

### Community 88 - "VideoDecoderStatistics"
Cohesion: 0.07
Nodes (31): int64_t, optional, size_t, string, uint64_t, name(), VideoDecoderConfig, max_access_unit_bytes (+23 more)

### Community 89 - "EnumNames<caps::TranslationState>"
Cohesion: 0.67
Nodes (3): EnumNames<caps::TranslationState>, last, names

### Community 90 - "Translator"
Cohesion: 0.12
Nodes (32): bounded_text(), codec_name(), CaptureApi, Codec, DisplayId, EncoderId, GpuId, IssueCode (+24 more)

### Community 91 - "report_cli_test.cpp"
Cohesion: 0.07
Nodes (37): ReportCollector, "a report file is written once and never replaced", optional, path, size_t, string, string_view, vector (+29 more)

### Community 92 - "MacVideoPresenter"
Cohesion: 0.06
Nodes (30): int64_t, uint32_t, uint64_t, unique_ptr, VideoPresenterErrorCode, MacVideoPresenter, impl_, present (+22 more)

### Community 93 - "DiagnosticsViewModel.cpp"
Cohesion: 0.17
Nodes (20): build_diagnostics(), change_lines(), array, ExportFormat, microseconds, ProbeOutcome, string, string_view (+12 more)

### Community 94 - "operating_profile_test.cpp"
Cohesion: 0.10
Nodes (22): OperatingProfile, ProfileDecision, profile, rule, OperatingPreference, optional, T, derive_operating_profile() (+14 more)

### Community 95 - ".capability"
Cohesion: 0.13
Nodes (25): bounded_text(), codec_name(), Codec, DisplayId, EncoderId, GpuId, IssueCode, NativeDisplay (+17 more)

### Community 96 - "Run"
Cohesion: 0.15
Nodes (11): RunningProbe, terminate, wait_until, time_point, unique_ptr, Run, fragment, process (+3 more)

### Community 97 - "~Impl"
Cohesion: 0.13
Nodes (15): unique_ptr, MacScreenCapture, enumerate_sources, impl_, start_source, statistics, stop, wait_for_latest (+7 more)

### Community 98 - "SnapshotUpdateResult"
Cohesion: 0.25
Nodes (8): bitset, optional, SnapshotUpdate, changes, snapshot, SnapshotUpdateResult, update, validation

### Community 99 - "enum_name"
Cohesion: 0.25
Nodes (9): enum_from(), enum_name(), Field, name, S, E, optional, string_view (+1 more)

### Community 100 - "CapabilityService::Impl"
Cohesion: 0.12
Nodes (18): CapabilityService::Impl, audio_notifications, current, executor, lifecycle_mutex, pending, ready, ready_changed (+10 more)

### Community 101 - "room_voice_runtime_test.cpp"
Cohesion: 0.07
Nodes (39): atomic, atomic_bool, byte, CatroRoomRuntimeHandle, condition_variable, deque, function, int32_t (+31 more)

### Community 102 - "README.md"
Cohesion: 0.15
Nodes (7): Dependencies, Platform frameworks, Source dependencies, Toolchains, Windows shell NuGet packages, Native Foundation Implementation Plan, Native Foundation and Capability System Design

### Community 103 - "CapabilitySnapshot"
Cohesion: 0.13
Nodes (20): CapabilitySnapshot, devices, hardware, header, issues, platform, probes, runtime (+12 more)

### Community 104 - "EncoderConfig"
Cohesion: 0.08
Nodes (38): CodecApplication, EncoderConfig, application, bitrate, channels, complexity, expected_packet_loss_percent, inband_fec (+30 more)

### Community 105 - "probe_coordinator_test.cpp"
Cohesion: 0.25
Nodes (7): "a cross-fragment dangling reference rejects the referencing fragment", "a fragment with duplicate IDs is rejected without poisoning publication", "all probe helpers start before the coordinator waits", "every terminal probe outcome produces one record", "the global deadline publishes explicit unknowns for a blocked family", "the initial schedule fixes stable probe IDs and hard budgets", "valid fragments merge without rewriting fact provenance"

### Community 106 - "NativeGpu"
Cohesion: 0.11
Nodes (19): Codec, string, uint64_t, NativeEncoder, codec, encoder_id, gpu, hardware (+11 more)

### Community 107 - "GpuDisplayProbeFacts"
Cohesion: 0.20
Nodes (10): AudioProbeFacts, endpoints, states, GpuDisplayProbeFacts, capture_paths, capture_permissions, display_states, displays (+2 more)

### Community 108 - "NoticeBanner"
Cohesion: 0.15
Nodes (21): Badge, .body, CatroFactState, .marker, CatroTone, .color, .name, FactRow (+13 more)

### Community 109 - "DisplayCapability"
Cohesion: 0.13
Nodes (15): DisplayCapability, bits_per_channel, gamut, gpu, hdr, id, modes, DisplayMode (+7 more)

### Community 110 - "DiagnosticsViewModel"
Cohesion: 0.17
Nodes (13): CatroApp, .body, DiagnosticsView, .body, .body, DiagnosticsViewModel, Notice, String (+5 more)

### Community 111 - "UserControl"
Cohesion: 0.13
Nodes (16): AudioPanel, DiagnosticsView, ExportButton, Navigation, Notice, RefreshButton, Rows, SectionTitle (+8 more)

### Community 112 - "NativeAdapter"
Cohesion: 0.10
Nodes (21): uint32_t, uint64_t, NativeAdapter, dedicated_memory, description, detachable, device_id, direct3d11 (+13 more)

### Community 113 - "EnumNames<caps::GpuKind>"
Cohesion: 0.67
Nodes (3): EnumNames<caps::GpuKind>, last, names

### Community 114 - "VoicePipeline"
Cohesion: 0.04
Nodes (43): atomic, atomic_bool, PcmFrame, RemoteStream, unique_ptr, VoicePipeline, capture_, capture_frame_ (+35 more)

### Community 115 - "EnumNames<caps::PlatformRole>"
Cohesion: 0.67
Nodes (3): EnumNames<caps::PlatformRole>, last, names

### Community 116 - "EnumNames<caps::ProbeOutcome>"
Cohesion: 0.67
Nodes (3): EnumNames<caps::ProbeOutcome>, last, names

### Community 117 - "AGENTS.md"
Cohesion: 0.05
Nodes (40): 10. Hardware Intelligence Philosophy, 11. Native Foundation Milestone, 12. Explicit Non-Goals of the Current Milestone, 18. Quality Concepts, 19. Validation Rules, 1. Product Vision, 20. Policy Identity and Bounded Decision Traces, 22. Capability Report Tool (+32 more)

### Community 118 - "EnumNames<caps::TransferKind>"
Cohesion: 0.67
Nodes (3): EnumNames<caps::TransferKind>, last, names

### Community 119 - "canonical_json.cpp"
Cohesion: 0.14
Nodes (36): canonical_order(), check_plan_bounds(), E, I, Json, microseconds, optional, ReportErrorCode (+28 more)

### Community 122 - "FakeRenderStream"
Cohesion: 0.08
Nodes (27): AudioStream, glitches, request_stop, start, FakeAudioPlatform, capture_error, capture_sample_, rendered_nonzero_samples (+19 more)

### Community 123 - "text"
Cohesion: 0.13
Nodes (26): E, FactState, I, int64_t, microseconds, optional, size_t, string (+18 more)

### Community 124 - "MainWindow"
Cohesion: 0.08
Nodes (27): DispatcherQueueTimer, optional, string, UIElement, vector, MainWindow, active_directory_server_, approved_join_requests_waiting_refresh_ (+19 more)

### Community 125 - ".window_procedure"
Cohesion: 0.22
Nodes (6): LPARAM, LRESULT, callback, set, UINT, WPARAM

### Community 126 - "NativeAudioDevice"
Cohesion: 0.18
Nodes (11): AudioDirection, SampleFormat, NativeAudioDevice, alive, channels, direction, is_default, name (+3 more)

### Community 127 - "LevelRow"
Cohesion: 0.22
Nodes (7): AppKit, LevelRow, .body, String, Double, SwiftUI, UniformTypeIdentifiers

### Community 128 - "Window"
Cohesion: 0.13
Nodes (19): AppTitleBar, CatroMascot, DiagnosticsSelection, JoinedServersPanel, JoinServerButton, MainWindow, ServerButton, ServerSelection (+11 more)

### Community 132 - "directory"
Cohesion: 0.23
Nodes (10): net/http.Request, net/http.ResponseWriter, credentialHash(), decodeJSON(), parseBearer(), validBoundedText(), writeAPIError(), writeAPIJSON() (+2 more)

### Community 133 - "ServerView.Screen.cpp"
Cohesion: 0.10
Nodes (30): IInspectable, RoutedEventArgs, uint32_t, fit_viewport(), ServerView::LocalStreamId(), ServerView::OnFullScreenStream(), ServerView::OnLeaveStream(), ServerView::OnPopOutStream() (+22 more)

### Community 134 - "CapturePathCapability"
Cohesion: 0.11
Nodes (19): CapturePermission, CapturePathCapability, api, frame_rates, gpu, hdr_output, id, output_formats (+11 more)

### Community 135 - "RoomRuntime"
Cohesion: 0.07
Nodes (29): atomic, atomic_bool, condition_variable, deque, mutex, string, uint64_t, DatagramQueue (+21 more)

### Community 136 - "0001: Split native shells over a shared C++ core"
Cohesion: 0.40
Nodes (4): 0001: Split native shells over a shared C++ core, Consequences, Context, Decision

### Community 137 - "0002: Isolated, passive, budgeted probes"
Cohesion: 0.40
Nodes (4): 0002: Isolated, passive, budgeted probes, Consequences, Context, Decision

### Community 138 - "capability_service.cpp"
Cohesion: 0.24
Nodes (6): CapabilityService::CapabilityService(), CapabilityService::stop(), condition_variable, path, same_key(), PROPERTYKEY

### Community 139 - "ProbeSchedule"
Cohesion: 0.12
Nodes (17): OperatingSystem, ProbeFamily, RefreshReason, uint32_t, uint64_t, UtcTimestamp, vector, ProbeSchedule (+9 more)

### Community 140 - "ScreenShareConfig"
Cohesion: 0.07
Nodes (31): CaptureSource, CatroRoomRuntimeHandle, int32_t, size_t, uint16_t, uint32_t, uint8_t, ScreenShareConfig (+23 more)

### Community 141 - "ProbeIssue"
Cohesion: 0.22
Nodes (15): IssueCode, ProbeIssue, code, probe_id, EncoderProbeFacts, encoders, transfer_paths, NativeEncoder (+7 more)

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

### Community 170 - "ScreenCaptureStatistics"
Cohesion: 0.10
Nodes (21): optional, ScreenCaptureBackend, ScreenCaptureState, uint64_t, ScreenCaptureConfig, adapter_luid, backend, borderless (+13 more)

### Community 171 - "UdpEndpoint"
Cohesion: 0.10
Nodes (30): string, uint16_t, UdpEndpoint, address, port, send_segments, close_socket(), byte (+22 more)

### Community 172 - ".websocket"
Cohesion: 0.13
Nodes (16): sync/atomic.Int64, sync/atomic.Uint64, sync.Mutex, sync.RWMutex, room, service, mintToken(), TestTamperedAndExpiredTokensAreRejected() (+8 more)

### Community 173 - "wmain"
Cohesion: 0.08
Nodes (32): unique_ptr, WindowsGraphicsCapture, impl_, start_primary_display, start_source, statistics, stop, wait_for_latest (+24 more)

### Community 174 - "testing.T"
Cohesion: 0.23
Nodes (29): directory, testing.T, canonicalServerCode(), ephemeralICEServers(), messageKey(), openDirectory(), authenticatedRequest(), registerTestUser() (+21 more)

### Community 175 - "WorkspaceSnapshot"
Cohesion: 0.11
Nodes (29): ActionState, availability, available, begin, disable, enable, fail, ready (+21 more)

### Community 176 - "ExternalAudioSession"
Cohesion: 0.07
Nodes (29): ExternalAudioSession, error_, fail, generation_, lifecycle_mutex_, mutex_, on_failure_, session_ (+21 more)

### Community 177 - "JitterBuffer"
Cohesion: 0.07
Nodes (35): atomic, atomic_bool, JitterBuffer, accepted_, advance_playout, buffered_, copy_payload, duplicates_ (+27 more)

### Community 178 - "VoiceRuntimeHost"
Cohesion: 0.07
Nodes (31): DirectPeerRunner, int32_t, string, RoomVoiceApi, VoiceRuntimeHost, control_, direct_, error_ (+23 more)

### Community 179 - "DuplicationBundle"
Cohesion: 0.09
Nodes (29): D3D11_BOX, IDirect3DDevice, IDXGIOutput1, IDXGIOutputDuplication, capture_client_rect(), ComPtr, D3D11_TEXTURE2D_DESC, ID3D11Device (+21 more)

### Community 180 - "main"
Cohesion: 0.13
Nodes (22): net/http.HandlerFunc, net.IP, time.Duration, time.Time, main(), runHealthCheck(), environmentBool(), environmentInt() (+14 more)

### Community 181 - "CatroVoiceRuntimeConfig"
Cohesion: 0.07
Nodes (27): CatroVoiceRuntimeConfig, bind_address, bind_port, bitrate, input_endpoint, jitter_packets, output_endpoint, peer_address (+19 more)

### Community 182 - ".run_stream_audio_receiver"
Cohesion: 0.09
Nodes (17): atomic, atomic_bool, span, uint64_t, StreamAudioCaptureBridge, resync_events_, resync_requested_, ring_ (+9 more)

### Community 183 - ".run_receiver"
Cohesion: 0.13
Nodes (17): int64_t, ScreenShareErrorCode, string, string_view, extended_rtp_to_100ns(), trace_event(), udp_error_text(), WindowsScreenShareRuntime::stop() (+9 more)

### Community 184 - "voice_runtime.cpp"
Cohesion: 0.11
Nodes (20): catro_voice_runtime_create(), catro_voice_runtime_destroy(), catro_voice_runtime_set_deafened(), catro_voice_runtime_set_muted(), catro_voice_runtime_snapshot(), catro_voice_runtime_start(), catro_voice_runtime_stop(), CatroVoiceRuntimeHandle (+12 more)

### Community 185 - "ServerView::StartVoice"
Cohesion: 0.17
Nodes (19): direct_video_config(), direct_voice_config(), DirectVideoConfig, bind, peer, DirectVoiceConfig, bind, peer (+11 more)

### Community 186 - "community/model.hpp"
Cohesion: 0.08
Nodes (30): can_manage_server(), Channel, id, kind, name, ChannelBlueprint, kind, name (+22 more)

### Community 187 - "local_state.cpp"
Cohesion: 0.20
Nodes (23): LocalStateError, bounded_codec_detail(), CodecError, LocalStateErrorCode, optional, path, string, string_view (+15 more)

### Community 188 - "audio_test.cpp"
Cohesion: 0.07
Nodes (26): "a concurrent stop waits for an in-flight start and leaves no stream running", "a meter session opens only the capture stream", "a monitor session carries capture to render and estimates latency", "a monitor session starts render before capture", "a stream failure fails the session once and stale failures are ignored", "a tone session renders the test tone on the default output", BlockingPlatform, capture_opened (+18 more)

### Community 189 - "model.cpp"
Cohesion: 0.19
Nodes (22): channel_id_from_hex(), ChannelId, optional, ServerId, size_t, string, string_view, UserId (+14 more)

### Community 190 - "RtpPacketSlice"
Cohesion: 0.10
Nodes (21): H264ReassemblyResult, error, frame, status, array, byte, H264ReassemblyError, span (+13 more)

### Community 191 - "D3D11CompositionVideoPresenter::Impl"
Cohesion: 0.08
Nodes (26): IDXGISwapChain3, kInputViewCache, array, ID3D11DeviceContext, ID3D11VideoContext, ID3D11VideoContext1, ID3D11VideoDevice, ID3D11VideoProcessor (+18 more)

### Community 192 - "video_encoder.cpp"
Cohesion: 0.14
Nodes (19): activate_h264_encoder(), GUID, ICodecAPI, IMFActivate, IMFTransform, LUID, string, time_point (+11 more)

### Community 193 - "FakePlatform"
Cohesion: 0.09
Nodes (26): atomic_bool, AudioEndpointId, OpenResult, optional, StreamFailure, string, uint64_t, endpoint() (+18 more)

### Community 194 - "room_mesh_transport.cpp"
Cohesion: 0.15
Nodes (18): RoomTransportErrorCode, string, on_state, RoomTransportError, code, message, optional, vector (+10 more)

### Community 195 - "rtp_h264.cpp"
Cohesion: 0.19
Nodes (26): byte, size_t, span, uint16_t, uint32_t, uint64_t, uint8_t, emit_packet() (+18 more)

### Community 196 - "H264DecoderStatistics"
Cohesion: 0.08
Nodes (25): H264DecoderStatistics, adapter_luid, compressed_bytes, d3d11_aware, decode_max_us, decode_total_us, decoder_name, frames_decoded (+17 more)

### Community 197 - "run_voice_peer"
Cohesion: 0.18
Nodes (24): audio_info(), audio_mode(), CodecError, ExternalSessionMode, int64_t, Integer, optional, ostream (+16 more)

### Community 198 - "EntropySource"
Cohesion: 0.09
Nodes (19): EntropySource, fill, SystemEntropy, fill, "bootstrap creates stable personal server invariants from injected entropy", "bootstrap reports secure entropy failure instead of fabricating identity", byte, span (+11 more)

### Community 199 - "RenderBridge"
Cohesion: 0.08
Nodes (22): RenderBridge, callbacks_, deafened_samples_rendered_, frames_enqueued_, on_render, pcm_samples_rendered_, peak_buffered_samples_, primed_ (+14 more)

### Community 200 - "screen_capture.cpp"
Cohesion: 0.16
Nodes (22): GraphicsCaptureItem, HMONITOR, LONG, capture_item_for_monitor(), capture_item_for_source(), capture_item_for_window(), condition_variable, DWORD (+14 more)

### Community 201 - "ProcessLoopbackAudioCapture"
Cohesion: 0.15
Nodes (11): unique_ptr, ProcessLoopbackAudioCapture, impl_, start, statistics, stop, WasapiStreamAudioRenderer, impl_ (+3 more)

### Community 202 - "HardwareEncoderStatistics"
Cohesion: 0.08
Nodes (24): HardwareEncoderStatistics, adapter_luid, asynchronous, conversion_failures, conversion_max_us, conversion_total_us, d3d11_aware, encode_max_us (+16 more)

### Community 203 - "windows/translation_test.cpp"
Cohesion: 0.10
Nodes (21): "adapter kinds come from DXGI and DXCore, never from the vendor", "audio endpoints report measured defaults and explicit gaps", "capture paths are granted only when usable", "cloned primary targets make the primary display unprovable", Codec, IssueCode, NativeDisplay, NativeEncoder (+13 more)

### Community 204 - "UserControl"
Cohesion: 0.11
Nodes (22): AudioView, Details, Failure, HeadphonesNotice, InputLevel, InputMeter, InputPicker, Modes (+14 more)

### Community 205 - "human_report.cpp"
Cohesion: 0.15
Nodes (22): bool_constant<!Record<T>>, IsOptional, IsOptional<std::optional<T>>, IsVector, IsVector<std::vector<T>>, OneLine, OneLine<caps::CandidateRef>, OneLine<caps::DimensionRange> (+14 more)

### Community 206 - "CaptureBridge"
Cohesion: 0.09
Nodes (23): CaptureBridge, alignment_samples_pending_, callbacks_, captured_samples_, dropped_callbacks_, dropped_samples_, frames_dequeued_, on_captured (+15 more)

### Community 207 - "Server Code discovery and approval-based join requests — design"
Cohesion: 0.09
Nodes (22): Abuse and privacy, Add-server button, API, Authorization model, Cancel, Create, Decision, External design references (+14 more)

### Community 208 - "UserControl"
Cohesion: 0.16
Nodes (21): AccessRequestCount, ChannelTitle, MemberCountLabel, OnlineStatusText, ProfileName, ProfileRoleText, RemoteShareMetaText, ServerName (+13 more)

### Community 209 - "directory.go"
Cohesion: 0.13
Nodes (20): bearerToken(), cloneMembers(), descriptorFor(), randomInviteCode(), randomJoinRequestID(), randomMessageID(), randomServerCode(), validJoinRequestStatus() (+12 more)

### Community 210 - "ui_policy_test.cpp"
Cohesion: 0.10
Nodes (25): "async server actions publish visible busy and failure reasons", path, size_t, string, string_view, "dynamic server controls keep accessible names", "icon-only server controls keep accessible names and minimum targets", line_count() (+17 more)

### Community 211 - "DiagnosticsView.xaml.cpp"
Cohesion: 0.19
Nodes (24): badge(), badge_style(), FactState, FrameworkElement, hstring, optional, pair, string (+16 more)

### Community 212 - "Windows/macOS production parity and distribution readiness — design"
Cohesion: 0.10
Nodes (20): 10. Automated verification, 11. Real-machine E2E matrix, 12. Distribution and security, 13. Release gates, 14. Non-goals, 1. Goal, 2. Verified baseline, 3.1 Rejected alternatives (+12 more)

### Community 213 - "DirectoryClient"
Cohesion: 0.10
Nodes (20): DirectoryClient, accept_invite, cancel_join_request, create_invite, create_join_request, decide_join_request, list_members, list_messages (+12 more)

### Community 214 - ".start"
Cohesion: 0.18
Nodes (11): int64_t, ScreenShareErrorCode, string, ScreenShareError, code, message, native_code, optional (+3 more)

### Community 215 - "ShellState"
Cohesion: 0.19
Nodes (11): channel_kind(), ChannelKind, optional, string_view, AppDestination, ChannelKind, ShellState, open_diagnostics (+3 more)

### Community 216 - "RoomMeshTransport::Impl"
Cohesion: 0.12
Nodes (15): atomic, atomic_bool, Json, mutex, RoomMeshTransport::claim_screen(), RoomMeshTransport::Impl, callbacks_, config_ (+7 more)

### Community 217 - "jitter.cpp"
Cohesion: 0.13
Nodes (17): index_for, resynchronize, unwrap_near, int64_t, JitterPushResult, size_t, uint16_t, JitterBuffer::copy_payload() (+9 more)

### Community 218 - "VoicePipelineStatistics"
Cohesion: 0.10
Nodes (20): uint64_t, aggregate_jitter_statistics, VoicePipelineStatistics, capture, decode_errors, decoded_frames, encode_errors, encoded_frames (+12 more)

### Community 219 - "Cross-platform releases and one-command installers — design"
Cohesion: 0.10
Nodes (19): 10. Security and failure behavior, 11. Release readiness gates, 12. Performance claims, 13. Non-goals, 14. Follow-up milestone, 1. Goal, 2. Existing baseline, 3. User-visible contract (+11 more)

### Community 220 - "process_probe_executor.cpp"
Cohesion: 0.50
Nodes (4): command_line(), path, wstring, ProcessProbeExecutor::ProcessProbeExecutor()

### Community 221 - "Platform"
Cohesion: 0.14
Nodes (17): "audio-check arguments accept each option once", "audio-check maps failures to exit codes", "audio-check reports streams, progress, and latency", AudioEndpointId, OpenResult, optional, StreamFailure, string_view (+9 more)

### Community 222 - "pipeline_test.cpp"
Cohesion: 0.11
Nodes (19): "capture overflow creates a packet-clock gap instead of time-compressing speech", "capture to Opus packet path increments sequence and timestamp across wrap", PcmFrame, span, "decoded audio never grows memory and reports bounded render backpressure", "decoder rejects an empty Opus packet deterministically", encode(), finite_nonzero() (+11 more)

### Community 223 - "ServerView.Directory.cpp"
Cohesion: 0.16
Nodes (14): fire_and_forget, int64_t, string, wstring, message_time(), ServerView::AppendMessage(), ServerView::BeginInvite(), ServerView::BeginJoinRequestDecision() (+6 more)

### Community 224 - "VoicePipelineConfig"
Cohesion: 0.11
Nodes (18): array, byte, size_t, span, uint16_t, uint32_t, OutboundDatagram, bytes (+10 more)

### Community 225 - "udp_transport_test.cpp"
Cohesion: 0.22
Nodes (9): "actual UDP loopback carries Opus packets into the jitter decoder", connect_pair(), "connected UDP sockets exchange one bounded datagram on loopback", "connected UDP sockets send scatter gather segments as one datagram", PcmFrame, encode(), frame(), "oversized UDP datagrams are rejected instead of silently truncated" (+1 more)

### Community 226 - "HardwareEncoderError"
Cohesion: 0.25
Nodes (11): EncodedAccessUnit, HardwareEncoderErrorCode, HardwareEncoderError, code, native_code, name(), ID3D11Texture2D, optional (+3 more)

### Community 227 - "system_probe.cpp"
Cohesion: 0.22
Nodes (18): absent(), add_issue(), architecture(), build_architecture(), CpuArchitecture, IssueCode, optional, SimdFeature (+10 more)

### Community 228 - "RuntimeShape"
Cohesion: 0.25
Nodes (8): optional, PowerSource, ThermalPressure, RuntimeShape, battery, low_power, power, thermal

### Community 229 - "Activate"
Cohesion: 0.16
Nodes (18): string_view, MainWindow::ActivateDirectoryServer(), MainWindow::RefreshDirectoryRail(), AppDestination, IInspectable, RoutedEventArgs, UIElement, Activate (+10 more)

### Community 230 - "jitter_test.cpp"
Cohesion: 0.09
Nodes (22): int32_t, uint16_t, sequence_ahead(), sequence_distance(), "16-bit sequence arithmetic is wrap safe", "a missing packet uses the following packet for FEC without consuming it", byte, JitterPushResult (+14 more)

### Community 231 - "video_peer.cpp"
Cohesion: 0.13
Nodes (28): unique_ptr, WindowsH264D3D11Decoder, d3d_device, decode, impl_, running, start, statistics (+20 more)

### Community 232 - "VideoPeerOptions"
Cohesion: 0.11
Nodes (19): seconds, size_t, uint16_t, uint32_t, uint8_t, VideoPeerOptions, bind, bitrate (+11 more)

### Community 233 - "NetworkStatistics"
Cohesion: 0.12
Nodes (17): duration, uint64_t, NetworkStatistics, oversized_packets, peer_unreachable_events, received_bytes, received_packets, send_backpressure_drops (+9 more)

### Community 234 - "byte"
Cohesion: 0.31
Nodes (9): catro_room_runtime_receive_stream_audio(), catro_room_runtime_receive_video(), catro_room_runtime_receive_voice(), catro_room_runtime_send_video(), byte, ptrdiff_t, size_t, span (+1 more)

### Community 235 - "VoicePeerControl"
Cohesion: 0.11
Nodes (18): CatroRoomRuntimeHandle, string, run_room_voice(), VoiceRuntimeHost::set_error(), start, stop, atomic, atomic_bool (+10 more)

### Community 236 - "DirectoryMessage"
Cohesion: 0.12
Nodes (18): vector, ServerView::AppendMessages(), ServerView::ApplyMemberRoster(), AppendMessage, DirectoryMessage, author_display_name, author_id, channel_id (+10 more)

### Community 237 - "ActivationHandler"
Cohesion: 0.16
Nodes (16): ClassicCom, FtmBase, IActivateAudioInterfaceAsyncOperation, IActivateAudioInterfaceCompletionHandler, activate_process_loopback(), ActivationHandler, completed_, destination_ (+8 more)

### Community 238 - "H264RtpReassembler"
Cohesion: 0.09
Nodes (24): H264RtpReassembler, active_, begin_frame, clear_frame_state, config_, damage_error_, damaged_, expected_sequence_ (+16 more)

### Community 239 - "rtp_h264_test.cpp"
Cohesion: 0.12
Nodes (16): append, "100ns timestamps convert to the 90kHz RTP clock with integer math", byte, vector, "H264 Annex-B packetizer fragments large NALs without copying payloads", "H264 packetizer rejects bytes that are not Annex-B", "H264 packetizer still rejects wildcard SSRC", "H264 RTP reassembler can lock wildcard SSRC to the first stream" (+8 more)

### Community 240 - "Oracle Free Production Rooms — Design"
Cohesion: 0.11
Nodes (17): 10. Image and release rules, 11. Failure behavior, 12. Observability, 13. Testing, 14. Cost and scaling boundary, 15. Non-goals, 16. Definition of done, 1. Purpose (+9 more)

### Community 241 - "VideoReceiveSource"
Cohesion: 0.09
Nodes (19): array, byte, CatroRoomRuntimeHandle, microseconds, size_t, SizeResult, uint16_t, uint8_t (+11 more)

### Community 242 - "Peer"
Cohesion: 0.21
Nodes (10): RoomTransportErrorCode, shared_ptr, string_view, Peer, connection, id, stream_audio, video (+2 more)

### Community 243 - "PlayoutFrame"
Cohesion: 0.08
Nodes (26): array, byte, int64_t, JitterPushResult, PlayoutKind, span, string_view, uint16_t (+18 more)

### Community 244 - "JitterStatistics"
Cohesion: 0.12
Nodes (17): size_t, uint64_t, JitterStatistics, accepted, buffered, duplicates, fec, late (+9 more)

### Community 245 - "audio_bridge.cpp"
Cohesion: 0.18
Nodes (13): CaptureBridge::CaptureBridge(), CaptureBridge::on_captured(), CaptureBridge::statistics(), CaptureBridge::update_peak(), atomic, size_t, span, RenderBridge::on_render() (+5 more)

### Community 246 - "ScreenCaptureError"
Cohesion: 0.31
Nodes (8): CaptureSource, optional, ScreenCaptureBackend, ScreenCaptureError, recommended_capture_backend(), WindowsGraphicsCapture::start_primary_display(), WindowsGraphicsCapture::start_source(), ScreenCaptureConfig

### Community 247 - "WindowsH264D3D11Decoder::Impl"
Cohesion: 0.12
Nodes (16): IMFDXGIDeviceManager, IMFMediaBuffer, IMFSample, size_t, WindowsH264D3D11Decoder::Impl, apartment_initialized_, config_, device_ (+8 more)

### Community 248 - "macos/translation_test.cpp"
Cohesion: 0.12
Nodes (17): "a display's GPU is claimed only when an enumerated Metal device drives it", "audio endpoints keep one default per direction as inferred roles", "CoreGraphics refresh rates become exact rationals", IssueCode, T, vector, "device names are bounded on a UTF-8 boundary", discrete_gpu() (+9 more)

### Community 249 - "VoicePeerOptions"
Cohesion: 0.12
Nodes (17): AudioEndpointId, int32_t, optional, seconds, uint16_t, uint32_t, VoicePeerMode, VoicePeerOptions (+9 more)

### Community 250 - "CatroRoomRuntimeSnapshot"
Cohesion: 0.12
Nodes (16): CatroRoomRuntimeSnapshot, error, peer_count, screen_owner, state, stream_audio_queue_drops, stream_audio_received_datagrams, stream_audio_sent_datagrams (+8 more)

### Community 251 - "room_runtime.cpp"
Cohesion: 0.29
Nodes (14): catro_room_runtime_claim_screen(), catro_room_runtime_create(), catro_room_runtime_destroy(), catro_room_runtime_release_screen(), catro_room_runtime_send_stream_audio(), catro_room_runtime_send_voice(), catro_room_runtime_snapshot(), catro_room_runtime_start() (+6 more)

### Community 252 - "CaptureSource"
Cohesion: 0.14
Nodes (14): CaptureSource, fullscreen_like, height, kind, monitor_handle, native_handle, primary, process_id (+6 more)

### Community 253 - "geometry.hpp"
Cohesion: 0.33
Nodes (10): distance_u64(), fit_even_video_extent(), optional, uint32_t, uint64_t, min_u32(), nearest_even_ratio(), VideoExtent (+2 more)

### Community 254 - "H264DecoderError"
Cohesion: 0.25
Nodes (11): H264DecoderErrorCode, H264DecoderError, code, native_code, name(), byte, int64_t, optional (+3 more)

### Community 255 - "HardwareEncoderConfig"
Cohesion: 0.12
Nodes (18): IMFMediaType, HardwareEncoderConfig, adapter_luid, bitrate, frame_rate_denominator, frame_rate_numerator, gop_frames, height (+10 more)

### Community 256 - ".run"
Cohesion: 0.23
Nodes (10): mutex, promise, StreamAudioErrorCode, error_from_hresult(), make_event(), sink_, publish_error(), stereo_format() (+2 more)

### Community 257 - "parse"
Cohesion: 0.14
Nodes (13): "capture-check accepts seconds once and rejects ambiguous arguments", "capture-check defaults to a short bounded validation run", optional, string_view, vector, parse(), CaptureCheckOptions, duration (+5 more)

### Community 258 - "milliseconds"
Cohesion: 0.38
Nodes (11): add(), add_stream(), audio_choices(), audio_level(), AudioDirection, string, uint32_t, vector (+3 more)

### Community 259 - "AudioViewModel"
Cohesion: 0.21
Nodes (8): AudioView, .body, CatroAudioDevice, Color, AudioViewModel, String, Foundation, Timer

### Community 260 - "CatroRoomRuntimeConfig"
Cohesion: 0.13
Nodes (14): CatroRoomRuntimeConfig, access_token, allow_insecure_signaling, allow_no_turn, channel_id, ice_server_count, ice_server_urls, max_remote_peers (+6 more)

### Community 261 - "GpuCaptureFrame"
Cohesion: 0.15
Nodes (13): Direct3D11CaptureFrame, GpuCaptureFrame, format, height, lease, sequence, texture, width (+5 more)

### Community 262 - "CaptureSink"
Cohesion: 0.22
Nodes (13): CaptureSink, on_captured, RenderSource, on_render, optional, ExternalAudioSession::start(), Direction, AudioEndpointId (+5 more)

### Community 263 - "Button"
Cohesion: 0.13
Nodes (15): AccessButton, DeafenVoiceButton, FullScreenStreamButton, InviteButton, JoinVoiceButton, LeaveStreamButton, MuteVoiceButton, PopOutStreamButton (+7 more)

### Community 264 - "LocalQualityEnvelope"
Cohesion: 0.16
Nodes (13): Confidence, LocalQualityEnvelope, bit_depth, confidence, frame_rate, hdr, reasons, resolution (+5 more)

### Community 265 - "NativeDisplay"
Cohesion: 0.15
Nodes (13): uint8_t, NativeDisplay, adapter, bits_per_channel, dpi, hdr_enabled, hdr_supported, height (+5 more)

### Community 266 - "decode_local_state"
Cohesion: 0.27
Nodes (14): ChannelKind, CodecError, optional, ServerRole, string, string_view, variant, decode_local_state() (+6 more)

### Community 267 - "UdpPeerSocket"
Cohesion: 0.13
Nodes (17): unique_ptr, UdpPeerSocket, bind, connect_peer, impl_, local_port, receive, send (+9 more)

### Community 268 - "RenderBridgeStatistics"
Cohesion: 0.13
Nodes (15): RenderBridgeStatistics, buffered_samples, callbacks, deafened_samples_rendered, frames_enqueued, pcm_samples_rendered, peak_buffered_samples, push_rejections (+7 more)

### Community 269 - "write_packet_header"
Cohesion: 0.42
Nodes (14): byte, PacketError, size_t, span, uint16_t, uint32_t, variant, get_u16() (+6 more)

### Community 270 - "VoicePipeline::RemoteStream"
Cohesion: 0.09
Nodes (27): next_playout_kind, accumulate_jitter(), CodecError, optional, PcmFrame, PlayoutKind, RemoteStream, uint16_t (+19 more)

### Community 271 - "Review Focus"
Cohesion: 0.13
Nodes (14): Global Constraints, Review Focus, Task 10: Run fresh Oracle and real-machine acceptance, Task 11: Rehearse final distribution and freeze, Task 1: Make RTC and room runtime portable, Task 2: Add shared directory values and macOS production session, Task 3: Extract portable production voice runtime, Task 4: Add macOS capture, H.264 codec, and presentation primitives (+6 more)

### Community 272 - "InitializeComponent"
Cohesion: 0.12
Nodes (28): ServerView::BeginSendMessage(), ServerView::SetDirectorySession(), IInspectable, RoutedEventArgs, string_view, BeginInvite, BeginJoinRequestRefresh, BeginMemberRefresh (+20 more)

### Community 273 - "StreamInfo"
Cohesion: 0.15
Nodes (10): uint32_t, StreamInfo, device, device_channels, device_latency_frames, device_sample_rate, period_frames, uint64_t (+2 more)

### Community 274 - "RoomMeshTransport"
Cohesion: 0.14
Nodes (13): unique_ptr, RoomMeshTransport, claim_screen, impl_, peer_count, release_screen, screen_owner, send_stream_audio (+5 more)

### Community 275 - "StartingQuality"
Cohesion: 0.18
Nodes (11): Downgrade, profile, quality, trigger, uint8_t, StartingQuality, bit_depth, frame_rate (+3 more)

### Community 276 - "Native UI stabilization and modular product-shell design"
Cohesion: 0.14
Nodes (13): 10. Verification, 11. Non-goals, 12. Definition of done for the interruption, 1. Goal, 2. Verified baseline, 3. Sequencing, 4. Presentation boundary, 5. Windows organization (+5 more)

### Community 277 - "CachedInputView"
Cohesion: 0.25
Nodes (8): ID3D11VideoProcessorInputView, CachedInputView, subresource, texture, view, ComPtr, IDXGISwapChain1, D3D11CompositionVideoPresenter::swap_chain()

### Community 278 - "AudioNotifications"
Cohesion: 0.14
Nodes (13): EDataFlow, ERole, IMMNotificationClient, LPCWSTR, AudioNotifications, references_, STDMETHODCALLTYPE, window_ (+5 more)

### Community 279 - "DecodedGpuFrame"
Cohesion: 0.14
Nodes (14): IUnknown, DecodedGpuFrame, format, height, pts_100ns, sample_lease, subresource_index, texture (+6 more)

### Community 280 - "StreamAudioStatistics"
Cohesion: 0.16
Nodes (14): optional, uint64_t, StreamAudioStatistics, callbacks, error, frames, glitches, running (+6 more)

### Community 281 - "create_decoder_device"
Cohesion: 0.21
Nodes (14): ComPtr, HRESULT, ID3D11Device, IDXGIAdapter1, LUID, uint64_t, create_decoder_device(), DecoderDevice (+6 more)

### Community 282 - "EncodeCheckOptions"
Cohesion: 0.12
Nodes (17): optional, string_view, vector, "encode-check accepts bounded explicit video settings", "encode-check defaults to conservative same-adapter H264 validation", "encode-check rejects malformed duplicate or unsafe ranges", parse(), Integer (+9 more)

### Community 283 - "realtime_bridge_test.cpp"
Cohesion: 0.14
Nodes (13): "capture backlog trimming drops only complete oldest codec frames", "capture backlog trimming preserves partial native callback phase", "capture bridge assembles arbitrary callback blocks into exact codec frames", "capture bridge preserves frame order under sustained two-thread load", "capture overflow aligns the post-gap PCM to the 20 ms packet clock", "capture overload resynchronizes instead of splicing stale and fresh PCM", "real-time bridge memory is clamped to a small fixed ceiling", "render bridge deafen outputs silence while draining live PCM" (+5 more)

### Community 284 - "PacketSendContext"
Cohesion: 0.14
Nodes (13): uint64_t, PacketSendContext, fatal_error, socket, soft_drop, stats, SendStatistics, backpressure_events (+5 more)

### Community 285 - "Mode"
Cohesion: 0.15
Nodes (13): Mode, .bridged, .id, meter, monitor, .title, tone, .usesInput (+5 more)

### Community 286 - "WindowsScreenShareRuntime"
Cohesion: 0.15
Nodes (12): unique_ptr, WindowsScreenShareRuntime, impl_, preview_swap_chain, remote_swap_chain, set_local_preview_enabled, set_remote_viewing_enabled, snapshot (+4 more)

### Community 287 - "PacketContext"
Cohesion: 0.15
Nodes (13): PacketContext, fatal_error, owner, room_failed, room_runtime, socket, soft_drop, string_view (+5 more)

### Community 288 - "run_audio_check"
Cohesion: 0.31
Nodes (12): AudioCheckWait, optional, ostream, span, string, string_view, dbfs(), describe() (+4 more)

### Community 289 - "DirectoryError"
Cohesion: 0.16
Nodes (15): DirectoryError, code, http_status, message, native_code, DirectoryErrorCode, DirectoryErrorCode, Result (+7 more)

### Community 290 - "CaptureBridgeStatistics"
Cohesion: 0.15
Nodes (13): CaptureBridgeStatistics, buffered_samples, callbacks, captured_samples, dropped_callbacks, dropped_samples, frames_dequeued, peak_buffered_samples (+5 more)

### Community 291 - "verify.sh script"
Cohesion: 0.22
Nodes (9): backup.sh script, fail(), install.sh script, fail(), rollback(), update.sh script, fail(), verify.sh script (+1 more)

### Community 292 - "ParsedBaseUrl"
Cohesion: 0.15
Nodes (13): DirectoryConfigResult, INTERNET_PORT, channel_of_kind(), ChannelKind, optional, string, environment(), load_directory_service_config() (+5 more)

### Community 293 - "Bounded persistent text channels — design"
Cohesion: 0.15
Nodes (12): Architecture, Bounded persistent text channels — design, Failure behavior, GET /v1/messages, Goal, HTTP API, POST /v1/messages, Security and privacy (+4 more)

### Community 294 - "Oracle Free production acceptance record"
Cohesion: 0.15
Nodes (13): Build and deployment identity, Final disposition, Five-client voice capacity, Forced TURN, Network diversity, Oracle Free production acceptance record, Resource and quality measurements, Restart and recovery (+5 more)

### Community 295 - "joinTestRoomPeer"
Cohesion: 0.36
Nodes (13): github.com/gorilla/websocket.Conn, net/http/httptest.Server, service, claimTestScreen(), joinTestRoomPeer(), newTestRoomService(), readTestRoomMessage(), TestLateJoinReceivesCurrentScreenOwner() (+5 more)

### Community 296 - "Kind"
Cohesion: 0.50
Nodes (4): Kind, failure, information, success

### Community 297 - "activate_h264_d3d11_decoder"
Cohesion: 0.18
Nodes (12): activate_h264_d3d11_decoder(), activation_name(), ActivationArray, count, data, IMFActivate, IMFTransform, string (+4 more)

### Community 298 - "GpuShape"
Cohesion: 0.15
Nodes (13): GpuKind, ProbeFamily, uint64_t, GpuShape, dedicated_gib, high_performance, id, kind (+5 more)

### Community 299 - "voice-peer/main.cpp"
Cohesion: 0.21
Nodes (11): console_control_handler(), BOOL, DWORD, string, vector, wchar_t, wstring_view, main() (+3 more)

### Community 300 - "UpdateVoiceUi"
Cohesion: 0.27
Nodes (11): fire_and_forget, IInspectable, RoutedEventArgs, ServerView::BeginVoiceJoin(), ServerView::OnDeafenVoice(), ServerView::OnJoinVoice(), ServerView::OnMuteVoice(), ServerView::StopVoice() (+3 more)

### Community 301 - "LocalState"
Cohesion: 0.22
Nodes (10): LocalState, identity, personal_server, StateError, code, detail, bootstrap_personal_state(), variant (+2 more)

### Community 302 - "size_t"
Cohesion: 0.45
Nodes (7): byte, size_t, span, RoomMeshTransport::peer_count(), RoomMeshTransport::send_stream_audio(), RoomMeshTransport::send_video(), RoomMeshTransport::send_voice()

### Community 303 - "App"
Cohesion: 0.20
Nodes (6): App, App, OnLaunched, window_, AppT, LaunchActivatedEventArgs

### Community 304 - "Review Focus"
Cohesion: 0.17
Nodes (11): Global Constraints, Oracle Free Production Rooms Implementation Plan, Review Focus, Task 1: Bound Room Capacity and Provision It to Clients, Task 2: Add Authoritative Screen Ownership to Signaling, Task 3: Enforce Capacity and Screen Ownership in RTC and Room Runtime, Task 4: Wire Provisioning and Screen Claims into the Windows Client, Task 5: Add Bounded API Rate Limiting and Production Metrics (+3 more)

### Community 305 - "Audio Capture and Playback — Design"
Cohesion: 0.17
Nodes (11): 10. Definition of done, 1. Purpose, 2. Scope, 3. Canonical format, 4. Real-time rules, 5. Core components (`core/audio`), 6. Latency statistics, 7. Platform backends (+3 more)

### Community 306 - "load_or_create_credential_bytes"
Cohesion: 0.26
Nodes (12): kCredentialBytes, base64url(), array, byte, DirectoryStringResult, path, span, variant (+4 more)

### Community 307 - "CapabilityService"
Cohesion: 0.17
Nodes (10): CapabilityService, impl_, refresh, start, stop, path, unique_ptr, ProcessProbeExecutor (+2 more)

### Community 308 - ".start"
Cohesion: 0.27
Nodes (8): optional, thread, uint32_t, ProcessLoopbackAudioCapture::start(), WasapiStreamAudioRenderer::start(), span, Sink, Source

### Community 309 - "room_runtime_test.cpp"
Cohesion: 0.18
Nodes (9): CatroRoomRuntimeHandle, "room runtime C ABI rejects invalid arguments", "room runtime can restart after stop", "room runtime keeps insecure signaling and no-TURN opt-ins strict", "room runtime stop wakes blocked receivers and is idempotent", "room runtime validates required configuration and peer bounds", Runtime, handle_ (+1 more)

### Community 310 - "AudioSessionView"
Cohesion: 0.10
Nodes (20): AudioLevel, fraction, text, AudioSessionView, input, output, rows, running (+12 more)

### Community 311 - ".present"
Cohesion: 0.33
Nodes (7): D3D11_TEXTURE2D_DESC, ID3D11Device, ID3D11Texture2D, optional, uint32_t, D3D11CompositionVideoPresenter::present(), VideoPresenterError

### Community 312 - "14. Design 2/3 — Capability Schema and Policy"
Cohesion: 0.18
Nodes (11): 14. Design 2/3 — Capability Schema and Policy, Additional constraints added during approval, Deterministic rule order, Evidence and uncertainty model, Explainability, `MediaPlan`, Multiple devices, Policy interface (+3 more)

### Community 313 - "Oracle Free production operations"
Cohesion: 0.18
Nodes (11): 10. Production acceptance boundary, 1. Provisioning boundary, 2. Network Security Group ingress, 3. Host firewall, 4. DNS and first install, 5. Health and diagnostics, 6. Backup and restore, 7. Update and rollback (+3 more)

### Community 314 - "Two-Client Low-Latency Voice — Design"
Cohesion: 0.18
Nodes (10): 1. Purpose, 2. Non-negotiable properties, 3. Codec baseline, 4. Voice packet v1, 5. Core boundaries, 6. Jitter and loss behavior, 7. Development transport, 8. Test progression (+2 more)

### Community 315 - "Authenticated server member roster — design"
Cohesion: 0.18
Nodes (10): Authenticated server member roster — design, Existing gap, Goal, Performance, Persistent-state hardening, Security, Server API, Validation (+2 more)

### Community 316 - "DirectoryCancellationSource"
Cohesion: 0.25
Nodes (6): DirectoryCancellationSource, state_, DirectoryCancellationToken, state_, atomic_bool, shared_ptr

### Community 317 - "VideoPresenterStatistics"
Cohesion: 0.18
Nodes (11): DXGI_FORMAT, uint64_t, VideoPresenterStatistics, frames_dropped, frames_presented, output_height, output_width, reconfigurations (+3 more)

### Community 318 - "runtime_probe.cpp"
Cohesion: 0.38
Nodes (10): absent(), add_issue(), IssueCode, string_view, T, vector, known(), measured() (+2 more)

### Community 319 - "video_decoder.cpp"
Cohesion: 0.25
Nodes (8): GUID, ICodecAPI, uint32_t, set_codec_bool(), set_codec_uint32(), WindowsH264D3D11Decoder::statistics(), WindowsH264D3D11Decoder::stop(), WindowsH264D3D11Decoder::WindowsH264D3D11Decoder()

### Community 320 - "Datagram"
Cohesion: 0.27
Nodes (6): array, string_view, Datagram, bytes, size, kMaxDatagramBytes

### Community 321 - "RoomTransportCallbacks"
Cohesion: 0.32
Nodes (7): function, RoomTransportCallbacks, on_error, on_stream_audio_datagram, on_video_datagram, on_voice_datagram, DataChannel

### Community 322 - "Review Focus"
Cohesion: 0.20
Nodes (9): Global Constraints, Native UI Stabilization Implementation Plan, Review Focus, Task 1: Add shared presentation state, Task 2: Pin Windows interaction and organization policy, Task 3: Stabilize MainWindow states and split directory behavior, Task 4: Split ServerView and apply explicit action states, Task 5: Validate Windows interaction and visuals (+1 more)

### Community 323 - "DirectoryHttpRequest"
Cohesion: 0.22
Nodes (9): DirectoryHttpRequest, access_token, body, endpoint, method, DirectoryHttpResponse, body, status (+1 more)

### Community 324 - "LocalStateError"
Cohesion: 0.20
Nodes (9): LocalStateErrorCode, string, uint32_t, LocalStateError, code, detail, native_code, SystemEntropy (+1 more)

### Community 325 - "EncodedAccessUnit"
Cohesion: 0.20
Nodes (10): EncodedAccessUnit, bytes, duration_100ns, keyframe, pts_100ns, sequence, byte, int64_t (+2 more)

### Community 326 - "VideoPresenterConfig"
Cohesion: 0.17
Nodes (11): int64_t, uint32_t, VideoPresenterErrorCode, name(), VideoPresenterConfig, frame_rate, max_height, max_width (+3 more)

### Community 327 - "ProcessLoopbackAudioCapture::Impl"
Cohesion: 0.20
Nodes (9): ProcessLoopbackAudioCapture::Impl, callbacks_, error_, error_mutex_, frames_, glitches_, running_, stop_event_ (+1 more)

### Community 328 - "MainWindow.Directory.cpp"
Cohesion: 0.26
Nodes (12): fire_and_forget, string, MainWindow::BeginDirectoryBootstrap(), MainWindow::BeginDirectoryServerRefresh(), MainWindow::BeginInviteJoin(), MainWindow::BeginJoinServer(), MainWindow::BeginOutgoingJoinRequestRefresh(), MainWindow::BeginServerCodeLookup() (+4 more)

### Community 329 - "audio-check/main.cpp"
Cohesion: 0.33
Nodes (8): string, vector, wchar_t, wstring_view, main(), run(), utf8(), wmain()

### Community 330 - "AudioPlatform"
Cohesion: 0.08
Nodes (20): AudioPlatform, open_capture, open_render, AudioEngine::AudioEngine(), FailureHandler, FailureHandler, uint64_t, unique_ptr (+12 more)

### Community 331 - "Border"
Cohesion: 0.22
Nodes (9): LocalShareViewport, MembersPane, RemoteShareViewport, RemoteStreamInvite, TextSelection, VoiceControlsBar, VoiceRoleBadge, VoiceSelection (+1 more)

### Community 332 - "SettingsView"
Cohesion: 0.25
Nodes (5): SettingsView, UserControl, SettingsView, InitializeComponent, SettingsViewT

### Community 333 - "InviteCode"
Cohesion: 0.14
Nodes (13): byte, ServerId, Id, bytes, Invite, code, creator_id, server_id (+5 more)

### Community 334 - "Decoder"
Cohesion: 0.10
Nodes (19): Decoder, conceal, create, decode, impl_, reset, Encoder, create (+11 more)

### Community 335 - "VideoEncoderConfig"
Cohesion: 0.20
Nodes (10): size_t, uint32_t, VideoEncoderConfig, bitrate, frame_rate, gop_frames, height, max_access_unit_bytes (+2 more)

### Community 336 - "RoomMeshConfig"
Cohesion: 0.17
Nodes (12): size_t, vector, RoomMeshConfig, access_token, allow_insecure_signaling, allow_no_turn, channel_id, ice_server_urls (+4 more)

### Community 337 - "29. Additional Engineering Principles Added During Review"
Cohesion: 0.22
Nodes (9): 29. Additional Engineering Principles Added During Review, ABI strategy, Dependency discipline, JSON is not the domain model, Objective-C++ bridge, Passive discovery, Platform support, Policy regression discipline (+1 more)

### Community 338 - "Strong domain objects"
Cohesion: 0.22
Nodes (9): `AudioEndpointCapability`, `CapturePathCapability`, `CpuCapability`, `DisplayCapability`, `EncoderCapability`, `GpuCapability`, `MemoryCapability`, `RuntimeState` (+1 more)

### Community 339 - "Voice + Stream Interaction Reference"
Cohesion: 0.22
Nodes (8): Broadcaster contract, Catro implementation state after this iteration, Natural-size / aspect-ratio rule, Official references, Viewer contract, Voice-room contract, Voice + Stream Interaction Reference, Windows capture guidance

### Community 340 - "Server Code discovery and approval-based join requests — implementation plan"
Cohesion: 0.22
Nodes (8): Server Code discovery and approval-based join requests — implementation plan, Task 1 — Public Server Code and persisted request model, Task 2 — Lookup and join-request API, Task 3 — Windows network boundary, Task 4 — Add Server flow, Task 5 — Owner Access surface, Task 6 — Documentation and acceptance, Verification

### Community 341 - "CatroCapabilitiesBridge"
Cohesion: 0.22
Nodes (8): CatroCapabilitiesBridge, -audioDevicesForInput, -exportReportWithFormat, -init, -initWithProbeURLNS_DESIGNATED_INITIALIZER, -refresh, -startWithHandler, -stop

### Community 342 - "string_view"
Cohesion: 0.33
Nodes (10): absent(), add_issue(), IssueCode, string_view, T, vector, inferred(), known() (+2 more)

### Community 343 - "Differ"
Cohesion: 0.44
Nodes (4): AudioEndpointId, ChangeDomain, Differ, changes_

### Community 344 - "WasapiStreamAudioRenderer::Impl"
Cohesion: 0.22
Nodes (9): WasapiStreamAudioRenderer::Impl, callbacks_, error_, error_mutex_, frames_, glitches_, running_, stop_event_ (+1 more)

### Community 345 - "SequenceEntropy"
Cohesion: 0.28
Nodes (6): byte, span, uint8_t, FailingEntropy, SequenceEntropy, next_

### Community 346 - "voice_test.cpp"
Cohesion: 0.22
Nodes (8): "opus audio codec preserves stereo 20 ms frames", "opus voice codec encodes decodes and conceals fixed 20 ms frames", "opus wrapper rejects unsupported channel counts", "opus wrapper validates frame and output sizes", "voice packet format rejects the reserved zero stream id", "voice packet header can be written in place without copying encoded payload", "voice packet parser rejects malformed datagrams", "voice packet v1 round trips in network byte order"

### Community 347 - "SequenceEntropy"
Cohesion: 0.28
Nodes (6): byte, span, uint8_t, FailingEntropy, SequenceEntropy, next_

### Community 348 - "video-peer/main.cpp"
Cohesion: 0.25
Nodes (8): console_control_handler(), BOOL, DWORD, string, wchar_t, wstring_view, utf8(), wmain()

### Community 349 - "VoicePacketView"
Cohesion: 0.22
Nodes (9): byte, span, uint16_t, uint32_t, VoicePacketView, payload, sequence, stream_id (+1 more)

### Community 350 - "CodecError"
Cohesion: 0.33
Nodes (6): CodecError, code, native_code, CodecErrorCode, string_view, name()

### Community 351 - "13. Design 1/3 — Foundation Boundaries"
Cohesion: 0.25
Nodes (8): 13. Design 1/3 — Foundation Boundaries, Additional constraints added during approval, Approximate repository boundary, Capability subsystem responsibilities, Core boundary, Explicit uncertainty, Policy style, Snapshot consumption rule

### Community 352 - "9. High-Level Media Research Conclusions"
Cohesion: 0.25
Nodes (8): 9. High-Level Media Research Conclusions, Audio codec / RTC direction, Desired media pipeline principle, Hardware video encoding, macOS audio / capture, Screen/game capture, SFU direction, Windows audio

### Community 353 - "Windows H.264 hardware encode slice"
Cohesion: 0.25
Nodes (7): Acceptance gate, Adapter affinity, Conversion, Data path, Diagnostic, Encoder behavior, Windows H.264 hardware encode slice

### Community 354 - "Review Focus"
Cohesion: 0.25
Nodes (7): Cross-platform Releases and One-command Installers Implementation Plan, Global Constraints, Review Focus, Task 1: Transactional Windows installer, Task 2: Native macOS preview package and transactional installer, Task 3: Release contract validator and draft-first workflow, Task 4: Download documentation and non-publishing release rehearsal

### Community 355 - "video_presenter.cpp"
Cohesion: 0.29
Nodes (6): DXGI_FORMAT, D3D11CompositionVideoPresenter::D3D11CompositionVideoPresenter(), D3D11CompositionVideoPresenter::reset(), D3D11CompositionVideoPresenter::statistics(), supported_input_format(), VideoPresenterStatistics

### Community 356 - "diagnostics_test.cpp"
Cohesion: 0.25
Nodes (7): "an oversized section is bounded on screen and complete in the export", "changes are listed only after the first publication", "device names are redacted on screen and in the text export, never in JSON", "probe health is sorted with locale-independent durations", report_for(), "sections come in a fixed order, plan first", "the headline follows the plan and probe outcomes"

### Community 357 - "directory_test.cpp"
Cohesion: 0.25
Nodes (7): "directory HTTP errors keep actionable server status and messages", "directory invite token and RTC payloads are strictly bounded", "directory lookup and join requests enforce codes roles bounds and uniqueness", "directory member rosters require one first owner and unique bounded members", "directory messages enforce content ordering identity and cursor bounds", "directory server payloads enforce every field and list uniqueness", "production directory URLs reject secret-bearing and local endpoints"

### Community 358 - "MacH264HardwareDecoder"
Cohesion: 0.22
Nodes (8): unique_ptr, MacH264HardwareDecoder, decode, impl_, running, start, statistics, stop

### Community 359 - "macos-installer-test.sh"
Cohesion: 0.50
Nodes (6): assert_eq(), assert_file(), assert_installed_version(), make_release(), run_installer(), macos-installer-test.sh script

### Community 360 - "MacH264HardwareEncoder"
Cohesion: 0.22
Nodes (8): unique_ptr, MacH264HardwareEncoder, encode, impl_, running, start, statistics, stop

### Community 361 - "rtc_room_transport_test.cpp"
Cohesion: 0.22
Nodes (7): string_view, screen_media_allowed(), "RTC room screen claim requires a joined signaling socket", "RTC room screen media accepts only the current owner", "RTC room transport bounds remote peers to four", "RTC room transport rejects insecure production configuration", "RTC room transport requires TURN unless explicitly in engineering mode"

### Community 362 - "ServerView::BeginScreenShare"
Cohesion: 0.29
Nodes (8): capture_source_label(), chromium_window(), CaptureSource, fire_and_forget, wstring, ServerView::BeginScreenShare(), ClaimScreenOwnership, LocalStreamId

### Community 363 - "audio_platform_test.cpp"
Cohesion: 0.25
Nodes (7): "a monitor session runs capture into render", optional, "process loopback stream audio rejects a zero process id", require_or_skip(), "the default capture endpoint delivers frames", "the default render endpoint plays the test tone", "unknown and wrong-direction endpoints are not found"

### Community 364 - "CatroDirectorySessionDelegate"
Cohesion: 0.50
Nodes (4): NSURLSessionTaskDelegate, CatroDirectorySessionDelegate, -URLSessiontaskwillPerformHTTPRedirectionnewRequestcompletionHandler, NSObject

### Community 365 - "Oracle Free production bundle"
Cohesion: 0.29
Nodes (6): First install, Host prerequisites, Operations, Oracle Free production bundle, Security boundary, Topology

### Community 366 - "Windows screen capture slice"
Cohesion: 0.29
Nodes (6): Backpressure, GPU affinity, Hardware validation, Hot path, Resize and source lifetime, Windows screen capture slice

### Community 367 - "Building"
Cohesion: 0.29
Nodes (7): Audio check, Building, macOS, Probe budgets, Report tool, Voice peer, Windows

### Community 368 - "Native UI foundation"
Cohesion: 0.29
Nodes (6): Integration boundaries, Native UI foundation, Performance invariants, Product contract, Visual system, Windows layout

### Community 369 - "Native UI stabilization validation"
Cohesion: 0.25
Nodes (7): Build and automated evidence, External UI Automation blocker, Final review fixes, Interaction and visual gate, Native UI stabilization validation, Release status, Scope

### Community 370 - "Voice channel runtime"
Cohesion: 0.29
Nodes (6): Architecture, Controls, Next media slice, Real-machine validation, Temporary direct-peer validation, Voice channel runtime

### Community 371 - "LocalStateError"
Cohesion: 0.29
Nodes (7): int32_t, LocalStateErrorCode, string, LocalStateError, code, detail, native_code

### Community 372 - "arguments"
Cohesion: 0.40
Nodes (6): arguments(), string, string_view, uint32_t, vector, views()

### Community 373 - "parse_video_peer_arguments"
Cohesion: 0.48
Nodes (7): Integer, optional, span, string_view, parse_endpoint(), parse_integer(), parse_video_peer_arguments()

### Community 374 - "ComPtr"
Cohesion: 0.67
Nodes (4): ComPtr, IDXGISwapChain1, WindowsScreenShareRuntime::preview_swap_chain(), WindowsScreenShareRuntime::remote_swap_chain()

### Community 375 - "Grid"
Cohesion: 0.33
Nodes (6): RemoteShareHost, ServerLayout, SharePreviewHost, TextPanel, VoicePanel, Grid

### Community 376 - "CodecError"
Cohesion: 0.33
Nodes (5): CodecError, code, detail, CodecErrorCode, string

### Community 377 - "H264PacketizeResult"
Cohesion: 0.29
Nodes (7): H264PacketizeResult, error, keyframe, next_sequence, packet_count, uint16_t, H264PacketizeError

### Community 378 - "Capability system"
Cohesion: 0.33
Nodes (6): Boundaries, Capability system, Data flow, Evidence, Refresh, Versioning

### Community 379 - "Personal identity and server state"
Cohesion: 0.33
Nodes (5): Identity, Persistence, Personal identity and server state, Personal server invariant, UI boundary

### Community 380 - "Two-Client Low-Latency Voice — Implementation Plan"
Cohesion: 0.33
Nodes (5): Final performance and recovery hardening, Slice 1 exact scope, Slice 3 implementation invariants, Slices 4-5 implementation invariants, Two-Client Low-Latency Voice — Implementation Plan

### Community 381 - "Bounded persistent text channels — implementation plan"
Cohesion: 0.33
Nodes (5): Bounded persistent text channels — implementation plan, Task 1 — Server schema and message API, Task 2 — Windows directory client, Task 3 — Native Windows timeline and composer, Task 4 — Documentation and verification

### Community 382 - "Authenticated server member roster — implementation plan"
Cohesion: 0.33
Nodes (5): Authenticated server member roster — implementation plan, Task 1 — Directory membership integrity and roster API, Task 2 — Windows directory client, Task 3 — Native member rail, Task 4 — Documentation and verification

### Community 383 - "Voice peer validation"
Cohesion: 0.33
Nodes (6): LAN test, Reading the counters, Regression gate, Voice peer validation, Windows same-machine full duplex, Windows same-machine one-way test

### Community 384 - "H264DecoderConfig"
Cohesion: 0.33
Nodes (6): H264DecoderConfig, adapter_luid, max_access_unit_bytes, optional, size_t, uint64_t

### Community 385 - "H264RtpConfig"
Cohesion: 0.38
Nodes (7): H264RtpConfig, mtu_bytes, payload_type, ssrc, valid_packetizer_config(), valid_reassembler_config(), valid_transport_shape()

### Community 386 - "HandleCloser"
Cohesion: 0.20
Nodes (3): HandleCloser, InternetHandleCloser, LocalMemoryCloser

### Community 387 - "process_loopback_audio.cpp"
Cohesion: 0.53
Nodes (4): ProcessLoopbackAudioCapture::
    ProcessLoopbackAudioCapture(), ProcessLoopbackAudioCapture::stop(), WasapiStreamAudioRenderer::stop(), WasapiStreamAudioRenderer::
    WasapiStreamAudioRenderer()

### Community 388 - "Catro"
Cohesion: 0.33
Nodes (6): Catro, Layout, More, Platform status, Quick start, Windows product shell

### Community 389 - "Catro signaling and server directory"
Cohesion: 0.33
Nodes (5): Catro signaling and server directory, Client flow, Engineering token minting, Production requirements, Windows portable package

### Community 390 - "ProbeNames"
Cohesion: 0.33
Nodes (6): ProbeNames, audio, encoders, gpu_display, runtime, system

### Community 391 - "windows-installer-test.ps1"
Cohesion: 0.47
Nodes (3): Assert-Equal(), Assert-InstalledVersion(), Assert-True()

### Community 392 - "CaptureBridge::trim_backlog"
Cohesion: 0.33
Nodes (7): finish_alignment_discard, resynchronize_if_needed, CaptureBridge::trim_backlog(), CaptureBridge::try_pop(), PcmFrame, uint64_t, RenderBridge::try_push()

### Community 393 - "Contributing"
Cohesion: 0.40
Nodes (5): Before a change, Checks, Commits, Contributing, Rules the codebase depends on

### Community 394 - "CatroScreenStreamOutput"
Cohesion: 0.29
Nodes (6): CatroScreenStreamOutput, -initWithFrameHandlerstopHandler, -streamdidOutputSampleBufferofType, NSObject, SCStreamDelegate, SCStreamOutput

### Community 395 - "string"
Cohesion: 0.50
Nodes (3): on_screen_owner, string, RoomMeshTransport::screen_owner()

### Community 396 - "text.hpp"
Cohesion: 0.29
Nodes (6): string, string_view, wstring, wstring_view, utf8(), wide()

### Community 397 - "H.264 RTP packetization slice"
Cohesion: 0.40
Nodes (4): H.264 RTP packetization slice, Hot-path resource rules, Loss behavior, Why RTP / RFC 6184

### Community 398 - "Two-client H.264 screen streaming"
Cohesion: 0.40
Nodes (4): Decoder latency control, Gate A — compressed transport, Gate B — decode and presentation, Two-client H.264 screen streaming

### Community 399 - "HWND"
Cohesion: 0.29
Nodes (3): CapabilityService::refresh(), HWND, RefreshReason

### Community 400 - "presentation_state_test.cpp"
Cohesion: 0.40
Nodes (4): "busy action rejects duplicates and can recover", "connecting and synchronized snapshots expose deterministic actions", "failed snapshot preserves local navigation and explains recovery", "workspace starts local-only with actionable reasons"

### Community 401 - "TempStatePath"
Cohesion: 0.40
Nodes (4): path, TempStatePath, directory, state

### Community 402 - "ScreenCaptureError"
Cohesion: 0.33
Nodes (6): int64_t, ScreenCaptureErrorCode, name(), ScreenCaptureError, code, native_code

### Community 403 - "process_probe_executor_test.cpp"
Cohesion: 0.33
Nodes (5): "a timed-out helper and its descendants are terminated by the job", DWORD, optional, path, read_pid()

### Community 404 - "windows/local_state_test.cpp"
Cohesion: 0.18
Nodes (9): "concurrent first-run opens converge on one persisted identity", path, TempStatePath, directory, state, "Windows atomic state save replaces a valid snapshot without changing identifiers", "Windows local state is created once and survives entropy becoming unavailable", "Windows local state never silently replaces corrupt identity data" (+1 more)

### Community 405 - "screen_capture_test.cpp"
Cohesion: 0.40
Nodes (4): "capture source descriptors are inert value objects", "CS2 and fullscreen window sources use the game-compatible capture backend", "stopping an idle Windows screen capture is idempotent", "Windows screen capture starts with no hidden GPU or capture work"

### Community 406 - "screen_runtime_test.cpp"
Cohesion: 0.40
Nodes (4): "screen transport equality includes bounded media parameters", "Windows screen media runtime is inert until explicitly started", "Windows screen media runtime rejects an empty listener configuration", "Windows screen media runtime rejects an empty share configuration"

### Community 407 - "capability-report/main.cpp"
Cohesion: 0.27
Nodes (9): collect_report(), optional, path, string, wchar_t, wstring_view, executable_directory(), utf8() (+1 more)

### Community 408 - ".detail"
Cohesion: 0.50
Nodes (3): .detail, Bool, CatroAudioDevice

### Community 409 - "NavigationEntry"
Cohesion: 0.50
Nodes (4): string_view, NavigationEntry, glyph, id

### Community 410 - "StackPanel"
Cohesion: 0.50
Nodes (4): TextEmptyState, VoiceIdentityPanel, VoiceToolbar, StackPanel

### Community 411 - "system_runtime_probe_test.cpp"
Cohesion: 0.33
Nodes (5): runtime_spec(), system_spec(), "the Windows capability service publishes generations and stops cleanly", "the Windows runtime probe reports power and session facts without inventing state", "the Windows system probe reports measured topology and explicit uncertainty"

### Community 412 - "16. Probe Behavior"
Cohesion: 0.50
Nodes (4): 16. Probe Behavior, macOS probe families, Passive probing requirement, Windows probe families

### Community 413 - "3. Business/Hosting Direction"
Cohesion: 0.50
Nodes (4): 3. Business/Hosting Direction, Future optional direction, Initial backend deployment target, Primary deployment model

### Community 414 - "Troubleshooting"
Cohesion: 0.50
Nodes (4): Build, Collecting a report for a bug, Runtime, Troubleshooting

### Community 415 - "frame_period"
Cohesion: 0.40
Nodes (5): nanoseconds, time_point, uint32_t, frame_period(), monotonic_rtp_timestamp()

### Community 416 - "shell_model_test.cpp"
Cohesion: 0.50
Nodes (3): "first-run server has exactly one text and one voice channel", "shell defaults come from the community personal-server contract", "shell opens personal server and switches channels without invalid state"

### Community 417 - "ScreenCaptureNativeAdapter"
Cohesion: 0.40
Nodes (4): ScreenCaptureNativeAdapter, enumerate_sources, start, stop

### Community 418 - "windows_keyboard_smoke.ps1"
Cohesion: 0.83
Nodes (3): Assert-CatroAlive(), Save-CatroScreenshot(), Send-CatroKey()

### Community 419 - "video_peer_test.cpp"
Cohesion: 0.40
Nodes (4): "video peer parser accepts a bounded sender configuration", "video peer parser keeps a nonzero sender default and receiver auto-lock", "video peer parser keeps conservative media defaults", "video peer parser rejects unsafe or incomplete settings"

### Community 420 - "geometry_test.cpp"
Cohesion: 0.40
Nodes (4): "even video fit handles coprime odd source dimensions without collapsing scale", "even video fit never upscales a smaller source", "even video fit preserves common display ratios at the largest bounded scale", "even video fit rejects dimensions that cannot form an NV12 frame"

### Community 421 - "Security"
Cohesion: 0.50
Nodes (4): Privacy, Reporting a vulnerability, Security, Trust boundaries

### Community 422 - "ChannelHeaderRow"
Cohesion: 0.67
Nodes (3): ChannelHeaderRow, VoiceControlsRow, RowDefinition

### Community 423 - "ChannelsColumn"
Cohesion: 0.67
Nodes (3): ChannelsColumn, MembersColumn, ColumnDefinition

### Community 424 - "LocalShareSwapChainPanel"
Cohesion: 0.67
Nodes (3): LocalShareSwapChainPanel, RemoteShareSwapChainPanel, SwapChainPanel

### Community 425 - "MemberList"
Cohesion: 0.67
Nodes (3): MemberList, MessageList, ListView

### Community 426 - "ServerOwnerIcon"
Cohesion: 0.67
Nodes (3): ServerOwnerIcon, VoiceChannelGlyph, FontIcon

### Community 427 - "EnumNames<caps::ProfileRule>"
Cohesion: 0.67
Nodes (3): EnumNames<caps::ProfileRule>, last, names

### Community 428 - "video_decoder_test.cpp"
Cohesion: 0.50
Nodes (3): "Windows H264 decoder is inert until started", "Windows H264 decoder rejects input before startup", "Windows H264 decoder rejects invalid configuration before device creation"

### Community 430 - "21. Diagnostics Workspace"
Cohesion: 0.67
Nodes (3): 21. Diagnostics Workspace, macOS diagnostics UI, Windows diagnostics UI

### Community 431 - "5. Non-Negotiable Native Requirement"
Cohesion: 0.67
Nodes (3): 5. Non-Negotiable Native Requirement, Explicitly rejected as the primary architecture, Required direction

### Community 432 - "8. Distribution Requirements"
Cohesion: 0.67
Nodes (3): 8. Distribution Requirements, macOS, Windows

### Community 433 - "StreamAudioError"
Cohesion: 0.29
Nodes (7): int64_t, StreamAudioErrorCode, string_view, name(), StreamAudioError, code, native_code

### Community 434 - "SystemEntropy::fill"
Cohesion: 0.67
Nodes (3): byte, span, SystemEntropy::fill()

### Community 436 - "name"
Cohesion: 0.67
Nodes (3): PacketError, string_view, name()

## Knowledge Gaps
- **2871 isolated node(s):** `id`, `label`, `fraction`, `text`, `status` (+2866 more)
  These have ≤1 connection - possible missing edges or undocumented components. (Counts symbols only; 3860 node(s) total have ≤1 connection when file, concept and rationale nodes are included.)
- **40 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `ServerView` connect `ServerView` to `Window`, `ServerView.Screen.cpp`, `string`, `ServerView::BeginScreenShare`, `directory_client.cpp`, `DirectoryMessage`, `UpdateVoiceUi`, `DiagnosticsViewModel`, `WorkspaceSnapshot`, `InitializeComponent`, `DirectoryServer`, `LocalState`, `ShellState`, `WindowsScreenShareRuntime`, `ServerView.Directory.cpp`?**
  _High betweenness centrality (0.047) - this node is a cross-community bridge._
- **Why does `WindowsH264HardwareEncoder::Impl` connect `WindowsH264HardwareEncoder::Impl` to `video_encoder.cpp`, `HardwareEncoderStatistics`, `HardwareEncoderError`, `HardwareEncoderConfig`?**
  _High betweenness centrality (0.026) - this node is a cross-community bridge._
- **What connects `id`, `label`, `fraction` to the rest of the system?**
  _2871 weakly-connected nodes found - possible documentation gaps or missing edges._
- **Should `capability_fixtures.cpp` be split into smaller, more focused modules?**
  _Cohesion score 0.1187214611872146 - nodes in this community are weakly interconnected._
- **Should `canonical_determinism_test.cpp` be split into smaller, more focused modules?**
  _Cohesion score 0.10582010582010581 - nodes in this community are weakly interconnected._
- **Should `Validator` be split into smaller, more focused modules?**
  _Cohesion score 0.07660455486542443 - nodes in this community are weakly interconnected._
- **Should `schema.hpp` be split into smaller, more focused modules?**
  _Cohesion score 0.028985507246376812 - nodes in this community are weakly interconnected._