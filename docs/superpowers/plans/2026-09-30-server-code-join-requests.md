# Server Code discovery and approval-based join requests — implementation plan

Date: 2026-09-30

## Task 1 — Public Server Code and persisted request model

Files:

- `services/signaling/directory.go`
- `services/signaling/main_test.go`

Implement:

- 80-bit formatted Server Code generation/canonicalization;
- persistent `public_code` on servers;
- migration for existing servers;
- persistent bounded join-request model;
- pruning and integrity validation.

## Task 2 — Lookup and join-request API

Files:

- `services/signaling/directory.go`
- `services/signaling/main.go`
- `services/signaling/rate_limit.go`
- `services/signaling/rate_limit_test.go`
- `services/signaling/main_test.go`

Implement:

- dedicated discovery lookup limiter;
- exact `/v1/server-lookup`;
- create/list/decision/cancel join-request endpoints;
- atomic approval -> membership transition;
- metrics without identifiers.

## Task 3 — Windows network boundary

Files:

- `platform/windows/include/catro/platform/windows/directory_client.hpp`
- `platform/windows/src/directory_client.cpp`

Implement strict models and functions for:

- server lookup preview;
- create join request;
- outgoing request status;
- owner pending queue;
- approve/reject;
- cancel.

## Task 4 — Add Server flow

Files:

- `apps/windows/Catro/MainWindow.xaml`
- `apps/windows/Catro/MainWindow.xaml.h`
- `apps/windows/Catro/MainWindow.xaml.cpp`

Implement:

- change tooltip/semantics from invite-only to Add Server;
- exact Server Code lookup preview;
- optional request note;
- preserve direct invite path;
- bounded outgoing status timer;
- approved request refreshes authoritative server list/rail.

## Task 5 — Owner Access surface

Files:

- `apps/windows/Catro/Server/ServerView.xaml`
- `apps/windows/Catro/Server/ServerView.xaml.h`
- `apps/windows/Catro/Server/ServerView.xaml.cpp`
- `tests/apps/ui_policy_test.cpp`

Implement:

- owner-only Access button;
- selectable Server Code;
- pending request count;
- virtualized request list;
- approve/reject;
- five-second single-flight owner queue refresh.

## Task 6 — Documentation and acceptance

Files:

- `README.md`
- `docs/ui-foundation.md`
- `docs/AGENTS.md`
- `services/signaling/README.md`
- `docs/validation/oracle-free-production-acceptance.md`

Update architecture/status and real-network acceptance.

## Verification

- Go tests;
- Go race tests;
- Go vet;
- Go build;
- Windows Release verification;
- UI policy;
- macOS matrix;
- sanitizers;
- deployment validation.
