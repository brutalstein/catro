# Two-Client Low-Latency Voice — Implementation Plan

Spec: `docs/superpowers/specs/2026-09-27-two-client-voice-design.md`.
Branch: `feature/two-client-voice`.

Every slice must remain independently buildable/testable. Do not proceed past a red local gate.

| # | Slice | Observable result | Verification |
|---|---|---|---|
| 1 | Pin libopus; add transport-agnostic codec wrappers and voice packet v1 | PCM can round-trip through Opus and packet bytes are deterministic/strict | local green 2026-09-27 |
| 2 | Bounded reorder/jitter buffer, sequence wrap logic, PLC/FEC decisions | deterministic simulated loss/reorder survives without unbounded memory | local green 2026-09-27 |
| 3 | Real-time capture/render bridges and worker-owned frame queues | no codec/socket work is reachable from audio callbacks | local green 2026-09-27 |
| 4 | Portable UDP development transport and one-way `catro-voice-peer` | one local process captures/encodes/sends; another decodes/plays | implemented; localhost hardware gate pending |
| 5 | Full-duplex peer, shutdown/restart, rich once-per-second counters | two processes on one PC/LAN can talk and diagnose loss/latency | implemented; localhost/LAN hardware gate pending |
| 6 | macOS compile/interoperability gate, docs and final regression | Windows remains green; macOS build green; real Mac remains explicit manual gate | CI + real hardware when available |

### Slice 1 exact scope

Files:
- `cmake/Dependencies.cmake`
- `core/voice/CMakeLists.txt`
- `core/voice/include/catro/voice/codec.hpp`
- `core/voice/include/catro/voice/packet.hpp`
- `core/voice/src/codec.cpp`
- `core/voice/src/packet.cpp`
- `tests/voice/voice_test.cpp`
- root/test CMake integration

Acceptance:
- no platform headers in `core/voice`;
- Opus dependency is pinned by archive hash;
- 20 ms / 48 kHz mono round trip succeeds;
- malformed packet, bad magic/version/flags/reserved, empty payload, oversized payload are rejected;
- sequence and timestamp serialize in network byte order;
- no exceptions cross the public voice codec API;
- Catro-owned warnings remain errors.

Commit boundary: one green commit after local MSVC tests.


### Slice 3 implementation invariants

- Audio callback path is only fixed-capacity SPSC memory movement, relaxed counters, and silence fill.
- Capture callback writes are all-or-nothing; overload drops a whole callback block rather than corrupting a 20 ms codec frame.
- Worker-to-render writes are one complete 960-sample decoded frame or a queue-full rejection. The bridge counts rejection attempts; the pipeline counts actual decoded frames dropped at that boundary.
- Default queue storage is eight voice frames per direction and constructor input is clamped to 64 frames.
- SPSC copies use at most two `memcpy` segments around the ring wrap; no per-sample callback loop is required.
- `VoicePipeline` owns Opus encoder/decoder, sequence/timestamp generation, packet parsing, jitter/FEC/PLC decisions, and the two real-time bridges while remaining transport-agnostic.
- Opus encodes directly into the datagram payload region; the fixed header is written afterward, avoiding a second payload copy.
- Codec, jitter, and packet methods are worker-side only. The audio callback cannot reach them through either bridge.
- Tests cover exact SPSC semantics, bounded overload, render underrun silence, sustained two-thread ordering, sequence wrap, packet reordering, FEC, PLC, malformed input, and bounded render backpressure.


### Slices 4-5 implementation invariants

- UDP is a development-only transport with numeric IPv4, explicit bind/peer endpoints, and no encryption/authentication claims.
- The UDP socket is connected to one peer, non-blocking, bounded, and sleeps in select; voice work does not busy-spin.
- One worker owns encode, send, receive, jitter, decode, and playout scheduling. The only concurrent media code is the native audio callbacks through the SPSC bridges.
- The worker drains at most four encode frames and 64 receive datagrams per iteration so either direction cannot monopolize the loop.
- Playout is paced at 20 ms with a maximum three-tick catch-up. Larger scheduler stalls are counted and resynchronized instead of generating an unbounded PLC burst.
- Native playback starts before capture in duplex sessions. Startup render silence is tracked separately and is not misreported as a steady-state underrun.
- Windows disables UDP ICMP connection-reset behavior so one localhost peer may start before the other without killing the socket.
- Socket buffers are finite, packet buffers are fixed, and a voice datagram remains below 1400 bytes.
- Live diagnostics read audio state once per second or on failure; the 2 ms worker loop does not take the audio-session mutex.
- Real UDP loopback tests send encoded Opus packets through the OS socket stack before jitter and decode, while the CLI parser has explicit range/repetition tests.
