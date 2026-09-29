# Two-client H.264 screen streaming

The Windows screen path was delivered in two implementation gates. Automated tests cover bounded
media contracts and runtime lifecycle; the remaining acceptance gate is measurement on two real
Windows machines.

## Gate A — compressed transport

`catro-video-peer` validates that a real Windows sender can sustain:

```text
Windows Graphics Capture
→ D3D11 BGRA texture
→ D3D11 video processor / NV12
→ same-adapter hardware H.264 MFT
→ RFC 6184 packetization
→ scatter/gather UDP
→ bounded RTP/H.264 reassembly on the peer
```

No raw pixels leave GPU memory on the sender. RTP packetization does not copy H.264 payload bytes,
and UDP uses `WSASend` scatter/gather so the RTP/FU-A prefix and compressed payload do not need a
temporary concatenation buffer.

The receiver allocates its maximum access-unit storage once. Individual datagrams use a fixed
stack buffer. Oversized or malformed frames are dropped instead of growing memory.

The sender derives an even H.264 size from the exact reduced source aspect ratio using integer GCD
math. It never upscales and respects width/height ceilings. The frame scheduler is integer
nanoseconds; no floating-point arithmetic is required in packet timing.

This tool remains an unencrypted engineering transport and enforces numeric loopback/private IPv4.
It is not the product transport.

## Gate B — decode and presentation

Gate B is implemented in the Windows screen runtime and WinUI shell:

```text
authenticated RTC room
→ bounded screen media channel
→ RTP/H.264 reassembly
→ Media Foundation H.264 decode
→ D3D11 swap-chain presentation
→ embedded, pop-out, or full-screen viewer
```

The Share Screen control can publish a display or window after the user joins voice. Remote viewing
is opt-in, local preview work is suspended when hidden, and selected-window process audio can share
the same RTC room as a separate bounded Opus channel. Direct private-IPv4 mode remains available only
for engineering diagnostics.

The outstanding Gate B acceptance work is a two-real-machine run that records glass-to-glass
latency, encode/decode behavior, CPU/GPU load, memory bounds, recovery after stream restart, and TURN
fallback behavior. Until that evidence is recorded, implementation-complete must not be described as
real-machine validated.

## Decoder latency control

The Windows inbox H.264 decoder is a documented Media Foundation special case for
`CODECAPI_AVLowLatencyMode`: it expects `VT_UI4` through `ICodecAPI`, while the encoder and
most other codecs use `VT_BOOL`. Catro therefore sets both the MFT `MF_LOW_LATENCY` UINT32
attribute and the decoder's `ICodecAPI` property using `VT_UI4`. The video-peer diagnostic
prints whether low-latency activation was accepted so a fixed decoder backlog cannot be mistaken
for network latency.
