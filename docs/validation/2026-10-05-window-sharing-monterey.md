# Window sharing and Monterey continuation

Target: macOS Monterey 12.6.7 on Intel Core i5, alongside Windows. Monterey uses
screen/window video and microphone voice without an additional audio driver.
Application/system audio capture remains available on macOS 13 and newer.

The last Claude Code session stopped during the CS2 capture investigation. Its
unfinished source-enumeration fix and capture-check source selector were recovered
from the local checkout and completed in this branch.

## Behavior

- Windows window sources, including CS2 and full-screen games, always use WGC.
  Explicitly requesting desktop duplication for a window cannot redirect capture
  to the monitor. Display sources retain desktop duplication.
- Minimized windows use their restored geometry in the chooser. Game/minimized
  window shares allow 30 seconds for the selected window to resume rendering.
- Source enumeration reports physical pixels regardless of the Windows caller's
  DPI context and restores that context before returning.
- macOS translates display/window point dimensions to backing pixels before
  choosing quality and configuring ScreenCaptureKit. Window filters remain
  desktop-independent and attached to the selected window.
- Display pixel geometry preserves 90/270-degree rotation; compile-time checks
  cover landscape/portrait at 1x and 2x pixel dimensions.
- Intel integrated graphics retain 720p60 and 1080p30 choices. Existing bounded,
  aspect-preserving video geometry remains in use.

## Local evidence, 2026-10-05

- Before the fix, the source-size regression returned 1707x1067 for a 2560x1600
  display; the game backend assertion also failed.
- MSVC Release core/tools/tests build succeeded. CTest: **56/56 passed**.
- Native Windows capture tests: **35 assertions across 6 cases passed**. The
  overlapping-window test captures two visibly different native windows, verifies
  the selected source uses WGC even when DXGI is requested, and checks that the
  last/new selected frame does not become the covering window. A static occluded
  source may retain its last frame rather than generate new frames.
- `catro-capture-check --list` reports the local display as **2560x1600**.
- `bash -n` passed for the macOS packaging script and installer tests.
- Full Windows GUI build, ZIP hash verification, and packaged app launch/quit
  smoke test passed.
- Graphify was refreshed with the AST-only `graphify update .` command.

## Remaining device evidence

This Windows host cannot execute the macOS application. Hosted macOS CI checks
compilation and tests on Intel/Apple Silicon with the 12.3 deployment target;
it does not prove operation on Monterey 12.6.7 or this specific Core i5.
On that Mac, verify microphone permission/voice, display and window sharing,
window isolation while changing apps/Spaces, Retina resolution, remote viewing,
and reconnect/full-screen behavior. CS2 was not running during this continuation,
so its live-game acceptance remains separate from the native window regression.
