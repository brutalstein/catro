# Oracle Free Production Rooms Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deploy Catro on one Oracle Cloud Always Free ARM64 VM and support authenticated, dynamic two-to-five-person voice rooms with one signaling-authorized screen publisher.

**Architecture:** Keep the existing WebRTC peer mesh and local Opus mixing. Extend the Go control plane with a five-person provisioning contract, authoritative screen ownership, bounded API rate limiting, and deployment metrics; propagate that state through the C++ RTC transport, room runtime C ABI, and Windows shell. Package Caddy, the local signaling image, and coturn in one pinned Docker Compose deployment.

**Tech Stack:** Go 1.27.1, C++20, libdatachannel, WinUI 3/C++/WinRT, Docker Compose, Caddy 2.11.4, coturn 4.18.0-r0, Bash, PowerShell, CMake/CTest.

**Spec:** `docs/superpowers/specs/2026-09-29-oracle-free-production-rooms-design.md`

## Global Constraints

- A room's configured total capacity is an integer from 2 through 5; production default is 5.
- Voice remains a client-side WebRTC mesh with local per-stream Opus decode and mixing.
- At most one authenticated peer owns screen video and selected-window stream audio in a room.
- Receivers drop screen video and stream audio from any peer other than the signaling-authorized owner.
- Public signaling is HTTPS/WSS; plaintext signaling is permitted only inside the private deployment network or explicit engineering mode.
- TURN credentials remain short lived and derived from a server-only coturn REST secret.
- The deployment must fit one Oracle Always Free ARM64 VM and must not add paid managed services.
- No SFU, database, media transcoding, recording, Kubernetes, unattended updater, or Oracle API credentials.
- Every completed task is committed and pushed to `origin/feature/two-client-screen-stream`.
- Never stage `graphify-out/**`, `.winapp/`, `apps/windows/Catro/Assets/`, `catro-crash-log.txt`, or other unrelated local files.

## Review Focus

- **Simultaneous fifth/sixth joins:** exactly five authenticated sockets remain joined and the loser receives capacity rejection — Task 1 tests this.
- **Competing screen claims:** only one claimant becomes owner and every peer converges on the same owner — Task 2 tests this.
- **Stale or malicious media sender:** video and stream audio from a non-owner are rejected after owner change or release — Task 3 tests this.
- **Forged proxy headers:** public callers cannot evade rate limits by changing `X-Forwarded-For`; only the private proxy path is trusted — Task 5 tests this.
- **Interrupted update/restore:** failed health checks retain the previous service and restore refuses to overwrite live state — Task 6 tests this.

---

### Task 1: Bound Room Capacity and Provision It to Clients

**Files:**
- Modify: `services/signaling/main.go`
- Modify: `services/signaling/directory.go`
- Modify: `services/signaling/main_test.go`

**Interfaces:**
- Consumes: existing `service.maxRoomPeers`, `room.add`, and authenticated `/v1/rtc-token`.
- Produces: JSON field `max_room_peers`; `directory.maxRoomPeers int`; accepted startup capacity `2..5`.

- [ ] **Step 1: Write failing Go tests for production capacity**

Add tests that assert:

```go
func TestRoomCapacityRejectsSixthPeer(t *testing.T)
func TestRoomCapacitySlotReturnsAfterDisconnect(t *testing.T)
func TestRTCProvisioningIncludesRoomCapacity(t *testing.T)
```

The provisioning assertion is exactly `"max_room_peers": 5`. The room tests use five distinct authenticated
peer IDs, reject the sixth with `"room capacity reached"`, remove one peer, then accept a replacement.

- [ ] **Step 2: Run the focused Go tests and verify failure**

Run:

```powershell
Push-Location services/signaling
go test ./... -run 'TestRoomCapacity|TestRTCProvisioningIncludesRoomCapacity'
Pop-Location
```

Expected: FAIL because provisioning does not carry capacity and the test fixture still assumes the old default.

- [ ] **Step 3: Implement the bounded configuration contract**

