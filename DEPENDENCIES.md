# Dependencies

The capability core (`core/capabilities`) links only the C++20 standard library. Everything
below belongs to reporting, tests, a platform layer, or a shell.

## Toolchains

| Tool | Version | Used for |
| --- | --- | --- |
| CMake | 3.28 or newer | All builds (`CMakePresets.json`). |
| Visual Studio 2022 | MSVC v143, x64 | Windows core, probes, tools, tests (generator `Visual Studio 17 2022`). |
| Windows SDK | 10.0.22621 or newer | Windows platform layer and shell. |
| Xcode | 16 or newer | macOS platform layer, Swift shell (generator `Xcode`). |
| macOS deployment target | 13.0 | `CMAKE_OSX_DEPLOYMENT_TARGET` in the macOS presets. |

## Source dependencies

Fetched by CMake `FetchContent` at configure time and pinned by commit in
`cmake/Dependencies.cmake`.

| Library | Pin | Used by |
| --- | --- | --- |
| nlohmann/json | `65ee68451d8eb2b5f3a30b410476ab83deb3289b` | `core/reporting` only (private link). |
| Catch2 | `95d8a61b089317bec800c7cc4c64064cbcb3802d` | Tests only. |

## Windows shell NuGet packages

Pinned in `apps/windows/Catro/packages.config` and restored into `apps/windows/packages/`
(ignored by git) by `scripts/build.ps1`.

| Package | Version |
| --- | --- |
| Microsoft.WindowsAppSDK | 2.5.1 |
| Microsoft.WindowsAppSDK.WinUI | 2.3.9 |
| Microsoft.WindowsAppSDK.Foundation | 2.3.12 |
| Microsoft.WindowsAppSDK.Base | 2.0.4 |
| Microsoft.WindowsAppSDK.Runtime | 2.5.1 |
| Microsoft.Windows.CppWinRT | 3.0.260818.1 |
| Microsoft.Windows.SDK.BuildTools | 10.0.26100.4654 |
| Microsoft.Windows.SDK.BuildTools.MSIX | 1.7.251221100 |

The remaining entries in `packages.config` are transitive dependencies of the
`Microsoft.WindowsAppSDK` metapackage. This includes `Microsoft.Web.WebView2` and the AI, ML,
Widgets, and Search component packages. No Catro source references them, and the shell creates
no WebView2 control or browser runtime.

## Platform frameworks

- **Windows:** DXGI, D3D11, D3D12, DXCore, Media Foundation (enumeration only), WASAPI/MMDevice
  (endpoint properties only), power and WTS session APIs.
- **macOS:** Foundation, AppKit, CoreGraphics, Metal (device enumeration), VideoToolbox (encoder
  list only), CoreMedia, CoreAudio (HAL properties only), IOKit, SwiftUI.

The probes never start capture, open an audio stream, or create an encoder session.
