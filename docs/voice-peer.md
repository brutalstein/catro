# Voice peer validation

catro-voice-peer is the development transport for the two-client voice milestone. It is
intentionally limited to numeric loopback/private/link-local IPv4 and connected UDP so localhost/LAN behavior is observable
without mixing production signaling, NAT traversal, authentication, or encryption into the media
core.

> This tool is unencrypted. Use loopback or a trusted LAN only. Do not expose it to the public
> Internet.

## Windows same-machine one-way test

Use headphones. Build first:

~~~powershell
./scripts/build.ps1 -Configuration Debug -SkipShell
~~~

Terminal A captures and sends:

~~~powershell
./out/build/windows-msvc/Debug/catro-voice-peer.exe ^
  --bind 127.0.0.1:50000 ^
  --peer 127.0.0.1:50001 ^
  --mode send ^
  --seconds 20 ^
  --input "<48-kHz-input-endpoint-id>" ^
  --stream-id 1001
~~~

In PowerShell, enter the command on one line or replace the caret continuations above with PowerShell
backticks.

Terminal B receives and renders:

~~~powershell
./out/build/windows-msvc/Debug/catro-voice-peer.exe ^
  --bind 127.0.0.1:50001 ^
  --peer 127.0.0.1:50000 ^
  --mode receive ^
  --seconds 20 ^
  --output "<output-endpoint-id>" ^
  --stream-id 2001
~~~

Start the receiver first, then the sender. The receiver should play the sender microphone after
the initial jitter cushion. A clean final summary has no malformed packets, capture drops,
render-full drops, worker resyncs, or continuing audio glitches. A small startup-silence count is
normal because render is deliberately armed before the first decoded packet.

## Windows same-machine full duplex

Once one-way audio is clean, run both sides in duplex. On one machine this intentionally loops the
same physical microphone through two processes, so use headphones and expect to hear your own voice.

Terminal A uses local port 50000, peer port 50001, mode duplex, stream id 1001.
Terminal B uses local port 50001, peer port 50000, mode duplex, stream id 2001.
Pass the same validated 48 kHz input and desired output endpoint to both.

## LAN test

Replace 127.0.0.1 with each machine's private or link-local LAN IPv4 address. Public IPv4 and 0.0.0.0 are rejected by the transport. Each process binds its own
machine address/port and sets --peer to the other machine. Keep the two stream ids distinct.
A host firewall may need to allow the executable on the selected private network.

## Reading the counters

- tx/rx: UDP voice packets and bytes handled.
- net-drop: encoded frames dropped because the non-blocking socket could not accept a send.
- reorder/late: packet ordering observations from the bounded jitter buffer.
- fec/plc: missing frames recovered from Opus in-band FEC or synthesized with PLC.
- jitter current/peak: packets held in the fixed 32-slot jitter store.
- cap-drop: capture callback blocks rejected because the bounded worker queue was full.
- render-full: decoded frames that could not enter the bounded render queue.
- underrun: render callbacks that needed silence after playback had already been primed.
- startup-silence: silence emitted before the first decoded frame; expected during startup.
- glitches: discontinuities reported by the native audio backend.
- enc/dec avg/max us: worker-side codec/pipeline timing, not audio-callback time.
- worker-resync: the worker missed more than three 20 ms playout ticks and resynchronized.

For a healthy localhost run, sustained values for net-drop, cap-drop, render-full, underrun, and
worker-resync should remain zero. reorder, late, fec, and plc should normally remain zero on
loopback. Native audio may report one startup discontinuity on some WASAPI devices; a counter that
keeps increasing is not considered healthy.

## Regression gate

~~~powershell
./scripts/test.ps1 -Configuration Debug -Filter "catro_voice|catro_audio"
./scripts/verify.ps1 -Configuration Debug
~~~

Hosted CI proves compilation, deterministic media behavior, UDP loopback, and sanitizers. It does
not prove real microphone, Bluetooth, LAN, or macOS hardware behavior.
