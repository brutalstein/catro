# 0002: Isolated, passive, budgeted probes

Status: accepted, 2026-09-26

## Context

GPU drivers, encoder enumeration, and audio HALs can hang, crash, or take seconds on some
machines. Some capture and audio APIs show permission prompts or change device state the first
time they are used. Capability discovery runs at every start and on every change signal. It must
never do either.

## Decision

- Each probe family runs in a separate helper process (`catro-capability-probe --probe <id>`).
  The helper prints one canonical JSON fragment.
- The service gives each probe a hard budget (500 to 1500 ms). At the budget it kills the helper:
  on Windows through a kill-on-close job object, and on macOS by process group. A late probe
  becomes a `timeout` issue, not a stall.
- The first snapshot is published within 2000 ms, with whatever evidence has arrived by then.
- The service treats helper output as untrusted input. The output has a 1 MiB cap, strict parsing,
  and a probe ID check.
- Probes are passive. They enumerate and read properties only. No capture starts, no audio stream
  opens, no encoder session is created, and no permission is requested. On macOS, screen-capture
  permission uses the non-prompting preflight check.

## Consequences

- A crashing or hanging driver costs one probe family's evidence, not the application.
- Some facts can only be proven by active tests, such as real encode throughput or zero-copy
  transfer. They stay `unknown` until a later milestone adds explicit, user-initiated validation.
- Process start-up adds tens of milliseconds per probe. The probes run concurrently.
