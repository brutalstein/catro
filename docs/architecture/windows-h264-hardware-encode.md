# Windows H.264 hardware encode slice

This slice validates the next zero-raw-CPU-copy stage after Windows Graphics Capture.

## Data path

The implementation is deliberately narrow:

```text
WGC BGRA8 ID3D11Texture2D
  -> D3D11 VideoProcessor
  -> same-device NV12 ID3D11Texture2D
  -> same-adapter Media Foundation hardware H.264 MFT
  -> compressed H.264 access unit
```

The BGRA and NV12 frame surfaces stay on the GPU. Catro never maps the uncompressed screen image to
CPU memory. Only the already-compressed H.264 access unit is copied into a bounded byte vector for
future packetization.

## Adapter affinity

The encoder discovers the DXGI LUID from the actual WGC texture and enumerates H.264 hardware MFTs
with `MFT_ENUM_ADAPTER_LUID` for that exact adapter. An explicitly requested adapter must match the
capture texture or activation fails; there is no silent cross-GPU fallback.

The NV12 conversion texture is created on the same D3D11 device and carries both
`D3D11_BIND_RENDER_TARGET` and `D3D11_BIND_VIDEO_ENCODER`.

## Conversion

The D3D11 Video Processor converts BGRA8 to NV12 and scales to the selected encode size. For this
primitive the encode size must preserve the source aspect ratio exactly; product-level letterbox or
crop policy belongs above the encoder and is not silently invented here.

When `ID3D11VideoContext1` is available, conversion explicitly declares SDR BT.709 RGB input and
limited-range BT.709 YCbCr output. The Media Foundation input type carries matching BT.709 metadata.

## Encoder behavior

Only hardware Media Foundation H.264 transforms are enumerated. The transform must advertise
D3D11-awareness and receives an `IMFDXGIDeviceManager` backed by the same capture device.

Asynchronous hardware MFTs are explicitly unlocked. Catro tracks `METransformNeedInput` and
`METransformHaveOutput` events and never calls `ProcessInput` or `ProcessOutput` without the
corresponding permission.

The encoder intentionally keeps one frame in flight. This trades some maximum throughput for:

- bounded driver queue depth;
- predictable live-screen latency;
- no reuse of the NV12 surface before the previous frame has produced output;
- simple failure accounting.

Because only one frame can be in flight, the Media Foundation input sample wrapping the persistent
NV12 texture is allocated once per stable source size and reused after output completes. If an MFT
requires caller-provided output samples, that sample and its backing buffer are also allocated once
and reused. The diagnostic exposes both allocation counters so accidental per-frame allocator churn
is visible during real-machine validation.

Low-latency mode is requested through `ICodecAPI` and reported as confirmed or unconfirmed rather
than assumed.

## Diagnostic

`catro-encode-check` joins the validated WGC capture path to the encoder.

Defaults:

- 1728x1080;
- 30 fps;
- 6 Mbit/s;
- H.264 Main;
- 2-second GOP;
- 10 seconds.

It reports once per second:

- captured and encoded frame counts;
- H.264 byte count and keyframes;
- GPU conversion average/max wall time;
- encoder wait average/max wall time;
- capture mailbox overwrite/contention counters;
- input/output/timeout failures;
- adapter LUID, hardware MFT name, async/D3D11-aware/low-latency status.

The tool throttles submission to the configured frame rate and releases each WGC frame lease as soon
as that frame has been encoded.

## Acceptance gate

Before screen transport is implemented, a real Windows machine must show:

- a named hardware H.264 MFT on the same adapter as capture;
- D3D11-aware = yes;
- encoded frames and bytes increasing for the full run;
- zero conversion/input/output failures and zero timeouts;
- conversion + encoder latency comfortably below the configured frame interval after warmup;
- capture contention drops remain zero; mailbox overwrite may increase when WGC produces frames
  faster than the configured encode rate, which is the intended one-frame latest-wins policy rather
  than queue growth;
- Media Foundation input sample allocations remain at one for a stable source size and
  caller-provided output sample allocations remain at most one;
- the existing voice and screen-capture tests still green.

Only compressed H.264 packetization/transport is allowed after this gate. Share Screen remains
disabled in the product UI until a two-client video path is validated.
