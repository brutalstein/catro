# Native UI stabilization validation

Date: October 1, 2026

## Scope

This record covers the bounded Windows native-shell stabilization interruption after
production-parity Task 3. It does not change the accepted Task 4–5 media interfaces or claim that
the macOS product workspace is complete.

The implementation:

- adds framework-neutral `Availability`, `ActionState`, `ConnectionState`, and
  `WorkspaceSnapshot` values in `apps/shell`;
- splits `MainWindow` directory work from lifecycle/navigation;
- splits `ServerView` into directory, voice, screen, and presentation-state translation units;
- removes the continuous decorative mascot animation;
- keeps local navigation usable while showing online connection, busy, unavailable, and failed
  reasons in persistent live status surfaces;
- keeps icon-only controls labelled and at least 36 logical pixels;
- adds a Win32 `SendInput` keyboard smoke harness that never imports or walks UI Automation.

## Build and automated evidence

Fresh evidence from October 1, 2026:

| Command | Result |
| --- | --- |
| `ctest --preset windows-debug -R catro_ui_policy --output-on-failure` | Pass, 1/1 |
| `ctest --preset windows-debug -R "catro_(presentation_state\|shell_model\|ui_policy)" --output-on-failure` | Pass, 3/3 |
| `./scripts/build.ps1 -Configuration Debug` | Pass; produced `out/apps/windows/x64/Debug/Catro.exe` |
| `./scripts/test.ps1 -Configuration Debug` | Pass, 49/49 |
| PowerShell AST parse of `tests/ui/windows_keyboard_smoke.ps1` | Pass |

The build emits one host Visual Studio warning because the machine's `LIB` environment contains a
missing ATL/MFC directory. The Catro project still compiles and links successfully.

The keyboard harness is source- and parser-validated. Its October 1, 2026 execution reached a live
Catro main-window handle but Windows rejected `SetForegroundWindow`; the harness stopped before
sending input or capturing screenshots. It must run in a clean interactive desktop session before
release acceptance.

## Final review fixes

The final review pass was covered RED→GREEN by `catro_ui_policy` and the full suite:

- pending invite and voice authorization completions carry monotonic generation tokens and verify
  both generation and server identity before touching the current server UI;
- same-server directory refreshes no longer reset active send/voice/share presentation states;
- send, voice join, and screen share expose busy, failed, and recovered states through the shared
  `WorkspaceSnapshot`;
- capture-source enumeration runs on a background thread and returns to the UI apartment before
  creating XAML controls;
- dynamically created server buttons receive stable automation names;
- the keyboard harness requires foreground acquisition and reports only liveness/screenshot
  capture, never semantic navigation success.

## Interaction and visual gate

The source policy verifies:

- visible `ShellStatusText` and `OnlineStatusText` live regions;
- explicit busy text for Server Code lookup, access requests, invite joins, and invite creation;
- no continuous compositor animation;
- responsibility-based source files below the agreed line ceilings;
- labelled icon buttons and minimum interaction targets;
- a keyboard harness based on `SendInput`, process liveness, and screenshots without
  `System.Windows.Automation` or `IUIAutomation`.

Visual acceptance is **not complete** on this host. Earlier default screenshot attempts captured
an unrelated desktop surface, a direct `PrintWindow` attempt returned a black frame, and the final
keyboard run could not acquire foreground input ownership. None is valid Catro UI evidence. A
narrow-window screenshot was therefore not accepted.

## External UI Automation blocker

The Windows shell has a reproducible external accessibility-tree crash:

- October 1, 2026 04:43:03: `Microsoft.UI.Xaml.dll`, exception `0xc000027b`, WER
  `8001010e` (`RPC_E_WRONG_THREAD`);
- October 1, 2026 05:17:49: `Catro.exe` application error `0xc000027b`; WER at 05:17:53 named
  `combase.dll` and preserved `8001010e`;
- October 1, 2026 05:19:10: the no-UIA screenshot attempt again ended with `0xc000027b`; WER at
  05:19:13 again named `combase.dll` and preserved `8001010e`.

These failures were first attributed to the upstream Windows App SDK issue
[microsoft-ui-xaml#11139](https://github.com/microsoft/microsoft-ui-xaml/issues/11139).

**Resolved October 2, 2026 — the cause was in Catro.** Dump analysis showed:

- The XAML-generated `wWinMain` called `winrt::init_apartment()`, putting the UI thread in the
  MTA. A `winrt::apartment_context` captured there never marshalled back, so
  `MainWindow::BeginDirectoryBootstrap` touched XAML from the thread pool and its
  `fire_and_forget` coroutine terminated the process 7–23 seconds after every launch, with or
  without UI Automation clients.
- With an MTA UI thread, a cross-process UI Automation raw-view walk failed at the XAML island
  root (`E_UNEXPECTED`) and could end in an access violation inside `Microsoft.UI.Xaml.dll`.

Fixes: coroutines resume through the UI thread's `DispatcherQueue` (`UiThread` in `pch.h`), and
Catro supplies its own `wWinMain` (`DISABLE_XAML_GENERATED_MAIN`) with a single-threaded
apartment. Evidence on this host: three launches stayed alive for 45 seconds (previously all
died), and a cross-process `System.Windows.Automation` raw-view walk reached all 57 elements twice
without a crash (previously it failed at element 8). `catro_ui_policy` pins both fixes.

## Release status

| Gate | Status |
| --- | --- |
| Presentation-state tests | Pass |
| Windows source policy | Pass |
| Windows Debug build | Pass |
| Full Windows test script | Pass, 49/49 |
| Keyboard smoke execution | Blocked: host rejected foreground acquisition |
| Default and narrow visual acceptance | Blocked/pending clean interactive desktop run |
| Narrator/UIA | Cross-process UIA walk passes; live Narrator session still pending |

Production-parity work resumes at Task 4 only after this interruption is handed off with these
open gates unchanged. No Task 4–5 runtime interface was modified.
