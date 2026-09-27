# Troubleshooting

## Build

**`Catro core libraries were not found under ...`** The WinUI shell links the CMake output.
Run `./scripts/build.ps1`. If you build the solution by hand, first run
`cmake --build --preset windows-debug`, then pass `/p:CatroCoreRoot=<CMake build directory>`.

**`This project references NuGet package(s) that are missing`** Build with
`-restore -p:RestorePackagesConfig=true`, as `build.ps1` does. The packages go into
`apps/windows/packages/`.

**`WMC1007`, a missing `*.xaml.g.h`, or unresolved `XamlTypeInfo` symbols** These mean the XAML
compile passes did not run in order. Do not remove the `CatroXamlWinMDReferences` or
`CatroXamlCompileInputs` targets from `Catro.vcxproj`. Delete `out/apps/windows/obj` and build
again.

**`XamlMetaDataProvider.cpp` appears next to the sources** Delete it. Generated files belong in
`out/apps/windows/obj/.../Generated Files/`.

**CMake generator `Visual Studio 17 2022` not found** The `windows-msvc` preset needs Visual
Studio 2022 with the C++ desktop workload.

**The macOS shell target is missing** The Swift shell needs the Xcode or Ninja generator. Use
`scripts/build.sh`, which uses the Xcode preset.

## Runtime

**Every probe reports `os_failure` within milliseconds** The helper did not start. The helper must sit next to the executable that
starts it: `catro-capability-probe(.exe)` beside `Catro.exe`, inside
`Catro.app/Contents/MacOS/`, or beside the report tool.

**A probe reports `timeout`** The probe missed its hard budget (see
[building.md](building.md#probe-budgets)). This is common on a cold start or a heavily loaded
machine. Refresh, or run the report tool again.

**Screen capture permission is `unknown` on macOS** This is expected. Discovery only runs the
non-prompting preflight check. Catro never asks for the permission in this milestone.

**Remote session is `unknown` on macOS** macOS has no passive API that reports it, so the runtime
probe is always `partial` there.

**The Windows shell closes when a screen reader or UI Automation tool walks it (known issue)**
Walking the UI Automation tree below the XAML island root causes an access violation in
`Microsoft.UI.Xaml.dll`. This happens with Narrator-style clients, `System.Windows.Automation`,
and the native `IUIAutomation` client. It reproduces with a window that holds only a `Grid` and a
`TextBlock`, so it is not caused by Catro's views. Removing the custom title bar, Mica, or
`XamlControlsResources` does not help, and neither does removing the app's `.pri`. The build here
is unpackaged, self-contained Windows App SDK 2.5.1 (WinUI 2.3.9), without the Visual Studio Windows
app workload. Other WinUI 3 apps on the same machine walk cleanly. Until this is resolved, the
Windows shell is not usable with a screen reader.

**`microphone access denied`** Windows blocks desktop apps from the microphone. Turn on
*Settings > Privacy & security > Microphone > Let desktop apps access your microphone*.

**`device in use`** Another application holds the endpoint in exclusive mode. Close it, or turn off
*Allow applications to take exclusive control* for that device in the Sound control panel.

**Live monitor howls** The monitor plays the microphone back live. Use headphones.

## Collecting a report for a bug

```powershell
./scripts/run.ps1 -Report --output out/catro-report.txt
```

```sh
scripts/run.sh --report --output out/catro-report.txt
```

The human report replaces device names, but identifiers remain, so the report is not anonymous.
Attach the JSON report (`--format json`) only if a maintainer asks for it. JSON is complete and
unredacted.