In `services/signaling/main.go`:

- change `defaultMaxRoom` from `16` to `5`;
- introduce `maximumProductionRoom = 5`;
- validate `--max-room-peers` in the inclusive range `2..5`;
- pass the selected value into `directory.setRTCProvisioning`.

Change `directory.setRTCProvisioning` to accept `maxRoomPeers int`, store it on `directory`, and include
`"max_room_peers": maxRoomPeers` in `handleRTCToken`.

- [ ] **Step 4: Run signaling tests**

Run:

```powershell
Push-Location services/signaling
go test ./...
go vet ./...
Pop-Location
```

Expected: PASS.

- [ ] **Step 5: Commit and push Task 1**

```powershell
git add -- services/signaling/main.go services/signaling/directory.go services/signaling/main_test.go
git diff --cached --check
git commit -m "feat: cap production voice rooms"
git push origin feature/two-client-screen-stream
```

---

### Task 2: Add Authoritative Screen Ownership to Signaling

**Files:**
- Modify: `services/signaling/main.go`
- Modify: `services/signaling/main_test.go`

**Interfaces:**
- Consumes: authenticated room WebSocket and existing `message` envelope.
- Produces: client messages `screen_claim` and `screen_release`; server messages `screen_state` and
  `screen_busy`; joined field `screen_owner`; room methods
  `claimScreen(peerID string) (owner string, acquired bool)`,
  `releaseScreen(peerID string) bool`, and `remove(peerID string) (removed bool, releasedScreen bool)`.

- [ ] **Step 1: Write failing signaling protocol tests**

Add:

```go
func TestScreenClaimIsExclusiveAndIdempotent(t *testing.T)
func TestScreenOwnerReleaseAndDisconnectBroadcastState(t *testing.T)
func TestLateJoinReceivesCurrentScreenOwner(t *testing.T)
func TestNonOwnerCannotReleaseScreen(t *testing.T)
```

Tests open authenticated WebSockets through existing helpers. They assert:

- first claim broadcasts `screen_state` with claimant;
- repeated owner claim is harmless;
- competing claim receives `screen_busy` with current owner;
- non-owner release changes nothing;
- owner release/disconnect broadcasts an empty owner;
- joined response carries the current owner.

- [ ] **Step 2: Run focused tests and verify failure**

```powershell
Push-Location services/signaling
go test ./... -run 'TestScreen'
Pop-Location
```

Expected: FAIL because room control messages are not implemented.

- [ ] **Step 3: Implement room ownership state**

Add `screenOwner string` to `room` and `ScreenOwner string 'json:"screen_owner,omitempty"'` to `message`.
Implement the three room methods under `room.mu`; ownership identity always comes from `client.id`.

In `websocket`:

- include `ScreenOwner` in `joined`;
- accept only `signal`, `screen_claim`, and `screen_release`;
- keep offer/answer/candidate validation unchanged;
- on successful claim/release broadcast `screen_state` to every room peer;
- on busy claim write `screen_busy` only to the claimant;
- when the owner disconnects, clear ownership and broadcast empty `screen_state`.

Room control messages use the existing signal rate window and size limit.

- [ ] **Step 4: Run signaling tests and race detector**

```powershell
Push-Location services/signaling
go test ./...
go test -race ./...
go vet ./...
Pop-Location
```

Expected: PASS.

- [ ] **Step 5: Commit and push Task 2**

```powershell
git add -- services/signaling/main.go services/signaling/main_test.go
git diff --cached --check
git commit -m "feat: arbitrate room screen sharing"
git push origin feature/two-client-screen-stream
```

---

### Task 3: Enforce Capacity and Screen Ownership in RTC and Room Runtime

**Files:**
- Create: `core/rtc/include/catro/rtc/room_media_policy.hpp`
- Modify: `core/rtc/include/catro/rtc/room_mesh_transport.hpp`
- Modify: `core/rtc/src/room_mesh_transport.cpp`
- Modify: `apps/room-runtime/windows/include/catro/room_runtime.h`
- Modify: `apps/room-runtime/windows/src/room_runtime.cpp`
- Modify: `tests/windows/rtc_room_transport_test.cpp`

