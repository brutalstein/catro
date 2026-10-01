# Native UI stabilization and modular product-shell design

Date: 2026-10-01

## 1. Goal

Pause the Windows/macOS production-parity plan after its completed production-voice slice, stabilize
the native Windows product shell, and establish the presentation contract that the later macOS
product shell will consume without disturbing the portable media work scheduled next.

Success means:

- every visible action has an observable enabled, busy, unavailable, or failed state;
- unavailable network/media actions explain why without requiring a disabled control tooltip;
- Windows UI code is organized by responsibility rather than one multi-thousand-line code-behind;
- Windows and macOS consume the same immutable product snapshots and command semantics while
  retaining native WinUI 3 and SwiftUI/AppKit views;
- network, capture, codec, image generation, and polling work remain off UI threads;
- keyboard, focus, text scaling, high contrast, reduced motion, and assistive-technology behavior
  are release gates;
- the existing production-parity Task 4 and Task 5 interfaces remain unchanged.

## 2. Verified baseline

At the interruption point:

- production-parity Tasks 1–3 are represented by the commits through `e4e5a1b`;
- Task 4 has not started;
- Windows has the product server/text/voice/share shell;
- macOS has diagnostics and local-audio views plus production directory and voice runtime adapters,
  but no product workspace;
- `ServerView.xaml.cpp` is approximately 2,748 lines, `MainWindow.xaml.cpp` approximately 933
  lines, and `ServerView.xaml` approximately 637 lines;
- many product controls begin disabled and expose their reason only through subtle nearby text or a
  tooltip;
- the current UI policy tests inspect source tokens but do not prove product interaction states;
- walking or activating the WinUI tree through external UI Automation reproduces a
  `Microsoft.UI.Xaml.dll` crash with `RPC_E_WRONG_THREAD`/`8001010e`.

The UI Automation crash also reproduces against minimal WinUI 3 content and matches the upstream
Windows App SDK failure tracked by Microsoft. It has no safe application-level exception boundary.
The application must not claim screen-reader readiness while that external gate remains red.

## 3. Sequencing

This interruption is a bounded stabilization gate:

1. preserve the completed production-parity state;
2. introduce presentation-state contracts and focused tests;
3. stabilize and modularize the Windows shell;
4. record deterministic visual and keyboard evidence that does not depend on the crashing external
   UI Automation path;
5. resume production-parity Tasks 4–5 unchanged;
6. implement the macOS product shell from the same presentation contract in the existing Task 6;
7. complete cross-platform source-picker and accessibility parity in Task 7.

The interruption does not pull the macOS screen-sharing UI ahead of its Task 5 transport
dependency. Creating placeholder media adapters now would introduce rework and would weaken the
accepted one-core/two-native-edges architecture.

## 4. Presentation boundary

`apps/shell` owns framework-neutral presentation state only:

- `Availability`: ready, busy, unavailable, failed;
- `ActionState`: availability plus concise user-visible reason;
- `ConnectionState`: local-only, connecting, synchronized, failed;
- `WorkspaceSnapshot`: active destination/channel and action states needed by the native shells.

The contract contains values, not service objects. It does not perform networking, persistence,
media work, polling, or platform calls.

Native views consume immutable snapshots and emit narrow commands:

- open server/system/settings;
- select text/voice channel;
- request server join/invite/access action;
- send text;
- join/leave/mute/deafen voice;
- start/stop share and watch/leave stream.

Windows initially adapts its existing state into this contract. The macOS product bridge later
publishes the same values as immutable Objective-C value objects delivered on the main actor.

## 5. Windows organization

The existing `ServerView` remains the generated WinRT/XAML owner, but implementation is split into
focused translation units:

- `ServerView.xaml.cpp`: lifecycle, channel selection, local layout, and shared helpers;
- `ServerView.Directory.cpp`: directory session, members, invites, access requests, and text;
- `ServerView.Voice.cpp`: voice join/leave/mute/deafen and voice snapshots;
- `ServerView.Screen.cpp`: source selection, ownership, capture, stream presentation, and pop-out;
- `ServerView.State.cpp`: application of immutable action/connection state to controls.

`MainWindow` is split into:

