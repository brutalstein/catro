<p align="center">
  <img src="docs/assets/catro-banner.svg" alt="Catro — native voice, screen sharing, and chat for small groups" width="100%">
</p>

<p align="center">
  <a href="https://github.com/brutalstein/catro/releases/latest/download/Catro-windows-x64.zip"><img alt="Download for Windows" src="https://img.shields.io/badge/Download_for_Windows-x64-E08A5C?style=for-the-badge&logo=windows11&logoColor=white&labelColor=2A1F1A"></a>
  <a href="https://github.com/brutalstein/catro/releases/latest/download/Catro-macos-arm64.zip"><img alt="Download for Mac with Apple silicon" src="https://img.shields.io/badge/Download_for_Mac-Apple_silicon-E08A5C?style=for-the-badge&logo=apple&logoColor=white&labelColor=2A1F1A"></a>
  <a href="https://github.com/brutalstein/catro/releases/latest/download/Catro-macos-x64.zip"><img alt="Download for Mac with Intel" src="https://img.shields.io/badge/Download_for_Mac-Intel-E08A5C?style=for-the-badge&logo=apple&logoColor=white&labelColor=2A1F1A"></a>
</p>

<p align="center">
  <a href="https://github.com/brutalstein/catro/releases/latest"><img alt="Latest release" src="https://img.shields.io/github/v/release/brutalstein/catro?style=flat-square&label=release&color=E08A5C&labelColor=2A1F1A"></a>
  <img alt="Windows 10 2004+ and Windows 11" src="https://img.shields.io/badge/Windows-10_2004%2B_%C2%B7_11-F6EBDD?style=flat-square&labelColor=2A1F1A">
  <img alt="macOS 13+" src="https://img.shields.io/badge/macOS-13%2B-F6EBDD?style=flat-square&labelColor=2A1F1A">
  <img alt="Native C++20, WinUI 3 and SwiftUI" src="https://img.shields.io/badge/native-C%2B%2B20_%C2%B7_WinUI_3_%C2%B7_SwiftUI-F6EBDD?style=flat-square&labelColor=2A1F1A">
  <a href="https://github.com/brutalstein/catro/actions/workflows/ci.yml"><img alt="CI" src="https://img.shields.io/github/actions/workflow/status/brutalstein/catro/ci.yml?branch=main&style=flat-square&label=CI&labelColor=2A1F1A"></a>
</p>

<p align="center">
  <b>A small, fast home for your people.</b><br>
  Talk, share your screen with game audio, and keep a running chat —<br>
  in native Windows and Mac apps that start instantly and stay out of your GPU's way.
</p>

<p align="center">
  <img src="docs/assets/catro-windows.png" alt="Catro on Windows: a server with a #general text channel, a Voice channel, and the member list" width="92%">
</p>

---

## Why Catro

Most chat apps ship a web browser in disguise. Catro doesn't. It is written in C++20 with a
native WinUI 3 interface on Windows and SwiftUI on the Mac, uses your hardware video encoder for
screen sharing, and is built for the way friends actually hang out: a handful of people in voice
and one person streaming. Windows and Mac users share the same servers, voice rooms, and streams.

|  |  |
| --- | --- |
| **🎧 Voice channels** | Low-latency Opus voice over WebRTC. Join and leave with one click; mute and deafen from anywhere in the app. |
| **🖥️ Screen and window sharing** | Share a whole display or a single window, hardware-encoded to H.264. Viewers can pop the stream out or go full screen. |
| **🔊 Game and app audio** | On Windows, include the sound of the window you share — only that app, not your notifications or music. |
| **💬 Text channel** | Every server has a persistent `#general` channel that keeps its history. |
| **🔑 Invites that stay private** | Send a direct invite code to let a friend in instantly, or publish a Server Code so people can *ask* to join and you approve each request. |
| **🪶 Light on your PC** | No Electron, no bundled browser. Background windows throttle their own UI refresh; voice and streams never do. |
| **🎨 Ivory and Espresso themes** | A warm light theme and a dark theme, or follow your system. |
| **🛡️ No passwords** | Your identity is created on first launch and stays on your computer. There is no account to sign up for. |