**Interfaces:**
- Consumes: `joined.screen_owner`, `screen_state`, `screen_busy`, and `RoomMeshConfig.max_peers`.
- Produces:
  - `bool screen_media_allowed(std::string_view owner, std::string_view sender) noexcept`;
  - `RoomTransportCallbacks::on_screen_owner(std::string_view)`;
  - `bool RoomMeshTransport::claim_screen() noexcept`;
  - `void RoomMeshTransport::release_screen() noexcept`;
  - `std::string RoomMeshTransport::screen_owner() const`;
  - `CatroRoomRuntimeConfig.max_remote_peers`;
  - `CatroRoomRuntimeSnapshot.screen_owner[129]`;
  - `catro_room_runtime_claim_screen` and `catro_room_runtime_release_screen`.

- [ ] **Step 1: Write failing RTC policy/configuration tests**

Add tests:

```cpp
TEST_CASE("RTC room transport bounds remote peers to four");
TEST_CASE("RTC room screen media accepts only the current owner");
```

Assertions:

- `max_peers == 0` and `max_peers > 4` reject production configuration;
- owner `"peer-a"` accepts `"peer-a"` and rejects `"peer-b"`;
- empty owner rejects every sender.

- [ ] **Step 2: Run focused C++ test and verify failure**

```powershell
cmake --build --preset windows-msvc-release --target catro_windows_rtc_room_test
ctest --test-dir out/build/windows-msvc -C Release -R catro_windows_rtc_room --output-on-failure
```

Expected: FAIL because the policy and four-remote-peer contract do not exist.

- [ ] **Step 3: Implement RTC ownership state and filtering**

Implement `screen_media_allowed` as the single pure authorization rule.

In `RoomMeshTransport::Impl`:

- store `screen_owner_` under `mutex_`;
- read `screen_owner` from `joined`;
- process `screen_state` and `screen_busy` without failing the room;
- publish owner changes through `on_screen_owner`;
- send `{"type":"screen_claim"}` and `{"type":"screen_release"}`;
- in the DataChannel callback, deliver voice from every peer but deliver video/stream audio only when
  `screen_media_allowed(screen_owner_, peer_id)` is true;
- clear and publish owner on stop.

Validation accepts remote peer bounds `1..4`; engineering two-peer tests use `1`.

- [ ] **Step 4: Propagate the contract through the C ABI**

Add `max_remote_peers` to `CatroRoomRuntimeConfig`. Reject values outside `1..4`.

Store the latest owner in `RoomRuntime`, copy it into the fixed `screen_owner` snapshot buffer, and forward
claim/release C functions to `RoomMeshTransport`. Claim returns `0` when the request was queued and `-1` for
invalid/not-running state.

- [ ] **Step 5: Run focused and complete native tests**

```powershell
cmake --build --preset windows-msvc-release
ctest --test-dir out/build/windows-msvc -C Release -R 'catro_windows_rtc_room|catro_windows_voice_runtime|catro_voice_pipeline' --output-on-failure
```

Expected: PASS.

- [ ] **Step 6: Commit and push Task 3**

```powershell
git add -- core/rtc/include/catro/rtc/room_media_policy.hpp core/rtc/include/catro/rtc/room_mesh_transport.hpp core/rtc/src/room_mesh_transport.cpp apps/room-runtime/windows/include/catro/room_runtime.h apps/room-runtime/windows/src/room_runtime.cpp tests/windows/rtc_room_transport_test.cpp
git diff --cached --check
git commit -m "feat: enforce authorized screen media"
git push origin feature/two-client-screen-stream
```

---

### Task 4: Wire Provisioning and Screen Claims into the Windows Client

**Files:**
- Modify: `platform/windows/include/catro/platform/windows/directory_client.hpp`
- Modify: `platform/windows/src/directory_client.cpp`
- Modify: `apps/windows/Catro/Server/ServerView.xaml.h`
- Modify: `apps/windows/Catro/Server/ServerView.xaml.cpp`
- Modify: `tests/apps/ui_policy_test.cpp`