- `MainWindow.xaml.cpp`: window lifecycle, navigation, title bar, and page ownership;
- `MainWindow.Directory.cpp`: bootstrap, server lookup/join, server refresh, and directory rail.

No additional runtime abstraction is introduced merely to split files. Existing methods and
service calls remain authoritative.

## 6. Interaction-state rules

- Navigation controls that can always change local pages remain enabled.
- A network-dependent control is not silently disabled when pressing it can safely explain the
  missing prerequisite.
- Long-running commands expose a visible busy label and reject duplicate invocation.
- Disabled media controls have persistent adjacent status text; tooltip-only explanations are not
  accepted.
- Offline/local-only mode remains usable for channel navigation, diagnostics, and settings.
- Error copy names the failed capability and the recovery action when one exists.
- Every icon-only button has a stable automation name and tooltip.
- Minimum interactive target size is 36 logical pixels; primary controls use at least 40.
- Focus order follows visual order and all primary navigation/actions are keyboard reachable.

## 7. Visual and performance system

Keep the accepted warm-neutral Catro palette and dense gaming-chat layout, but improve hierarchy:

- one visible shell-status strip for connecting/local-only/failed state;
- stronger selected/focus/hover affordances without blur, Mica, Acrylic, shadows, or continuous
  animation;
- no bitmap decoration in product views;
- member/message lists remain virtualized and bounded;
- no per-frame UI callbacks;
- the decorative mascot animation is removed because it is continuous motion with no product
  value and conflicts with reduced-motion simplicity.

## 8. Windows UI Automation release blocker

The project stays on the current supported Windows App SDK line during this interruption. Random
package downgrades or private binary replacement are not acceptable.

The gate is:

- if a Microsoft servicing update fixes external UI Automation, update within the supported line
  and rerun Narrator/UIA;
- if the crash remains when product parity reaches final accessibility acceptance, the Windows
  shell must migrate before release to a supported UI stack whose external accessibility tree is
  stable;
- until one of those conditions is green, documentation and release status must state that Windows
  screen-reader support is blocked.

Application interaction improvements may proceed, but they must not be represented as a fix for
the upstream crash.

## 9. macOS product shell

After Task 5, Task 6 implements:

- `NavigationSplitView`-based server/channel/content/member workspace;
- native SwiftUI lists and controls with AppKit-backed media presentation where required;
- the shared availability, reason, busy, and failure semantics;
- keyboard commands and focus behavior appropriate to macOS;
- reduced-motion and accessibility labels/help;
- equivalent product capability rather than pixel-identical Windows controls.

The SwiftUI files remain small and responsibility-based as already planned:
`AppModel`, `ServerWorkspace`, `ChannelSidebar`, `MemberSidebar`, `VoiceControls`, `SourcePicker`,
`StreamViewer`, and `SettingsView`.

## 10. Verification

The interruption requires:

- pure C++ tests for presentation-state transitions and reasons;
- source-policy tests for file-size boundaries, automation names, focus order declarations,
  visible status surfaces, virtualization, and prohibited effects;
- Windows Debug build and focused tests;
- clean no-network startup;
- screenshot inspection at default, narrow, and high-text-scale layouts where the host permits;
- keyboard navigation smoke evidence without walking the crashing UI Automation tree;
- fresh graph update.

Later Task 6–7 acceptance still requires native arm64/x64 macOS builds, screenshots, VoiceOver,
Narrator/UIA after the upstream/framework gate is green, and real-machine interaction records.

## 11. Non-goals

- no Electron, Qt, Avalonia, embedded browser, or shared cross-platform UI runtime;
- no rewrite of directory, voice, room, screen, or signaling behavior;
- no speculative channel creation, DMs, reactions, attachments, or presence;
- no placeholder macOS screen implementation before Task 5;
- no claim that an upstream UI Automation crash is solved by application refactoring.

## 12. Definition of done for the interruption

1. Shared presentation state and tests exist.
2. Windows connection/action states are visible and deterministic.
3. The largest Windows UI implementation files are split by responsibility.
4. Existing shell behavior and production runtime contracts pass regression tests.
5. The Windows app builds and launches with no network.
6. Visual evidence confirms the updated shell hierarchy and status communication.
7. The external UI Automation failure is accurately gated and documented.
8. The production-parity plan can resume at Task 4 without interface churn.