## Download

| Platform | Download | Install from the terminal |
| --- | --- | --- |
| **Windows 10 / 11** (x64) | [Catro-windows-x64.zip](https://github.com/brutalstein/catro/releases/latest/download/Catro-windows-x64.zip) | `irm https://github.com/brutalstein/catro/releases/latest/download/install-windows.ps1 \| iex` |
| **Mac with Apple silicon** (M1 and later) | [Catro-macos-arm64.zip](https://github.com/brutalstein/catro/releases/latest/download/Catro-macos-arm64.zip) | `curl -fsSL https://github.com/brutalstein/catro/releases/latest/download/install-macos.sh \| sh` |
| **Mac with Intel** | [Catro-macos-x64.zip](https://github.com/brutalstein/catro/releases/latest/download/Catro-macos-x64.zip) | same command — it picks your architecture |

### Windows

Unzip anywhere and run `Catro.exe`. Everything it needs, including the Windows App SDK and the
Visual C++ runtime, is inside the folder.

### Mac

Unzip and move **Catro.app** to Applications, or use the terminal command above: it installs
Catro to `~/Applications` after checking its SHA-256 checksum. Catro asks for Microphone access the
first time you join voice and for Screen Recording access the first time you share.

### Install on Windows with one command

Prefer an installer with a Start Menu entry? Run this in PowerShell (no administrator rights
needed):

```powershell
irm https://github.com/brutalstein/catro/releases/latest/download/install-windows.ps1 | iex
```

It downloads the latest release, verifies its SHA-256 checksum, installs to
`%LOCALAPPDATA%\Programs\Catro`, and adds Catro to the Start Menu. Run it again to update.

### Verify the download

Each release publishes a `.sha256` file next to every zip:

```powershell
(Get-FileHash .\Catro-windows-x64.zip -Algorithm SHA256).Hash
```

```bash
shasum -a 256 Catro-macos-arm64.zip
```

### First launch

Catro is not code-signed yet.

- **Windows:** SmartScreen may say it *protected your PC*. Choose **More info → Run anyway**.
- **Mac:** if you downloaded the zip in a browser, Gatekeeper may refuse to open it.
  Control-click **Catro.app**, choose **Open**, then **Open** again. The terminal installer does
  not need this step.

Signed and notarized builds are on the roadmap.

### System requirements

| | Windows | Mac |
| --- | --- | --- |
| **OS** | Windows 11, or Windows 10 version 2004 (build 19041) or newer, 64-bit | macOS 13 Ventura or newer, Apple silicon or Intel |
| **Screen sharing** | A GPU with a hardware H.264 encoder (NVIDIA, AMD, or Intel from the last several years) and current drivers | Built in on every supported Mac |
| **App audio in streams** | Windows build 20348 or newer | Not available yet — Mac streams share video, and your voice still works |
| **Network** | Any normal home connection. Catro relays through TURN automatically when a direct path is blocked. | Same |

## Getting started

1. **Open Catro.** Your own server is ready with `#general` and a `Voice` channel.
2. **Pick a name.** Open **Profile** in the left rail on Windows, or **Catro → Settings** on the
   Mac, to set the name your friends see.
3. **Invite friends.** Use the invite button next to your server name to share an invite code, or
   turn on a Server Code and approve join requests as they come in.
4. **Hop into Voice.** Click **Voice**, then **Join**. Up to five people can talk in one room.
5. **Share your screen.** In a voice channel, choose **Share screen** and pick a window or
   display. On Windows, you can also include that app's audio.

## Uninstall

**Mac:** quit Catro and move `Catro.app` (in `~/Applications` if you used the terminal installer)
to the Trash.

**Windows**, if you used the installer:

```powershell
irm https://github.com/brutalstein/catro/releases/latest/download/uninstall-windows.ps1 | iex
```

This removes the app and its Start Menu shortcut and keeps your identity and servers in
`%LOCALAPPDATA%\Catro`. To remove those too, download `uninstall-windows.ps1` and run it with
`-RemoveUserData`. If you used the portable zip, delete its folder.

## Status

Catro is in **early access**. Voice, screen sharing, and text chat work on both platforms, and
every release is built and tested in CI. Large-scale real-world testing across many networks and
machines is ongoing, so expect rough edges and please
[report them](https://github.com/brutalstein/catro/issues).

| Platform | Status |
| --- | --- |
| **Windows 10 / 11 x64** | ✅ Available — download above |
| **macOS 13+ (Apple silicon and Intel)** | ✅ Available — download above. App audio in streams is not available yet. |

**Roadmap:** code-signed and notarized builds, `winget` and Homebrew installs, automatic updates,
app audio on the Mac, more channels per server.

## Privacy and security

- Your identity is a random key generated on your computer. On Windows it is stored under
  `%LOCALAPPDATA%\Catro` and encrypted with DPAPI; on the Mac it is stored in your Application
  Support folder with owner-only permissions. No email, phone number, or password is collected.
- Voice and video travel over WebRTC, which encrypts media with DTLS-SRTP. The Catro service
  coordinates rooms and relays traffic only when a direct connection is impossible.
- Rooms use short-lived access tokens that are never written to disk.

Found a vulnerability? See [SECURITY.md](SECURITY.md).

<details>
<summary><b>For developers</b></summary>

### Build from source

Requirements: Windows 11, Visual Studio 2022 with the C++ desktop and WinUI workloads, CMake 3.28+.

```powershell
./scripts/bootstrap.ps1                 # check prerequisites (installs nothing)
./scripts/build.ps1 -Configuration Release
./scripts/test.ps1 -Configuration Release
./scripts/run.ps1                       # launch the app
./scripts/package-windows.ps1 -ServiceUrl https://your-catro-service   # portable zip
```

On a Mac (Xcode 16+, CMake 3.28+):

```bash
scripts/bootstrap.sh
scripts/build.sh Release && scripts/test.sh Release
CATRO_SERVICE_URL=https://your-catro-service scripts/package-macos.sh Release
```

See [docs/building.md](docs/building.md) and [docs/troubleshooting.md](docs/troubleshooting.md).

### Architecture

```text
WinUI 3 shell (C++/WinRT) ─┐
SwiftUI shell (macOS)     ─┴─ shared C++20 core ─ capability, audio, Opus voice, H.264 RTP, WebRTC rooms
                                     │
              platform/windows: WASAPI · Windows Graphics Capture · Media Foundation · D3D11
              platform/macos:   Core Audio · ScreenCaptureKit · VideoToolbox · Keychain
                                     │
              services/signaling (Go): identity, servers, invites, text channel, RTC/TURN provisioning
```

| Path | Contents |
| --- | --- |
| `core/` | Portable C++20: capabilities, audio engine, Opus voice, video/RTP, WebRTC room transport, community model |
| `platform/windows`, `platform/macos` | Native audio, capture, hardware encode/decode, presentation, local state |
| `apps/windows` | WinUI 3 desktop app |
| `apps/macos`, `apps/product-session/macos` | SwiftUI desktop app and its C++ session bridge |
| `apps/room-runtime`, `apps/voice-runtime`, `apps/screen-runtime` | Runtime boundaries for rooms, voice, and screen media |
| `services/signaling` | Go service: identity, membership, invites, messages, signaling, TURN credentials |
| `deploy/oracle-free` | Single-VM production deployment (Caddy + signaling + coturn) |
| `tools/` | Capability report, audio check, voice/video engineering harnesses |
| `tests/` | Unit, platform, UI-policy, installer, and lifecycle tests |

More reading: [capability system](docs/architecture/capability-system.md) ·
[personal state](docs/architecture/personal-state.md) ·
[production operations](docs/operations/oracle-free-production.md) ·
[decision records](docs/architecture/decisions) ·
[dependencies](DEPENDENCIES.md) · [contributing](CONTRIBUTING.md)

</details>