**Interfaces:**
- Consumes: provisioning JSON `max_room_peers`, room-runtime claim/release functions, and snapshot owner.
- Produces: `RtcProvisioning.max_room_peers`; `ServerView::ClaimScreenOwnership()`; owner-aware share UI.

- [ ] **Step 1: Write failing UI policy tests**

Extend `tests/apps/ui_policy_test.cpp` to assert the WinUI source contains:

- provisioning-derived `max_remote_peers`;
- `catro_room_runtime_claim_screen`;
- `catro_room_runtime_release_screen`;
- a bounded ownership wait;
- user-visible busy and timeout messages.

- [ ] **Step 2: Run UI policy test and verify failure**

```powershell
cmake --build --preset windows-msvc-release --target catro_ui_policy_test
ctest --test-dir out/build/windows-msvc -C Release -R catro_ui_policy --output-on-failure
```

Expected: FAIL.

- [ ] **Step 3: Parse and validate room capacity**

Add `std::size_t max_room_peers = 0` to `RtcProvisioning`.

`request_rtc_provisioning` reads `max_room_peers`, accepts `2..5`, and treats missing/out-of-range values as
`DirectoryErrorCode::malformed_response`.

`StartVoice` sets:

```cpp
.max_remote_peers = static_cast<std::uint32_t>(provisioning->max_room_peers - 1)
```

- [ ] **Step 4: Claim ownership before starting room screen capture**

Add:

```cpp
winrt::Windows::Foundation::IAsyncOperation<bool> ClaimScreenOwnership();
```

Behavior:

- direct engineering mode returns `true` without signaling;
- if another owner is already visible, show `"Another participant is sharing"` and return `false`;
- send claim once;
- poll the room snapshot every 50 ms for at most 3 seconds, restoring the UI apartment before touching controls;
- succeed only when `screen_owner` equals the local provisioning peer id;
- show busy or timeout state without starting capture.

Call it after the share dialog/configuration is valid and immediately before `screen_runtime_->start`.
If capture start fails after a successful claim, release ownership. `StopScreenShare` releases ownership before
stopping local capture. Voice teardown remains the final release path.

- [ ] **Step 5: Make share controls owner aware**

`UpdateScreenShareUi` disables the share action while another peer owns the room and keeps watching available.
Owner identity is not displayed as a raw internal ID; the user-facing text is `"Another participant is sharing"`.

- [ ] **Step 6: Run Windows client tests**

```powershell
cmake --build --preset windows-msvc-release
ctest --test-dir out/build/windows-msvc -C Release -R 'catro_ui_policy|catro_windows_screen_runtime|catro_windows_voice_runtime|catro_windows_rtc_room' --output-on-failure
```

Expected: PASS.

- [ ] **Step 7: Commit and push Task 4**

```powershell
git add -- platform/windows/include/catro/platform/windows/directory_client.hpp platform/windows/src/directory_client.cpp apps/windows/Catro/Server/ServerView.xaml.h apps/windows/Catro/Server/ServerView.xaml.cpp tests/apps/ui_policy_test.cpp
git diff --cached --check
git commit -m "feat: coordinate Windows screen ownership"
git push origin feature/two-client-screen-stream
```

---

### Task 5: Add Bounded API Rate Limiting and Production Metrics

**Files:**
- Create: `services/signaling/rate_limit.go`
- Create: `services/signaling/rate_limit_test.go`
- Modify: `services/signaling/main.go`
- Modify: `services/signaling/directory.go`
- Modify: `services/signaling/main_test.go`

**Interfaces:**
- Consumes: Caddy-provided `X-Forwarded-For` on a private-only upstream.
- Produces:
  - `sourceLimiter.Allow(source string, now time.Time) (allowed bool, retryAfter time.Duration)`;
  - `CATRO_TRUST_PROXY_HEADERS`;
  - `CATRO_API_WRITES_PER_MINUTE` with default `30`, accepted range `1..300`;
  - HTTP `429` plus `Retry-After`;
  - metrics for capacity rejections, screen claims/releases/busy results, rate-limit rejections, active rooms,
    and active screen publishers.

