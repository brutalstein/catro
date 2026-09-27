# Voice channel runtime

Catro's current product priority is the media path: join a voice channel, talk with low overhead,
mute/deafen safely, then add native screen capture and hardware encode. Text messaging and the invite
UI are intentionally not being expanded in this slice.

## Architecture

The WinUI shell does not link the Opus/network worker graph directly. It talks through the narrow
`catro-voice-runtime.dll` C ABI. The DLL owns:

- WASAPI capture/render through the native Windows audio platform;
- the bounded Catro voice pipeline;
- Opus encode/decode;
- jitter/FEC/PLC;
- the proven non-blocking UDP engineering transport;
- the codec/network worker thread.

This keeps codec/network implementation types out of XAML and lets a future production
ICE/DTLS-SRTP/SFU transport replace the direct UDP backend without redesigning the voice-channel UI.

The XAML thread only performs lifecycle commands and reads lock-free snapshots at 2 Hz while the
server page is visible. Navigating to Settings/System pauses UI polling but does not disconnect the
voice session.

## Controls

- **Join / Leave** starts or stops the native duplex session.
- **Mute** keeps capture timing continuous but replaces the codec input frame with zero PCM before
  Opus encode. Raw microphone samples are therefore not transmitted while muted.
- **Deafen** drains decoded PCM on the render callback while outputting silence, so undeafen resumes
  at live audio instead of replaying a backlog. Deafen also forces effective transmit mute.
- The shell reports `Joining`, `Waiting for peer`, `Connected`, `Muted`, `Deafened`, or
  `Voice error` without high-frequency UI work.

## Temporary direct-peer validation

Until rendezvous/invite membership is implemented, the app uses a private/loopback direct-peer
configuration. This is an engineering bridge, not the final Internet security boundary.

Defaults:

- slot 1: bind `127.0.0.1:50000`, peer `127.0.0.1:50001`
- slot 2: bind `127.0.0.1:50001`, peer `127.0.0.1:50000`

Optional environment variables:

- `CATRO_VOICE_SLOT=2`
- `CATRO_VOICE_BIND=<private-ip:port>`
- `CATRO_VOICE_PEER=<private-ip:port>`
- `CATRO_VOICE_INPUT=<mmdevice endpoint id>`
- `CATRO_VOICE_OUTPUT=<mmdevice endpoint id>`

Public IPv4 peers remain rejected by the engineering UDP transport.

## Next media slice

After real-machine voice-channel validation and current-head CI are green, the next slice is Windows
Graphics Capture source selection and a bounded GPU-resident frame pipeline. Screen-share transport
is not enabled in the UI until capture and hardware encode are separately validated.


## Real-machine validation

Windows duplex validation on 2026-09-27 exercised the WinUI voice runtime against the engineering
peer for roughly 38 seconds at 48 kHz / 20 ms Opus frames. The peer reported 1,927 transmitted and
1,930 received packets with zero network drops, malformed/duplicate/late packets, capture drops,
codec errors, render-full events, underruns, or worker resynchronizations. One startup WASAPI
discontinuity was reported and remained flat. Mute/deafen behavior was also confirmed audibly.

This validates the native voice-channel media path on that machine. It does not validate public
Internet transport, NAT traversal, SFU behavior, or macOS interoperability.
