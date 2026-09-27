# Audio Capture and Playback — Design

Status: approved by the user on 2026-09-27 (milestone 3 of the product sequence in `docs/AGENTS.md` §39).

## 1. Purpose

Prove the real-time audio path Catro's voice features will sit on: capture from a chosen microphone, carry
frames across threads without locks or allocation, and render to a chosen output, with honest latency and
glitch statistics. The milestone ends with a local microphone test (meter and live monitor) and an output
test tone in both native shells and a headless check tool.

Voice encoding (Opus), echo cancellation and noise suppression (WebRTC APM), networking, and application or
system audio capture are later milestones. Nothing here may block them: the canonical frame format, the
real-time rules, and the backend contract are chosen for a voice pipeline.

## 2. Scope

In scope:

- `core/audio`: portable real-time primitives and the audio engine (no platform types).
- Windows backend: WASAPI shared-mode, event-driven capture and render streams on MMCSS "Pro Audio" threads.
- macOS backend: AUHAL (`kAudioUnitSubType_HALOutput`) input and output units.
- Device selection by the capability system's `AudioEndpointId`; the pickers list the snapshot's endpoints.
- Device loss and default-device changes surfaced as engine events.
- Microphone permission handled as an explicit engine error; macOS requests access only on a user action.
- `catro-audio-check` CLI; Audio page in both diagnostics shells.

Out of scope: codecs, AEC/NS/AGC, networking, loopback/process audio capture, exclusive mode, IAudioClient3
low-latency periods (recorded as an upgrade path), recording to files.

## 3. Canonical format

Engine frames are 48 kHz, 32-bit float, mono, interleaving irrelevant. Voice is mono and Opus prefers 48 kHz.
The operating system converts at the device boundary: WASAPI shared mode with `AUTOCONVERTPCM |
SRC_DEFAULT_QUALITY`, and the AUHAL client-side stream format on macOS. Catro ships no resampler.

## 4. Real-time rules

Callbacks that run on a platform audio thread (`CaptureSink::on_captured`, `RenderSource::on_render`) must be
`noexcept`, must not allocate, lock, log, or call into the OS beyond the stream API, and must finish in a
bounded time. Everything they touch is pre-allocated before the stream starts. Cross-thread state is atomics
or the single-producer single-consumer ring.

## 5. Core components (`core/audio`)

- `SpscRing<T>`: fixed-capacity, wait-free single-producer single-consumer ring (power-of-two capacity,
  acquire/release indices, no allocation after construction).
- `LevelMeter`: per-block peak and RMS, published as relaxed atomics; readers convert to dBFS.
- `ToneGenerator`: phase-continuous sine at a fixed frequency and level for the output test.
- `MonitorPipe`: capture-to-render bridge over `SpscRing<float>`. Render waits (outputs silence) until the
  ring holds the target fill, then plays; an empty ring outputs silence and counts an underrun; a ring above
  the high-water mark drops the oldest samples down to target and counts a drift correction (the two device
  clocks are independent). Capture into a full ring counts an overrun.
- `AudioEngine`: owns one session at a time. A session is a configuration `{input, output, mode}` with modes
  `meter` (capture only), `tone` (render only), `monitor` (capture to render). The engine opens the streams
  through `AudioPlatform`, wires sinks and sources, exposes `AudioStatistics` (atomic counters and meters)
  and a state `idle | running | failed`. A stream failure (device lost, access revoked) stops the session and
  reports the error through a callback delivered off the audio thread.
- `AudioPlatform` (interface implemented per platform): `open_capture` and `open_render` take an optional
  `AudioEndpointId` (absent means the current system default for communications), the sink or source, and a
  failure callback, and return either a started `AudioStream` handle with its `StreamInfo` (device id, native
  sample rate and channels, period frames, device latency frames) or an `AudioError`.

`AudioError` codes: `device_not_found`, `device_in_use`, `permission_denied`, `format_unsupported`,
`device_lost`, `os_failure`, each with an optional native code.

## 6. Latency statistics

The engine reports an **estimated** monitor latency: capture device latency + capture period + monitor target
fill + render period + render device latency. It is labelled estimated everywhere; a measured round trip needs
an acoustic or cable loopback and is not claimed.

## 7. Platform backends

Windows:

- `IMMDeviceEnumerator::GetDevice` for `mmdevice:<id>`; default = `eCommunications` endpoint.
- `IAudioClient::Initialize(SHARED, EVENTCALLBACK | AUTOCONVERTPCM | SRC_DEFAULT_QUALITY, default period)`
  with a 48 kHz float mono `WAVEFORMATEXTENSIBLE`.
- One thread per stream, `AvSetMmThreadCharacteristicsW(L"Pro Audio")`, waiting on the buffer event and a stop
  event. `AUDCLNT_E_DEVICE_INVALIDATED` ends the stream with `device_lost`. `E_ACCESSDENIED` on activation is
  `permission_denied` (Windows microphone privacy setting).
- Capture honours `AUDCLNT_BUFFERFLAGS_SILENT` (zeros) and counts `DATA_DISCONTINUITY` as a glitch.
- A session opened on the default endpoint stays on that endpoint. Following default-device changes
  (`IMMNotificationClient`) is deferred; a removed endpoint surfaces as `device_lost` and the user restarts.

macOS:

- `coreaudio:<uid>:input|output` translated with `kAudioHardwarePropertyTranslateUIDToDevice`; default =
  default input or output device.
- AUHAL with IO enabled on the needed scope, `kAudioOutputUnitProperty_CurrentDevice`, client format 48 kHz
  float mono, render callback / input callback with `AudioUnitRender` into a pre-allocated buffer.
- `kAudioDevicePropertyDeviceIsAlive` listener reports `device_lost`.
- Microphone access through `AVCaptureDevice` authorization; `notDetermined` requests access only when the user
  starts a capture session; denial is `permission_denied`. The app bundle declares `NSMicrophoneUsageDescription`.

## 8. Tools and shells

- `catro-audio-check [--mode meter|tone|monitor] [--seconds N] [--input <id>] [--output <id>]` runs one session
  and prints stream info and statistics; exit codes: 0 ok, 2 arguments, 5 audio error.
- Both shells add an Audio page: input and output pickers fed from the latest capability snapshot, buttons for
  microphone test (meter), live monitor (with a headphones warning), and test tone, live meters (UI timer
  reading atomics, never the audio thread), and the statistics with the latency marked estimated.

## 9. Testing

- Core: ring correctness and a two-thread stress test, meter math, tone continuity, monitor-pipe priming,
  underrun, overrun, and drift correction, engine state machine and failure propagation with a fake platform.
- Windows: integration test opening the default render and capture endpoints for one second; it reports SKIP
  (not pass) when no endpoint exists, as on CI runners.
- macOS: backend code compiles in CI; hardware checks are manual and never claimed without a run.

## 10. Definition of done

Core tests pass on MSVC and clang; the Windows backend passes its integration test on a machine with audio
endpoints; `catro-audio-check` runs all three modes locally; the Windows shell Audio page drives a session with
live meters; macOS code is written and marked unverified until a Mac build runs; docs updated.