- [ ] **Step 1: Write failing limiter and metrics tests**

Add tests for:

- the 31st mutation from one source inside one minute returning `429`;
- allowance after the window expires;
- bounded source storage and stale-entry pruning;
- ignoring forged `X-Forwarded-For` when proxy trust is disabled;
- honoring the first valid forwarded address when trust is enabled;
- metrics changing for capacity, screen, and rate-limit events;
- metrics containing no peer IDs, IP addresses, tokens, invites, SDP, or candidates.

- [ ] **Step 2: Run focused tests and verify failure**

```powershell
Push-Location services/signaling
go test ./... -run 'TestSourceLimiter|TestAPIRateLimit|TestProductionMetrics'
Pop-Location
```

Expected: FAIL.

- [ ] **Step 3: Implement the bounded standard-library limiter**

Use one mutex-protected map, a fixed one-minute window, at most 4096 source entries, and pruning before rejecting
new storage. Do not add a dependency.

Apply the limiter only to mutating directory routes: registration, personal-server sync, invite creation/accept,
and RTC token minting. WebSocket signaling retains its existing per-connection rate limit.

- [ ] **Step 4: Add metric counters/gauges**

Counters are atomics. Active room and active screen-publisher gauges are computed under existing room locks at
scrape time. Keep Prometheus text output bounded and label-free.

- [ ] **Step 5: Run Go verification**

```powershell
Push-Location services/signaling
gofmt -w *.go
go test ./...
go test -race ./...
go vet ./...
Pop-Location
```

Expected: PASS.

- [ ] **Step 6: Commit and push Task 5**

```powershell
git add -- services/signaling/rate_limit.go services/signaling/rate_limit_test.go services/signaling/main.go services/signaling/directory.go services/signaling/main_test.go
git diff --cached --check
git commit -m "feat: harden signaling API limits"
git push origin feature/two-client-screen-stream
```

---

### Task 6: Build the Oracle Free Deployment Bundle

**Files:**
- Modify: `services/signaling/Dockerfile`
- Create: `deploy/oracle-free/compose.yaml`
- Create: `deploy/oracle-free/Caddyfile`
- Create: `deploy/oracle-free/coturn.conf`
- Create: `deploy/oracle-free/.env.example`
- Create: `deploy/oracle-free/install.sh`
- Create: `deploy/oracle-free/update.sh`
- Create: `deploy/oracle-free/backup.sh`
- Create: `deploy/oracle-free/restore.sh`
- Create: `deploy/oracle-free/verify.sh`
- Create: `deploy/oracle-free/README.md`
- Modify: `.gitignore`

**Interfaces:**
- Consumes: one Ubuntu ARM64 host, public IPv4, DNS hostname, Docker Engine with Compose v2.
- Produces: Caddy `2.11.4-alpine`, local `catro-signaling`, coturn `4.18.0-r0`, persistent state, rollback-safe
  updates, seven-backup retention, and deterministic validation.

- [ ] **Step 1: Write deployment validation before artifacts**

Create `verify.sh` to fail unless:

- required environment names exist and secrets are at least 32 bytes;
- `CATRO_MAX_ROOM_PEERS` is `2..5`;
- relay bounds are numeric and `MIN < MAX`;
- Compose has no `latest` image;
- signaling is not published directly;
- metrics are not routed publicly;
- the state path and backup path are distinct;
- `docker compose config` succeeds.

The script supports `VERIFY_ONLY=1`, which performs static validation without starting containers.

- [ ] **Step 2: Run validation and verify failure**

On Git Bash or WSL:

```bash
cd deploy/oracle-free
VERIFY_ONLY=1 ./verify.sh
```

Expected: FAIL because deployment files are incomplete.

