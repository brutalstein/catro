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
