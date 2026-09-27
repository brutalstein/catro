# Two-client H.264 transport validation

The first two-client screen path is deliberately split into two gates.

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

## Gate B — decode and presentation

Gate A is not yet a user-visible screen stream. The next gate adds hardware H.264 decode to a D3D11
surface and native presentation, then validates end-to-end glass-to-glass latency and resource use on
two real Windows machines. The product Share Screen control remains disabled until Gate B passes.
