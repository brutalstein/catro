# Windows screen capture slice

This slice proves native screen capture before Catro activates hardware encoding or screen
transport. It deliberately does not enable the product Share Screen button yet.

## Hot path

The Windows backend uses Windows Graphics Capture with a free-threaded D3D11 frame pool:

```text
desktop compositor
  -> Windows Graphics Capture
  -> ID3D11Texture2D
  -> one-frame latest mailbox
  -> next: hardware encoder worker
```

Pixel data is never mapped to CPU memory. The callback only unwraps the WinRT surface to an
`ID3D11Texture2D`, publishes the newest frame, and returns.

The WGC `Direct3D11CaptureFrame` is retained as a lease beside the texture so the frame-pool
surface cannot be recycled while the consumer is still reading it.

## Backpressure

Latency is more important than replaying stale video. Catro therefore keeps one pending frame:

- the WGC pool has two buffers;
- the application mailbox has one latest frame;
- the callback uses `try_lock` and never waits for the consumer;
- an unconsumed frame is replaced by the newer one and counted;
- lock contention drops the callback frame and is counted;
- the consumer blocks efficiently on a condition variable rather than polling.

This bounds memory and prevents a slow encoder from creating seconds of screen-share latency.

## GPU affinity

The backend accepts an optional packed DXGI adapter LUID. Production media policy can pin WGC to
the same adapter selected for hardware encoding. When no adapter is provided, the engineering
capture diagnostic uses the OS high-performance GPU preference and falls back only to another
hardware adapter.

An explicit adapter request never silently changes GPUs.

## Resize and source lifetime

Content-size changes recreate the two-buffer WGC frame pool and are counted. The backend also
tracks source closure, native HRESULT failures, received/published frames, mailbox overwrites,
contention drops, texture size/format, and the actual capture adapter LUID.

Stopping first prevents new callback work, unregisters events, closes the WGC session/pool, waits
for in-flight callbacks to leave, and only then releases D3D/WinRT resources.

## Hardware validation

`catro-capture-check` captures the primary display without copying pixels to CPU memory. It
consumes only GPU texture leases and metadata and prints cumulative counters once per second.

The slice is accepted on a real Windows machine when:

- frames are received and consumed for the requested duration;
- texture format is BGRA8;
- dimensions match the captured display;
- contention drops stay at zero under the diagnostic consumer;
- mailbox overwrites are zero or rare and do not grow continuously;
- there is no capture failure or unexpected source closure;
- build/tests and current-head CI remain green.

Only after this gate should Catro add GPU color conversion / hardware encoder activation.
