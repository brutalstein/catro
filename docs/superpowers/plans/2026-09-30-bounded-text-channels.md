# Bounded persistent text channels — implementation plan

Date: 2026-09-30

## Task 1 — Server schema and message API

Files:

- `services/signaling/directory.go`
- `services/signaling/main.go`
- `services/signaling/main_test.go`

Steps:

1. Add server text-channel identity and bounded persisted message records.
2. Make old state load with an empty message map and initialized next sequence.
3. Add membership/channel helpers and atomic rollback around message persistence.
4. Add authenticated GET/POST `/v1/messages`.
5. Route the handler through the existing mutation limiter so only POST consumes write budget.
6. Test authorization, ordering, cursoring, retention, and persistence.

## Task 2 — Windows directory client

Files:

- `platform/windows/include/catro/platform/windows/directory_client.hpp`
- `platform/windows/src/directory_client.cpp`

Steps:

1. Add `text_channel_id` to `DirectoryServer`.
2. Sync both default channel ids.
3. Add bounded `DirectoryMessage` and page result types.
4. Add GET history and POST send operations.
5. Strictly validate all server JSON before returning it to the UI.

## Task 3 — Native Windows timeline and composer

Files:

- `apps/windows/Catro/Server/ServerView.xaml`
- `apps/windows/Catro/Server/ServerView.xaml.h`
- `apps/windows/Catro/Server/ServerView.xaml.cpp`

Steps:

1. Replace the text empty-state-only surface with a virtualized `ListView` plus empty overlay.
2. Enable composer only for an authenticated selected server.
3. Send on Enter with a single in-flight guard.
4. Poll active text channel once per second with one request in flight.
5. Track server generation/cursor so stale async completions cannot mutate a newly selected server.
6. Preserve composer text on send failure and retain existing timeline on refresh failure.

## Task 4 — Documentation and verification

Files:

- `README.md`
- `docs/ui-foundation.md`
- `docs/AGENTS.md`
- tests as required by compilation/runtime behavior

Steps:

1. Update product status from placeholder text channel to bounded persistent text.
2. Remove stale handoff wording that names completed screen-stream work as the immediate next step.
3. Run signaling tests/race/vet/build.
4. Run full GitHub Actions matrix and keep hardware claims separate from CI evidence.