- [ ] **Step 3: Update the signaling container**

Change the build image to `golang:1.27.1-alpine`. Keep `CGO_ENABLED=0`, the distroless non-root runtime, and the
existing static binary. Build both ARM64 and AMD64 successfully.

- [ ] **Step 4: Add pinned Compose services**

`compose.yaml` requirements:

- `caddy:2.11.4-alpine`;
- local build `catro-signaling:${CATRO_IMAGE_TAG}`;
- `coturn/coturn:4.18.0-r0`;
- Caddy is the only public HTTP service on TCP 80/443;
- signaling exists only on the private Compose network and listens with `--allow-insecure-http`;
- coturn uses host networking, `external-ip=${CATRO_PUBLIC_IP}`, port 3478 TCP/UDP, and relay UDP
  `${CATRO_TURN_MIN_PORT}-${CATRO_TURN_MAX_PORT}`;
- state bind mount is writable only by signaling's non-root UID;
- secrets enter as environment values and never appear in image build arguments;
- restart policy is `unless-stopped`;
- health checks use commands verified to exist in each pinned image.

`Caddyfile` proxies only `/v1/*` and `/healthz`, preserves WebSocket upgrades, adds conservative security headers,
and does not expose `/metrics`.

`coturn.conf` enables REST-secret auth, fingerprinting, stale nonce, denied loopback/private peer destinations,
bounded allocations, no CLI listener, no anonymous auth, and the configured relay range.

- [ ] **Step 5: Add idempotent operations scripts**

`install.sh`:

- verifies root/Ubuntu/ARM64-or-AMD64/Docker/Compose/DNS inputs;
- creates data, backup, and Caddy directories;
- generates two separate 48-byte base64 secrets if absent;
- writes `.env` with mode `0600`;
- assigns state ownership to UID/GID `65532`;
- runs static validation, builds, starts, and waits for health.

`update.sh`:

- requires a clean checked-out revision;
- records the current commit and image IDs;
- builds new signaling image;
- recreates services;
- runs `verify.sh`;
- restores previous image IDs and service definitions when health fails.

`backup.sh` atomically copies the state and retains seven files.

`restore.sh` requires stopped signaling, validates JSON with the signaling binary or `python3 -m json.tool`, writes
through a temporary file, restores ownership, and never overwrites live state.

- [ ] **Step 6: Run deployment validation**

```bash
cd deploy/oracle-free
shellcheck install.sh update.sh backup.sh restore.sh verify.sh
VERIFY_ONLY=1 ./verify.sh
docker compose --env-file .env.example config
docker buildx build --platform linux/amd64,linux/arm64 ../../services/signaling --output type=cacheonly
```

Expected: PASS. If Docker/Buildx is unavailable locally, record that exact gate as unavailable; do not claim it
passed.

- [ ] **Step 7: Commit and push Task 6**

```powershell
git add -- .gitignore services/signaling/Dockerfile deploy/oracle-free
git diff --cached --check
git commit -m "feat: add Oracle free deployment"
git push origin feature/two-client-screen-stream
```

---

### Task 7: Document Operations and Create the Real-Network Acceptance Record

**Files:**
- Modify: `README.md`
- Modify: `services/signaling/README.md`
- Create: `docs/operations/oracle-free-production.md`
- Create: `docs/validation/oracle-free-production-acceptance.md`
- Modify: `scripts/package-windows.ps1`

**Interfaces:**
- Consumes: deployed hostname and validated Oracle bundle.
- Produces: exact NSG ports, deploy/upgrade/backup/restore instructions, production package command, and an
  unambiguous manual acceptance checklist.

- [ ] **Step 1: Write the operations runbook**

Document:

- Oracle A1 Ubuntu VM selection without claiming permanent availability;
- reserved/public IP and DNS prerequisite;
- NSG ingress: TCP 22 from operator IP, TCP 80/443, TCP/UDP 3478, configured UDP relay range;
- host firewall parity;
- install, health, logs, metrics, backup, restore, update, rollback, and secret rotation;
- free-tier and single-VM limitations;
- budget alerts and prohibited paid resources;
- package build:

