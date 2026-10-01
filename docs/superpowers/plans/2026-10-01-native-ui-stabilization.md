# Native UI Stabilization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stabilize and modularize the Windows product shell now, establish the shared presentation contract, and preserve a clean handoff to the planned native macOS product shell.

**Architecture:** `apps/shell` gains immutable framework-neutral presentation state. WinUI code-behind remains the native edge but is split by product responsibility and applies explicit connection/action states. macOS later consumes the same semantics through its existing Objective-C++ bridge boundary after screen transport exists.

**Tech Stack:** C++20, Catch2, C++/WinRT, WinUI 3, SwiftUI/AppKit contract files, CMake, PowerShell build scripts.

**Spec:** `docs/superpowers/specs/2026-10-01-native-ui-stabilization-design.md`

## Global Constraints

- Preserve production-parity Task 4 and Task 5 interfaces and sequencing.
- Keep native WinUI 3 and SwiftUI/AppKit product shells.
- Do not run network, capture, codec, image generation, or polling work on UI threads.
- Do not add Electron, Qt, Avalonia, a browser runtime, blur, Mica, Acrylic, shadows, or continuous animation.
- Do not claim the upstream WinUI 3 UI Automation crash is fixed by application code.
- Preserve unrelated user changes and the copied icon assets byte-for-byte.

## Review Focus

- Directory configuration absent or network unreachable: local navigation remains usable and the reason is visible.
- Duplicate click while an async command is pending: one command runs and the busy state remains coherent.
- Narrow window or larger text: primary controls stay reachable without overlapping rails.
- Keyboard-only use: server, system, settings, channel, composer, and voice actions have deterministic order.
- External UI Automation crash: tests and documentation report the gate honestly rather than hiding it.

---

### Task 1: Add shared presentation state

