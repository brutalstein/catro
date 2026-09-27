# Building

Prerequisites and pinned versions are in [DEPENDENCIES.md](../DEPENDENCIES.md). Every build
writes under `out/`. The Windows shell also restores NuGet packages into `apps/windows/packages/`.

## Windows

```powershell
./scripts/bootstrap.ps1                          # check prerequisites only
./scripts/build.ps1 [-Configuration Release] [-SkipShell]
./scripts/test.ps1  [-Configuration Release] [-Filter <regex>]
./scripts/run.ps1                                # launch the WinUI shell
./scripts/run.ps1 -Report --format human         # print a redacted human report
```

What `build.ps1` runs:

1. `cmake --preset windows-msvc`
2. `cmake --build out/build/windows-msvc --config <Configuration>` builds the core, the
   platform layer, the helper, the report tool, the shared view model, and the tests.
3. MSBuild on `apps/windows/Catro.sln` with `-restore -p:RestorePackagesConfig=true` and
   `CatroCoreRoot=out/build/windows-msvc`. The shell links the CMake-built static libraries
   from that directory. It copies `catro-capability-probe.exe` next to `Catro.exe`.

| Output | Path |
| --- | --- |
| Shell | `out/apps/windows/x64/<Configuration>/Catro.exe` |
| Report tool | `out/build/windows-msvc/<Configuration>/catro-capability-report.exe` |
| Audio check | `out/build/windows-msvc/<Configuration>/catro-audio-check.exe` |
| Probe helper | `out/build/windows-msvc/<Configuration>/catro-capability-probe.exe` |

## macOS

```sh
scripts/bootstrap.sh
scripts/build.sh [Debug|Release]                 # CATRO_PRESET=macos-universal for arm64 + x86_64
scripts/test.sh  [Debug|Release] [regex]
scripts/run.sh                                   # open Catro.app
scripts/run.sh --report --format human
```

`build.sh` configures the `macos-clang` preset (Xcode generator, deployment target 13.0) and
builds every target. CMake generates the Xcode project for the SwiftUI shell, so no project
file is checked in. The post-build step copies the probe helper into
`Catro.app/Contents/MacOS/`.

| Output | Path |
| --- | --- |
| Shell | `out/build/<preset>/<Configuration>/Catro.app` |
| Report tool | `out/build/<preset>/<Configuration>/catro-capability-report` |
| Audio check | `out/build/<preset>/<Configuration>/catro-audio-check` |

The Swift shell needs the Xcode or Ninja generator. With another generator, CMake builds
everything except the shell.

## Report tool

```
catro-capability-report [--format human|json] [--output <file>]
```

`human` (the default) is the engineering report with device names redacted. `json` is the complete canonical report.
Its schema is `catro.capabilities` 1.0 and its policy is 1.0.0. `--output` writes a new file
atomically and refuses to replace an existing one.

## Audio check

```
catro-audio-check [--mode meter|tone|monitor] [--seconds 0-60] [--input <id>] [--output <id>]
```

`meter` (the default) captures and prints input levels, `tone` plays a 440 Hz tone at -14 dBFS,
and `monitor` plays the microphone back live, so use headphones. Device ids are the capability
report's endpoint ids (`mmdevice:...`, `coreaudio:<uid>:input|output`). Without one, the tool
uses the system default: the communications endpoint on Windows, the default device on macOS.
The engine runs at 48 kHz float mono and the OS converts to the device format. The printed
latency is an estimate from device and buffer sizes, not a measurement. Exit codes: 0 ok,
2 invalid arguments, 5 audio failure.

The Windows integration test (`catro_windows_audio`) opens the default endpoints for about a
second and reports skipped on a machine without them. On macOS, `catro-audio-check` runs from a
terminal, so the terminal app receives the microphone permission.

## Probe budgets

| Probe | Hard budget |
| --- | --- |
| `<os>.system.v1` | 500 ms |
| `<os>.runtime.v1` | 500 ms |
| `<os>.gpu_display.v1` | 1500 ms |
| `<os>.encoders.v1` | 1500 ms |
| `<os>.audio.v1` | 1000 ms |
| First publication | 2000 ms |

A probe that misses its budget is terminated and reported as a timeout. The first snapshot is
published within the publication budget, even when some probes have not finished.
