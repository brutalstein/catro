# Capability system

This document describes the code as it is built. The full contract is in the
[design spec](../superpowers/specs/2026-09-26-native-foundation-capabilities-design.md).

## Data flow

```text
capability service (platform/*)
  |  starts one helper process per probe family, each with its own hard budget
  v
catro-capability-probe --probe <os>.<family>.v1   -> one canonical JSON fragment on stdout
  |
  v
probe coordinator (core)   parses and checks fragments, turns failures into issues
  |
  v
immutable CapabilitySnapshot (generation N) + ChangeSet against generation N-1
  |
  +--> validator             structural invariants
  +--> derive_media_plan     deterministic policy, versioned
  +--> reporting             canonical JSON, human report, presented rows
          |
          v
     catro::app::build_diagnostics  (apps/diagnostics, shared by both shells)
          |
          +--> WinUI shell (C++/WinRT, links the C++ types directly)
          +--> Objective-C++ bridge -> SwiftUI shell (immutable value objects only)
```

## Boundaries

- `core/capabilities` and `core/reporting` are portable C++20. They use only the standard library
  and never include a platform header.
- A platform layer turns native results into core values at its edge. Both translations
  (`platform/windows/src/windows_translation.*`, `platform/macos/src/macos_translation.*`) are
  plain C++ records and rules. They build on every platform, so their tests run anywhere.
- The shells only arrange the view model. They never interpret evidence.

## Evidence

Each fact carries its knowledge (`known`, `unknown`, or `unavailable`), its method, and its
confidence. An absent fact always has an issue code that explains why, for example `not_reported`,
`timeout`, or `relationship_unprovable`. A fact is never filled with a guess. Relationships such as
"this encoder can read this GPU's surfaces" are claimed only when the platform proves them.
Otherwise they are `unknown`.

## Refresh

Each service watches OS change signals: display, power, thermal, session, memory pressure, and
audio device changes. It debounces them into one refresh and publishes a new generation with a
classified change set. The diagnostics refresh button asks for a full refresh.

## Versioning

- Schema: `catro.capabilities` 1.0. A reader accepts the same major version with a minor version
  at or below its own.
- Policy: 1.0.0. A plan records the policy version that produced it, and a different policy
  version is rejected.
- Probe IDs carry their own version (`windows.encoders.v1`).
