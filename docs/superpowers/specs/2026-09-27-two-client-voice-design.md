# Two-Client Low-Latency Voice — Design

Status: active implementation milestone 4 from `docs/AGENTS.md` §39.

## 1. Purpose

Prove the complete real-time voice path between two Catro processes before server/channel work:
microphone capture -> 48 kHz mono frames -> Opus -> packetization -> datagram transport -> reorder/jitter handling ->
Opus decode/PLC -> native audio render.

The production product will later use a negotiated secure media transport and SFU. This milestone deliberately keeps
the media core transport-agnostic and uses a small UDP development harness only to prove latency, packet loss,
reordering, device handling, and cross-platform interoperability. The development UDP harness is not a public
Internet security boundary and must not be presented as the production transport.

## 2. Non-negotiable properties

- Shared voice/media core is portable C++20 and contains no Win32, CoreAudio, WinUI, Swift, or socket types.
- Native capture/render remain WASAPI on Windows and CoreAudio/AUHAL on macOS.
- Canonical PCM remains 48 kHz, float32, mono.
- Audio callbacks never encode/decode, allocate, lock, perform socket IO, or log.
- Encoding, decoding, networking, and jitter decisions run on worker/control threads.
- Every queue between real-time audio and worker threads is bounded and preallocated.
- Packet parser is strict and rejects malformed/version-incompatible input without partial state mutation.
- Sequence wrap, duplicate, late, reordered, lost, concealed, and decode-error cases are tested.
- A process may stop/restart without leaving audio or network worker threads behind.
- Logs expose enough timing/counter information to diagnose a user's real machine.

## 3. Codec baseline

Catro uses the reference libopus implementation. The dependency is pinned to the stable libopus 1.6.1 source
archive and checksum.

Initial voice profile:
- 48 kHz mono.
- 20 ms frames (960 samples).
- `OPUS_APPLICATION_VOIP`.
- 48 kbit/s starting bitrate.
- VBR enabled.
- complexity 10 for the development baseline.
- in-band FEC enabled with an initial expected packet-loss percentage of 5.
- decoder supports normal decode and packet-loss concealment (PLC).

These are starting values, not a permanent product policy. Later network adaptation may change bitrate, expected
loss, FEC, DTX, and frame duration while preserving protocol compatibility.

## 4. Voice packet v1

Each datagram carries exactly one Opus packet.

Fixed header, network byte order:
- magic: 2 bytes, ASCII `CV`.
- version: 1 byte, currently 1.
- flags: 1 byte; unknown flags are rejected in v1.
- stream id: uint32.
- sequence: uint16, wrapping naturally.
- reserved: uint16, must be zero in v1.
- RTP-style sample timestamp: uint32, incremented by 960 per 20 ms frame.
- remaining datagram bytes: Opus payload, 1..1275 bytes.

No user identity, authentication token, endpoint address, or platform-specific data is part of this packet.
Encryption/authentication belongs to the later production media transport.

## 5. Core boundaries

`core/voice` owns:
- Opus encoder/decoder RAII wrappers.
- Voice packet serialization/parsing.
- Sequence arithmetic.
- Bounded reorder/jitter buffer and playout decisions.
- Voice counters/statistics independent of sockets and devices.

`core/audio` continues to own native-independent real-time PCM capture/render primitives.

The future voice session coordinator owns worker threads connecting:
- capture SPSC -> frame assembler -> Opus encoder -> transport;
- transport -> packet parser -> jitter buffer -> Opus decoder/PLC -> render SPSC.

## 6. Jitter and loss behavior

The first networked slice starts with a bounded target of three 20 ms packets (60 ms) and a small reorder window.
It may later become adaptive.

For a missing playout packet:
- use in-band FEC when the following packet is available and the decoder can recover the missing frame;
- otherwise invoke Opus PLC for exactly one 20 ms frame;
- never stall the render callback waiting for a network packet.

Duplicates are dropped. Packets older than the playout cursor are late and dropped. Implausibly far-ahead packets
are rejected/reset according to the session policy rather than growing memory.

## 7. Development transport

A CLI development harness will provide a one-way and then full-duplex UDP peer on localhost/LAN.

Rules:
- default bind is loopback unless the user explicitly chooses another address;
- packet size is bounded before parse;
- receive timeout allows clean shutdown;
- no socket work occurs on an audio callback;
- every second it prints packet, loss, late, reorder, PLC/FEC, queue depth, audio glitch, underrun/overrun and
  encode/decode timing summaries.

This transport is for engineering validation only. Public-Internet NAT traversal, ICE/STUN/TURN, DTLS-SRTP,
identity/authentication, and SFU routing are later milestones.

## 8. Test progression

1. Codec + packet unit tests, including malformed packets and sequence wrap.
2. Deterministic simulated loss/reorder tests for jitter/PLC.
3. In-process encode/decode quality/sanity test with generated speech-like/tone PCM.
4. Localhost one-way process test.
5. Localhost full-duplex process test.
6. Two Windows machines on one LAN.
7. Windows <-> macOS interoperability once a real Mac is available.

No real-mac validation is claimed until executed on real macOS hardware.

## 9. Definition of done

Milestone 4 is complete when two native Catro processes can talk bidirectionally with Opus over the development
transport, audio callbacks stay real-time safe, loss/reorder are tolerated without blocking playback, counters
make failures diagnosable, Windows local/LAN testing is clean, and macOS builds/tests pass with real hardware
validation explicitly tracked separately.
