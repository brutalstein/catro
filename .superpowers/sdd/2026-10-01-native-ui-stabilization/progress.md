# SDD ledger — plan: docs/superpowers/plans/2026-10-01-native-ui-stabilization.md

Pre-flight: Task 1 produces shared presentation values consumed by Tasks 3–4; names and ownership are consistent.
Pre-flight: Task 2 policy checks consume the file split and visible state produced by Tasks 3–4; checks will be introduced RED before implementation.
Pre-flight: Task 5 consumes the built shell and smoke harness from Tasks 2–4; no interface conflict.
Ruling: external WinUI UI Automation RPC_E_WRONG_THREAD is an upstream release blocker, not an application exception to catch — preserve WinUI for this interruption and do not claim screen-reader readiness — cost if wrong: a later framework migration may still be required.
Ruling: Superpowers helper scripts are not directly executable on this Windows host; maintain the required ledger and per-task verification manually — cost if wrong: review packaging must be assembled manually.
Task 1: Ruling: `windows-debug` is a build/test preset while configure uses `windows-msvc`; configure with `cmake --preset windows-msvc`, then use the planned build preset — cost if wrong: only local command selection changes.
Task 1: complete (commit 38ad4ea, tests: ctest --preset windows-debug -R catro_(presentation_state|shell_model) -> 2/2 pass)
Task 2: complete (tests: catro_ui_policy RED on missing visible busy states, keyboard harness, and control targets; GREEN 1/1; PowerShell parser OK)
Task 3: complete (tests: ./scripts/build.ps1 -Configuration Debug -> Catro.exe produced)
Task 4: complete (tests: ctest --preset windows-debug -R catro_(presentation_state|shell_model|ui_policy) -> 3/3 pass; Debug WinUI build pass)
Task 5: build/test slice complete (tests: ./scripts/test.ps1 -Configuration Debug -> 49/49 pass)
Task 5: blocked gate: external UI/client observation reproduced 0xc000027b with WER 8001010e at 2026-10-01 05:17 and 05:19; keyboard smoke execution and accepted default/narrow screenshots remain pending on a clean desktop session.
Task 5: Ruling: invalid desktop and black PrintWindow captures are not visual acceptance evidence — document the blocker instead of claiming visual completion — cost if wrong: layout regressions may remain until a clean-host screenshot run.
Final: fixed stale invite completion after server switches — generation/server guards RED→GREEN, suite 49/49.
Final: fixed stale voice authorization across A→B→A switches — generation/server guards RED→GREEN, suite 49/49.
Final: fixed UI-thread capture-source enumeration — background-hop policy RED→GREEN, Debug WinUI build pass.
Final: fixed non-authoritative send/voice/share action presentation — shared-state policy RED→GREEN, suite 49/49.
Final: fixed missing dynamic server automation names — source policy RED→GREEN, suite 49/49.
Final: fixed keyboard harness overclaim — foreground check and evidence wording RED→GREEN; parser 0 errors.
Final: keyboard smoke attempted on October 1, 2026; Catro created a main-window handle, but the host rejected `SetForegroundWindow`, so no input or screenshots were accepted.
Final: implementation committed as `f1c3d7e`; production-parity resume point remains Task 4.
