# Authenticated server member roster — implementation plan

Date: 2026-09-30

## Task 1 — Directory membership integrity and roster API

Files:

- `services/signaling/directory.go`
- `services/signaling/main.go`
- `services/signaling/main_test.go`

Steps:

1. Validate persisted membership references/roles/owner invariant in `openDirectory`.
2. Add bounded `memberDescriptor`.
3. Add membership-authorized `GET /v1/members?server_id=...`.
4. Sort owner first, then deterministic display-name/id order.
5. Test owner/member access, outsider rejection, deterministic ordering, and malformed-state startup
   rejection.

## Task 2 — Windows directory client

Files:

- `platform/windows/include/catro/platform/windows/directory_client.hpp`
- `platform/windows/src/directory_client.cpp`

Steps:

1. Add `DirectoryMember` and `DirectoryMembersResult`.
2. Add `list_directory_members`.
3. Strictly validate ids, display names, roles, duplicates, owner uniqueness/order, and count bound.

## Task 3 — Native member rail

Files:

- `apps/windows/Catro/Server/ServerView.xaml`
- `apps/windows/Catro/Server/ServerView.xaml.h`
- `apps/windows/Catro/Server/ServerView.xaml.cpp`
- `tests/apps/ui_policy_test.cpp`

Steps:

1. Replace the hard-coded member row with a virtualized `ListView`.
2. Preserve a local-state owner fallback before the directory session is available.
3. Add a five-second member refresh timer with one in-flight request.
4. Add generation protection across server switches.
5. Keep last valid roster on transient failure.
6. Update member count from successful roster snapshots.
7. Add UI regression checks for virtualization and bounded refresh.

## Task 4 — Documentation and verification

Files:

- `README.md`
- `docs/ui-foundation.md`
- `docs/AGENTS.md`
- `docs/validation/oracle-free-production-acceptance.md`

Steps:

1. State that the member rail is backed by authoritative membership snapshots.
2. Add real-network invite/roster convergence acceptance.
3. Run signaling test/race/vet/build.
4. Run Windows Release verification and full GitHub Actions matrix.
