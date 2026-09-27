# Catro

Catro is a native desktop application for Windows and macOS. This repository currently holds its
foundation: a portable C++20 capability core, isolated passive hardware probes for each platform,
and a native diagnostics shell on each platform (WinUI 3 on Windows, SwiftUI on macOS).

There is no Electron, Qt, or browser runtime. The milestone also contains no server, account,
capture, streaming, or WebRTC code.

## Layout

| Path | Contents |
| --- | --- |
| `core/capabilities` | Capability model, validation, snapshot diffing, deterministic media policy. Standard library only. |
| `core/reporting` | Canonical JSON and human reports, strict parsing, redaction. |
| `platform/windows`, `platform/macos` | Passive probes, the out-of-process probe executor, and the capability service. |
| `apps/diagnostics` | Shared plain C++ view model for both shells. |
| `apps/windows`, `apps/macos` | Native diagnostics shells. |
| `tools/capability-probe` | Helper process that runs one passive probe and prints one fragment. |
| `tools/capability-report` | Command-line capability report. |
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
| Windows 11 x64 | Core, probes, report tool, and WinUI shell built and tested locally. |
| macOS 13+ Apple Silicon | Code written; waiting for its first build on the `macos-15` CI runner. |
| macOS 13+ Intel | Code written; waiting for its first build on the `macos-15-intel` CI runner. |

Hosted CI runners have no reliable GPU, encoder, or display. Their reports are build evidence,
not hardware evidence.

## More

- [Capability system architecture](docs/architecture/capability-system.md)
- [Decision records](docs/architecture/decisions)
- [Dependencies](DEPENDENCIES.md) · [Contributing](CONTRIBUTING.md) · [Security](SECURITY.md)
