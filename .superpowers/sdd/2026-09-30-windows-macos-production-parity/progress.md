# SDD ledger — plan: docs/superpowers/plans/2026-09-30-windows-macos-production-parity.md

Pre-flight: Windows has production directory/voice/screen behavior; macOS has diagnostics/local-audio
preview only. Installer CI success is not parity evidence.

Ruling: use one portable RTC/room/voice/screen-transport core with native Windows and macOS
capture/codec/presentation edges — cost if wrong: duplicated platform runtime logic would drift and
double the E2E surface.

Ruling: preserve native WinUI 3 and SwiftUI/AppKit shells — cost if wrong: a cross-platform UI
runtime would add latency, memory, accessibility, and platform-integration risk.

Ruling: release automation may be completed and rehearsed but public tag/release remains blocked
until real-machine cross-platform media, accessibility, Oracle, and performance gates pass — cost
if wrong: CI could publish a package whose macOS media path or production network path was never
observed.

Ruling: user authorized autonomous execution and per-slice pushes; implement in
`work/release-installers`, push `HEAD:feature/two-client-screen-stream`, and fast-forward the IDE
checkout after each completed logical slice.

