# Contributing

## Before a change

1. Run `scripts/bootstrap` for your platform.
2. Read [the capability system architecture](docs/architecture/capability-system.md) and the
   decision records.

## Rules the codebase depends on

- **The core has no platform types.** `core/` includes only the standard library. Windows and
  Apple types stay in `platform/` and `apps/`.
- **Evidence is explicit.** Every fact is `known`, `unknown`, or `unavailable`, with its method
  (measured, advertised, probe-validated, inferred, or cached) and confidence. Never invent a default for a fact the OS did not report.
- **No vendor names as capability proxies.** Decide on what a device reports it can do, never on
  who made it.
- **Probes are passive.** A probe must not show a prompt, start capture, open an audio stream,
  create an encoder session, or change system state. It runs in the helper process within its
  budget.
- **Policy is deterministic.** The same snapshot, request, and policy version produce the same
  plan and trace. A change in behavior needs a policy version bump.
- **The schema is versioned.** Adding optional fields bumps the minor version of
  `catro.capabilities`. Any other change bumps the major version.
- **Claim only what was run.** A change note says which builds and tests ran on which machines.
  Do not describe unrun macOS or hardware checks as passed.

## Checks

```powershell
./scripts/build.ps1; ./scripts/test.ps1
```

```sh
scripts/build.sh && scripts/test.sh
```

CI runs Windows, macOS arm64, macOS Intel, and a sanitizer job on every push.

## Commits

Use conventional prefixes (`feat:`, `fix:`, `refactor:`, `docs:`, `build:`, `test:`). Keep each
commit buildable.