**Files:**
- Create: `apps/shell/PresentationState.hpp`
- Create: `apps/shell/PresentationState.cpp`
- Modify: `apps/shell/CMakeLists.txt`
- Create: `tests/apps/presentation_state_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces: `catro::app::Availability`, `ActionState`, `ConnectionState`, and `WorkspaceSnapshot`.
- Consumes: existing shell destination/channel identifiers only.

- [ ] **Step 1: Write failing state tests**

Cover local-only, connecting, synchronized, failed, busy-action, and recovery transitions.

- [ ] **Step 2: Run focused test and verify RED**

Run: `cmake --build --preset windows-debug --target catro_presentation_state_test`

- [ ] **Step 3: Implement the minimum value/state model**

No service ownership, callbacks, or platform headers.

- [ ] **Step 4: Run focused and shell-model tests**

Run: `ctest --preset windows-debug -R "catro_(presentation_state|shell_model)" --output-on-failure`

### Task 2: Pin Windows interaction and organization policy

**Files:**
- Modify: `tests/apps/ui_policy_test.cpp`
- Modify: `tests/CMakeLists.txt`
- Create: `tests/ui/windows_keyboard_smoke.ps1`

**Interfaces:**
- Produces: deterministic source-policy checks and a non-UIA keyboard smoke harness.
- Consumes: Windows shell XAML/source and built `Catro.exe`.

- [ ] **Step 1: Write failing policy checks**

Require visible shell status, complete icon-button labels, no continuous mascot animation, focused
implementation files below the agreed size ceiling, and explicit busy/unavailable reasons.

- [ ] **Step 2: Run `catro_ui_policy` and verify RED**

Run: `ctest --preset windows-debug -R catro_ui_policy --output-on-failure`

- [ ] **Step 3: Add the keyboard smoke harness**

Launch the existing built app, use Win32 keyboard input only, verify the process remains alive, and
capture screenshots without walking UI Automation.

### Task 3: Stabilize MainWindow states and split directory behavior

**Files:**
- Modify: `apps/windows/Catro/MainWindow.xaml`
- Modify: `apps/windows/Catro/MainWindow.xaml.cpp`
- Modify: `apps/windows/Catro/MainWindow.xaml.h`
- Create: `apps/windows/Catro/MainWindow.Directory.cpp`
- Modify: `apps/windows/Catro/Catro.vcxproj`
- Modify: `apps/windows/Catro/Catro.vcxproj.filters`

**Interfaces:**
- Consumes: shared presentation state and existing directory client functions.
- Produces: visible shell connection state and unchanged directory behavior.

- [ ] **Step 1: Add a visible connection-status surface**

Represent connecting, synchronized, local-only, and failed states.

- [ ] **Step 2: Keep safe local actions interactive**

System/settings/server navigation remains usable; join explains missing prerequisites rather than
silently remaining disabled.

- [ ] **Step 3: Move directory methods to `MainWindow.Directory.cpp`**

Keep method signatures and behavior unchanged.

- [ ] **Step 4: Build the Windows shell**

Run: `./scripts/build.ps1 -Configuration Debug`

### Task 4: Split ServerView and apply explicit action states

**Files:**
- Modify: `apps/windows/Catro/Server/ServerView.xaml`
- Modify: `apps/windows/Catro/Server/ServerView.xaml.cpp`
- Modify: `apps/windows/Catro/Server/ServerView.xaml.h`
- Create: `apps/windows/Catro/Server/ServerView.Directory.cpp`
- Create: `apps/windows/Catro/Server/ServerView.Voice.cpp`
- Create: `apps/windows/Catro/Server/ServerView.Screen.cpp`
- Create: `apps/windows/Catro/Server/ServerView.State.cpp`
- Modify: `apps/windows/Catro/Catro.vcxproj`
- Modify: `apps/windows/Catro/Catro.vcxproj.filters`

**Interfaces:**
- Consumes: existing directory/room/voice/screen runtime APIs and shared action state.
- Produces: unchanged product commands with visible busy/unavailable/failure semantics.

- [ ] **Step 1: Add persistent status surfaces**

Text, voice, share, invite, and access controls expose concise reasons near the action.

- [ ] **Step 2: Move methods by responsibility**

No signature or runtime-contract changes.

- [ ] **Step 3: Remove continuous decorative animation**

Keep static vector branding and reduced-motion-safe visuals.

- [ ] **Step 4: Build and run focused UI/shell tests**

Run: `ctest --preset windows-debug -R "catro_(ui_policy|presentation_state|shell_model)" --output-on-failure`

### Task 5: Validate Windows interaction and visuals

**Files:**
- Modify: `docs/troubleshooting.md`
- Modify: `README.md`
- Create: `docs/validation/native-ui-stabilization.md`

**Interfaces:**
- Produces: reproducible evidence and an honest release gate.
- Consumes: built Windows shell and keyboard smoke harness.

- [ ] **Step 1: Run Debug build and focused/full tests**

Run: `./scripts/build.ps1 -Configuration Debug`

Run: `./scripts/test.ps1 -Configuration Debug`

- [ ] **Step 2: Launch and capture default/narrow screenshots**

Do not use external UI Automation until the upstream gate is green.

- [ ] **Step 3: Run keyboard smoke**

Verify server/system/settings/channel navigation without a process crash.

- [ ] **Step 4: Record UIA blocker and exact evidence**

Include fault module/code and the Microsoft issue; do not claim screen-reader readiness.

### Task 6: Refresh graph and preserve parity handoff

**Files:**
- Modify: `graphify-out/*`
- Modify: `.superpowers/sdd/2026-09-30-windows-macos-production-parity/progress.md`
- Create: `.superpowers/sdd/2026-10-01-native-ui-stabilization/progress.md`

**Interfaces:**
- Produces: current graph and explicit resume point.
- Consumes: completed Tasks 1–5.

- [ ] **Step 1: Run `graphify update .`**

- [ ] **Step 2: Record that production parity resumes at Task 4**

- [ ] **Step 3: Commit the interruption as focused logical slices**