```powershell
.\scripts\package-windows.ps1 -Configuration Release -ServiceUrl https://catro.example.com
```

- [ ] **Step 2: Make packaging reject non-HTTPS production endpoints**

Keep engineering mode unchanged. In production packaging, reject HTTP, localhost, raw room secrets, or missing
service URL. Preserve the existing `catro-network.json` minimal contract.

- [ ] **Step 3: Add the acceptance record**

Create unchecked fields for:

- service commit and image IDs;
- Oracle shape/region/date;
- two distinct Internet providers;
- five simultaneous voice clients and sixth-peer rejection;
- one screen plus selected-window audio to four viewers;
- competing share rejection;
- forced TURN candidate evidence;
- restart and backup/restore evidence;
- CPU, memory, disk, transfer, packet loss, voice quality, and glass-to-glass latency;
- unresolved failures.

The document must state `NOT VALIDATED` until every required field is filled with observed evidence.

- [ ] **Step 4: Run documentation and packaging checks**

```powershell
git diff --check
.\scripts\package-windows.ps1 -Configuration Release -ServiceUrl http://example.test
```

Expected: packaging command fails because production requires HTTPS.

Then run with a test HTTPS endpoint through the script's no-build/configuration validation path if available.

- [ ] **Step 5: Commit and push Task 7**

```powershell
git add -- README.md services/signaling/README.md docs/operations/oracle-free-production.md docs/validation/oracle-free-production-acceptance.md scripts/package-windows.ps1
git diff --cached --check
git commit -m "docs: add Oracle production runbook"
git push origin feature/two-client-screen-stream
```

---

### Task 8: Full Verification, Graph Refresh, and Final Push

**Files:**
- Modify only files required to fix failures found by this task.
- Refresh: `graphify-out/**` locally, but do not commit generated graph output unless it was already tracked and
  the project policy explicitly requires those generated diffs in the same commit.

**Interfaces:**
- Consumes: all previous task outputs.
- Produces: fresh verification evidence and a remote branch containing every verified implementation commit.

- [ ] **Step 1: Run all Go verification**

```powershell
Push-Location services/signaling
gofmt -w *.go
go test ./...
go test -race ./...
go vet ./...
Pop-Location
```

Expected: PASS.

- [ ] **Step 2: Run full Windows Release build and tests**

```powershell
cmake --build --preset windows-msvc-release
ctest --test-dir out/build/windows-msvc -C Release --output-on-failure
```

Expected: every available test passes; hardware-dependent skips are reported as skips, not passes.

- [ ] **Step 3: Run deployment static checks**

```bash
cd deploy/oracle-free
shellcheck install.sh update.sh backup.sh restore.sh verify.sh
VERIFY_ONLY=1 ./verify.sh
docker compose --env-file .env.example config
```

Expected: PASS, or explicitly record unavailable local tooling.

- [ ] **Step 4: Refresh graph and inspect the final diff**

```powershell
graphify update .
git diff --check
git status --short
git log --oneline --decorate -10
```

Confirm no unrelated file is staged.

- [ ] **Step 5: Commit only verification-driven fixes**

If Step 1–4 required source corrections:

```powershell
# Stage each corrected file by its literal repository path; never use git add -A or git add .
git diff --cached --check
git commit -m "fix: complete production room verification"
git push origin feature/two-client-screen-stream
```

If no corrections were required, do not create an empty commit.

- [ ] **Step 6: Verify remote synchronization**

```powershell
git fetch origin
git rev-list --left-right --count 'HEAD...origin/feature/two-client-screen-stream'
git log -1 --oneline --decorate
```

Expected divergence: `0 0`.

- [ ] **Step 7: Report the remaining external gate**

Report implementation and automated checks separately from the Oracle real-network acceptance. Until an Oracle
VM and five clients execute `docs/validation/oracle-free-production-acceptance.md`, describe the result as
implementation-complete but not Internet-production-validated.
