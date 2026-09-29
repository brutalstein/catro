# Catro

Catro is a native desktop application for Windows and macOS. The repository includes a portable
C++20 capability core, isolated passive hardware probes, low-latency native audio, bounded Opus
voice, persistent personal/shared-server state, a production signaling and directory service, and
native desktop shells (WinUI 3 on Windows, SwiftUI on macOS).

The Windows product path now provisions authenticated WebRTC rooms for voice, hardware-encoded
H.264 screen sharing, native D3D11 decode/presentation, and selected-window application audio.
There is no Electron, Qt, or browser runtime. The connected-UDP voice/video peers remain deliberately
unencrypted localhost/private-LAN engineering harnesses; they are not the normal product transport
or a public-Internet security boundary.

## Layout

| Path | Contents |
| --- | --- |
| `core/capabilities` | Capability model, validation, snapshot diffing, deterministic media policy. Standard library only. |
| `core/reporting` | Canonical JSON and human reports, strict parsing, redaction. |
| `core/audio` | Real-time audio primitives and the session engine. Standard library only. |
| `core/voice` | Opus codec wrapper, packet v1, bounded jitter/loss handling, real-time PCM bridges, transport-agnostic media pipeline. |
| `core/video`, `core/transport`, `core/rtc` | Bounded H.264 RTP, UDP primitives, and the production small-room WebRTC mesh transport. |
| `core/community` | Typed identity/server/channel roles, personal-server bootstrap, invite token format, versioned local-state codec. |
| `platform/windows`, `platform/macos` | Passive probes, native audio, local state, and platform media implementations. Windows includes capture, hardware H.264 encode/decode, D3D11 presentation, and process-loopback audio. |
| `apps/diagnostics` | Shared plain C++ diagnostics/audio presentation. |
| `apps/shell` | Portable personal-server/channel navigation contract. |
| `apps/room-runtime`, `apps/voice-runtime`, `apps/screen-runtime` | Windows runtime boundaries for shared RTC rooms, duplex voice, and full-duplex screen media. |
| `apps/windows`, `apps/macos` | Native desktop shells. Windows includes the server-first voice and screen-stream product UI. |
| `services/signaling` | TLS-ready identity, membership, invite, short-lived RTC/TURN provisioning, and WebRTC signaling service. |
| `tools/capability-probe` | Helper process that runs one passive probe and prints one fragment. |
| `tools/capability-report` | Command-line capability report. |
| `tools/audio-check` | Command-line audio session check (meter, tone, monitor). |
| `tools/voice-peer` | Localhost/LAN engineering harness for Opus voice, bounded UDP, jitter/FEC/PLC, and live timing counters. |
| `tools/capture-check`, `tools/encode-check`, `tools/video-peer` | Windows capture, hardware encode, and two-client H.264 engineering validation tools. |
| `tests` | Core, reporting, policy, and platform tests. |

## Windows product shell

The Windows app opens directly into the user's personal server: one `# general` text channel, one
`Voice` channel, a member rail, owner state, invite/join connection points, and voice-local screen
share controls. Shared-server members can join the same provisioned RTC room, publish a window or
display, opt into remote viewing, pop the stream out or use full screen, and optionally include the
selected window's process audio. The shell is native WinUI 3 and intentionally avoids
blur/backdrop/animation costs. See [`docs/ui-foundation.md`](docs/ui-foundation.md).

## Quick start

Windows (PowerShell):

```powershell
./scripts/bootstrap.ps1
./scripts/build.ps1
./scripts/test.ps1
./scripts/run.ps1                     # native Windows app
./scripts/run.ps1 -Report --format json --output out/capability-report.json
```

macOS:

```sh
scripts/bootstrap.sh
scripts/build.sh
scripts/test.sh
scripts/run.sh                        # native macOS app
scripts/run.sh --report --format json --output out/capability-report.json
```

The scripts check prerequisites and never install tools or change system settings. See
[docs/building.md](docs/building.md) and [docs/troubleshooting.md](docs/troubleshooting.md).

## Platform status

| Platform | Status |
| --- | --- |
| Windows 11 x64 | Release build, 44-test local gate, capability report, and WinUI startup pass locally. Voice/screen product integration is implemented; two-real-machine glass-to-glass latency and resource validation remains a manual acceptance gate. The shell still has a known UI Automation issue (see troubleshooting). |
| macOS 13+ Apple Silicon | Code written; waiting for its first build on the `macos-15` CI runner. |
| macOS 13+ Intel | Code written; waiting for its first build on the `macos-15-intel` CI runner. |

Hosted CI runners have no reliable GPU, encoder, or display. Their reports are build evidence,
not hardware evidence.

## More

- [Personal identity and server state](docs/architecture/personal-state.md)
- [Capability system architecture](docs/architecture/capability-system.md)
- [Decision records](docs/architecture/decisions)
- [Dependencies](DEPENDENCIES.md) · [Contributing](CONTRIBUTING.md) · [Security](SECURITY.md)
