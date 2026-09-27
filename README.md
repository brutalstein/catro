# Catro

Catro is a native desktop application for Windows and macOS. This repository currently holds its
foundation: a portable C++20 capability core, isolated passive hardware probes for each platform,
a low-latency audio engine (microphone meter, test tone, live monitor), the transport-agnostic
Opus voice core (packet format, bounded jitter/loss handling, and real-time PCM bridges), and a
native diagnostics shell on each platform (WinUI 3 on Windows, SwiftUI on macOS).

There is no Electron, Qt, or browser runtime. The repository still contains no production media
transport, server, account, screen capture, streaming, or WebRTC code. A deliberately unencrypted,
connected-UDP engineering peer exists for localhost/LAN voice validation only; it is not a public
Internet transport or security boundary.

## Layout

| Path | Contents |
| --- | --- |
| `core/capabilities` | Capability model, validation, snapshot diffing, deterministic media policy. Standard library only. |
| `core/reporting` | Canonical JSON and human reports, strict parsing, redaction. |
| `core/audio` | Real-time audio primitives and the session engine. Standard library only. |
| `core/voice` | Opus codec wrapper, packet v1, bounded jitter/loss handling, real-time PCM bridges, transport-agnostic media pipeline. |
| `platform/windows`, `platform/macos` | Passive probes, the out-of-process probe executor, the capability service, and the audio backends (WASAPI, CoreAudio AUHAL). |
| `apps/diagnostics` | Shared plain C++ view model for both shells. |
| `apps/windows`, `apps/macos` | Native diagnostics shells. |
| `tools/capability-probe` | Helper process that runs one passive probe and prints one fragment. |
| `tools/capability-report` | Command-line capability report. |
| `tools/audio-check` | Command-line audio session check (meter, tone, monitor). |
| `tools/voice-peer` | Localhost/LAN engineering harness for Opus voice, bounded UDP, jitter/FEC/PLC, and live timing counters. |
| `tests` | Core, reporting, policy, and platform tests. |

## Quick start

Windows (PowerShell):

```powershell
./scripts/bootstrap.ps1
./scripts/build.ps1
./scripts/test.ps1
./scripts/run.ps1                     # diagnostics shell
./scripts/run.ps1 -Report --format json --output out/capability-report.json
```

macOS:

```sh
scripts/bootstrap.sh
scripts/build.sh
scripts/test.sh
scripts/run.sh                        # diagnostics shell
scripts/run.sh --report --format json --output out/capability-report.json
```

The scripts check prerequisites and never install tools or change system settings. See
[docs/building.md](docs/building.md) and [docs/troubleshooting.md](docs/troubleshooting.md).

## Platform status

| Platform | Status |
| --- | --- |
| Windows 11 x64 | Core, probes, report tool, audio engine, and WinUI shell built and tested locally. The shell crashes under UI Automation (see troubleshooting). |
| macOS 13+ Apple Silicon | Code written; waiting for its first build on the `macos-15` CI runner. |
| macOS 13+ Intel | Code written; waiting for its first build on the `macos-15-intel` CI runner. |

Hosted CI runners have no reliable GPU, encoder, or display. Their reports are build evidence,
not hardware evidence.

## More

- [Capability system architecture](docs/architecture/capability-system.md)
- [Decision records](docs/architecture/decisions)
- [Dependencies](DEPENDENCIES.md) · [Contributing](CONTRIBUTING.md) · [Security](SECURITY.md)
