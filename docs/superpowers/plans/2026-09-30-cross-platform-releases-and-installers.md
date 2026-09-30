# Cross-platform Releases and One-command Installers Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build and rehearse verified Windows production and macOS preview packages with transactional one-command installers and a draft-first GitHub Release workflow, without publishing a production release before the native gaming UX milestone is accepted.

**Architecture:** Keep distribution at the repository boundary: native PowerShell and POSIX shell scripts create and install fixed-name assets, a small cross-platform PowerShell validator enforces tag/asset/checksum contracts, and GitHub Actions builds each native platform before assembling a draft release. Installer tests use only the Python standard-library HTTP server and temporary directories; no package manager, updater daemon, marketplace release action, or external signing service is introduced.

**Tech Stack:** Windows PowerShell 5.1, POSIX `sh`, Python 3 standard library, CMake/Xcode, GitHub Actions, GitHub CLI, existing WinUI/SwiftUI builds.

**Spec:** `docs/superpowers/specs/2026-09-30-cross-platform-releases-and-installers-design.md`

**Execution method:** Native inline execution, as explicitly requested by the user.

## Global Constraints

- Do not create or push a version tag and do not publish a GitHub Release in this plan.
- The public release remains blocked on the separately designed native gaming UX, accessibility, real-machine media, and game-impact acceptance gates.
- Windows packages must embed exactly `https://51-170-186-61.sslip.io`.
- macOS assets remain named and documented as preview assets until native room, voice, and screen parity is accepted.
- Windows installation is per-user under `%LOCALAPPDATA%\Programs\Catro`; macOS installation is per-user under `~/Applications/Catro.app`.
- Installers must verify SHA-256 before replacing an existing install and must restore the old install when replacement fails.
- macOS packaging is ad-hoc signed only; no Apple account, paid certificate, notarization, `sudo`, quarantine removal, or Gatekeeper bypass.
- Use only GitHub-maintained checkout/artifact actions and the runner-provided `gh` CLI; add no release marketplace action.
- Preserve the existing dirty `graphify-out/**` and `apps/windows/Catro/Assets/` files and never stage them.
- After each source/document change, run `graphify update .`; commit and push only the files owned by the current task.
- Every behavioral change follows RED → GREEN. Configuration-only workflow wiring is covered by repository contract tests before the YAML is written.

## Review Focus

1. **Corrupt or attacker-controlled archive/checksum input:** installer rejects malformed/mismatched checksums and a package missing the expected executable/app before touching the old install; Tasks 1 and 2 own these tests.
2. **Interrupted or failed upgrade:** existing installation is restored after a forced replacement failure and no sibling path is changed; Tasks 1 and 2 own these tests.
3. **Wrong platform or architecture:** Windows rejects non-x64 and macOS maps only `arm64`/`x86_64` to the fixed assets; Tasks 1 and 2 own these tests.
4. **Partial or accidental public release:** workflow contract requires all build/service jobs, draft-first publication, complete asset validation, and tag-only publication; Task 3 owns these tests.
5. **Misleading product claims:** documentation labels macOS as preview, states that zero game impact is impossible to promise, and says release URLs activate only after the gated first release; Task 4 owns these tests.

---

### Task 1: Transactional Windows installer

**Files:**
- Create: `scripts/install-windows.ps1`
- Create: `tests/installers/windows-installer-test.ps1`
- Modify: `.github/workflows/ci.yml` in the `windows` job

**Interfaces:**
- Consumes: release assets `Catro-windows-x64.zip` and `Catro-windows-x64.zip.sha256`.
- Consumes: optional test environment variables `CATRO_RELEASE_BASE_URL`, `CATRO_INSTALL_ROOT`, `CATRO_VERSION`, `CATRO_TEST_ARCHITECTURE`, and `CATRO_TEST_FAIL_AFTER_BACKUP`.
- Produces: the public installer asset `install-windows.ps1`, compatible with Windows PowerShell 5.1 and PowerShell 7.
- Produces: a per-user installation containing `Catro.exe` at `<install-root>\Catro.exe`.

- [ ] **Step 1: Write the failing Windows installer fixture test**

Create `tests/installers/windows-installer-test.ps1` as a framework-free assertion script. It must:

- allocate a loopback port with `System.Net.Sockets.TcpListener`;
- generate valid v1 and v2 ZIP fixtures containing `Catro.exe`;
- generate a package missing `Catro.exe`;
- write valid, malformed, and mismatched checksum fixtures;
- launch `python -m http.server <port> --bind 127.0.0.1 --directory <fixture-root>` with `Start-Process -WindowStyle Hidden`;
- invoke `scripts/install-windows.ps1` in child Windows PowerShell processes with test overrides;
- assert first install, v1→v2 replacement, rollback after `CATRO_TEST_FAIL_AFTER_BACKUP=1`, malformed checksum rejection, mismatched checksum rejection, missing-executable rejection, non-x64 rejection through `CATRO_TEST_ARCHITECTURE=x86`, no automatic app launch, and an untouched sentinel beside the overridden install root;
- stop the HTTP process and remove all temporary directories in `finally`.

- [ ] **Step 2: Run the Windows installer test to verify RED**

Run:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/installers/windows-installer-test.ps1
```

Expected: FAIL because `scripts/install-windows.ps1` does not exist.

- [ ] **Step 3: Implement the minimal transactional installer**

Create `scripts/install-windows.ps1` with:

- `$ErrorActionPreference = 'Stop'`;
- x64 validation using `RuntimeInformation.OSArchitecture`, overridden only when `CATRO_TEST_ARCHITECTURE` is non-empty;
- default base URL `https://github.com/brutalstein/catro/releases`;
- default selector `latest/download`, or `download/<CATRO_VERSION>` when a version is supplied;
- HTTP allowed only for a loopback host when `CATRO_RELEASE_BASE_URL` is explicitly set for tests; all production URLs require HTTPS;
- strict parsing of exactly one 64-character hexadecimal digest from the checksum file;
- `Invoke-WebRequest -UseBasicParsing`, `Get-FileHash`, and `Expand-Archive`;
- staging validation for `Catro.exe`;
- backup → replace → rollback transaction;
- the test-only forced failure immediately after the old installation is moved to backup;
- cleanup in `finally`;
- best-effort per-user Start Menu shortcut creation after a successful install;
- no process launch;
- installed version and executable path output.

- [ ] **Step 4: Run the Windows installer test to verify GREEN**

Run:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/installers/windows-installer-test.ps1
```

Expected: PASS with a final `Windows installer tests passed.` line.

- [ ] **Step 5: Add the installer test to Windows CI**

After the existing Windows `Test` step in `.github/workflows/ci.yml`, add:

```yaml
- name: Test Windows installer
  shell: powershell
  run: ./tests/installers/windows-installer-test.ps1
```

- [ ] **Step 6: Run Windows regression checks**

Run:

```powershell
./scripts/test.ps1 -Configuration Debug
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/installers/windows-installer-test.ps1
git diff --check
```

Expected: existing Windows tests pass, installer tests pass, and `git diff --check` is silent.

- [ ] **Step 7: Refresh Graphify, commit, and push Task 1**

Run:

```powershell
graphify update .
git add .github/workflows/ci.yml scripts/install-windows.ps1 tests/installers/windows-installer-test.ps1
git commit -m "feat: add verified Windows installer"
git push origin feature/two-client-screen-stream
```

Expected: only the three Task 1 files are committed and the branch push succeeds.

---

### Task 2: Native macOS preview package and transactional installer

**Files:**
- Create: `scripts/package-macos.sh`
- Create: `scripts/install-macos.sh`
- Create: `tests/installers/macos-installer-test.sh`
- Modify: `.github/workflows/ci.yml` in the `macos` job

**Interfaces:**
- Consumes: `scripts/build.sh <Debug|Release>`, `CATRO_PRESET`, and the built `out/build/<preset>/<configuration>/Catro.app`.
- Consumes: optional test environment variables `CATRO_RELEASE_BASE_URL`, `CATRO_INSTALL_ROOT`, `CATRO_VERSION`, `CATRO_TEST_ARCHITECTURE`, and `CATRO_TEST_FAIL_AFTER_BACKUP`.
- Produces: `out/dist/Catro-macos-arm64-preview.zip` or `out/dist/Catro-macos-x64-preview.zip` plus the matching `.sha256`.
- Produces: the public installer asset `install-macos.sh`.

- [ ] **Step 1: Write the failing macOS installer/package contract test**

Create `tests/installers/macos-installer-test.sh` using `set -eu` and small `assert_*` functions. It must:

- create v1/v2 `.app` fixture bundles with executable `Contents/MacOS/Catro`;
- create a bundle missing that executable;
- create ZIP/checksum fixtures with `ditto`;
- serve them from `python3 -m http.server` on loopback;
- verify first install, replacement, forced rollback, malformed checksum rejection, mismatched checksum rejection, missing app executable rejection, `arm64` and `x86_64` asset selection, unsupported architecture rejection, no launch, and untouched sibling sentinel;
- when `CATRO_TEST_BUILT_APP` is set, verify `scripts/package-macos.sh` output name, `codesign --verify --deep --strict`, archive architecture, preview README, and checksum;
- clean up the server and temporary files with `trap`.

- [ ] **Step 2: Wire the test into macOS CI before implementation**

In the existing macOS matrix job, add after the current Debug test:

```yaml
- name: Build release package
  run: scripts/package-macos.sh Release
- name: Test macOS installer and package
  env:
    CATRO_TEST_BUILT_APP: out/build/macos-clang/Release/Catro.app
  run: tests/installers/macos-installer-test.sh
```

- [ ] **Step 3: Commit and push the intentional RED macOS contract**

Run:

```powershell
git add .github/workflows/ci.yml tests/installers/macos-installer-test.sh
git commit -m "test: define macOS release package contract"
git push origin feature/two-client-screen-stream
```

Expected: GitHub macOS jobs fail because `scripts/package-macos.sh` and `scripts/install-macos.sh` do not exist. Record the failing run URL and exact missing-script failure in the execution ledger.

- [ ] **Step 4: Implement `scripts/package-macos.sh`**

The script must:

- use `set -eu`;
- accept configuration as `${1:-Release}`;
- call `scripts/build.sh "$configuration"`;
- validate `Catro.app` and `Contents/MacOS/catro-capability-probe`;
- map `uname -m` `arm64` → `arm64` and `x86_64` → `x64`, rejecting anything else;
- copy the app into a clean `out/dist/Catro-macos-<asset-arch>-preview/`;
- add `README.txt` stating diagnostics/local-audio-only preview scope, macOS 13.0 minimum, Gatekeeper behavior, and no production voice/screen claim;
- apply `codesign --force --deep --sign -`;
- verify with `codesign --verify --deep --strict`;
- verify native architecture with `lipo -archs`;
- create the ZIP with `ditto -c -k --sequesterRsrc` so `Catro.app` and `README.txt` are the archive
  roots rather than an extra staging-directory wrapper;
- write lowercase SHA-256 in `<digest><two spaces><filename>` format;
- remove incomplete final output on failure.

- [ ] **Step 5: Implement `scripts/install-macos.sh`**

Mirror the Task 1 transaction with POSIX tools:

- production base URL `https://github.com/brutalstein/catro/releases`;
- `latest/download` or `download/<CATRO_VERSION>`;
- loopback-only HTTP test override;
- architecture mapping `arm64`/`x86_64`;
- `curl -fL`, `shasum -a 256`, and `ditto -x -k`;
- validation of `Catro.app/Contents/MacOS/Catro`;
- backup → replace → rollback with forced test failure support;
- no `sudo`, `xattr`, Gatekeeper changes, privacy changes, or app launch;
- install destination default `~/Applications/Catro.app`;
- cleanup with `trap`;
- installed version and app path output.

- [ ] **Step 6: Refresh Graphify, commit GREEN implementation, and push**

Run:

```powershell
graphify update .
git add scripts/package-macos.sh scripts/install-macos.sh
git commit -m "feat: package and install macOS preview"
git push origin feature/two-client-screen-stream
```

- [ ] **Step 7: Verify native macOS CI turns GREEN**

Run:

```powershell
gh run list --repo brutalstein/catro --branch feature/two-client-screen-stream --limit 5
gh run watch <green-implementation-run-id> --repo brutalstein/catro --exit-status
```

Expected: both `macos-15` and `macos-15-intel` package/installer steps pass; the complete CI run succeeds. Record the run URL in the ledger.

---

### Task 3: Release contract validator and draft-first workflow

**Files:**
- Create: `scripts/validate-release.ps1`
- Create: `tests/release/release-validation-test.ps1`
- Create: `tests/release/release-workflow-test.ps1`
- Create: `.github/workflows/release.yml`
- Modify: `.github/workflows/ci.yml` in the `deployment` job

**Interfaces:**
- Consumes: `CMakeLists.txt` project version and an assembled release asset directory.
- Produces: `scripts/validate-release.ps1 -Tag <vX.Y.Z> -AssetDirectory <path> [-RepositoryRoot <path>]`.
- Consumes in the workflow: Windows/macOS package artifacts and repository installer scripts.
- Produces in manual dispatch: an `assembled-release-<version>` workflow artifact only.
- Produces for a future valid tag: a verified draft release that becomes public only after all assets pass validation.

- [ ] **Step 1: Write failing release validator tests**

Create `tests/release/release-validation-test.ps1`. In temporary repositories/asset directories, test:

- tag `v0.1.0` matches `project(Catro VERSION 0.1.0 ...)`;
- a mismatched tag fails;
- every fixed asset is required;
- unexpected missing checksum fails;
- malformed checksum fails;
- mismatched archive checksum fails;
- a complete asset set succeeds;
- installer files are required but do not require checksum sidecars.

- [ ] **Step 2: Run validator tests to verify RED**

Run:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/release/release-validation-test.ps1
```

Expected: FAIL because `scripts/validate-release.ps1` does not exist.

- [ ] **Step 3: Implement `scripts/validate-release.ps1`**

Implement exactly:

```powershell
param(
    [Parameter(Mandatory = $true)][string] $Tag,
    [Parameter(Mandatory = $true)][string] $AssetDirectory,
    [string] $RepositoryRoot
)
```

The script must:

- resolve the repository root when omitted;
- parse the numeric root CMake project version;
- require `$Tag -eq "v$version"`;
- require the eight fixed assets from the spec;
- parse strict SHA-256 sidecars and hash the six archives;
- reject missing/non-file paths and checksum mismatches;
- print the validated tag and complete asset list;
- return non-zero on every invalid case.

- [ ] **Step 4: Run validator tests to verify GREEN**

Run:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/release/release-validation-test.ps1
```

Expected: PASS with `Release validation tests passed.`.

- [ ] **Step 5: Write the failing workflow contract test**

Create `tests/release/release-workflow-test.ps1` with text-level assertions that
`.github/workflows/release.yml` contains:

- `workflow_dispatch` and tag trigger `v*`;
- Windows, macOS matrix, service validation, and publish jobs;
- top-level `contents: read`;
- job-level `contents: write` only in publish;
- `needs` on all package/service jobs;
- exact production service URL;
- `scripts/validate-release.ps1`;
- `gh release create` with `--draft`;
- complete asset verification before `gh release edit --draft=false`;
- a manual-dispatch assembled artifact path;
- no marketplace release action.

- [ ] **Step 6: Run workflow contract test to verify RED**

Run:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/release/release-workflow-test.ps1
```

Expected: FAIL because `.github/workflows/release.yml` does not exist.

- [ ] **Step 7: Implement `.github/workflows/release.yml`**

Create the four jobs from the spec:

- Windows Release build/test/package/upload;
- macOS Release matrix build/test/package/upload;
- signaling/deployment validation that also emits the validated release tag;
- publish/assemble with `needs` on all three.

For `workflow_dispatch`, validate and upload the assembled directory as a workflow artifact, then
stop. For a tag, fail if a published release already exists, create a draft with the complete asset
set, compare the release asset names to the expected set, re-download into a temporary directory,
run `scripts/validate-release.ps1` again, and only then execute:

```sh
gh release edit "$TAG" --draft=false
```

If upload or validation fails, the release remains draft.

- [ ] **Step 8: Add release contract tests to regular CI**

In the deployment job, add a `pwsh` step that runs:

```powershell
./tests/release/release-validation-test.ps1
./tests/release/release-workflow-test.ps1
```

- [ ] **Step 9: Run Task 3 checks**

Run:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/release/release-validation-test.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/release/release-workflow-test.ps1
./scripts/package-windows.ps1 -SkipBuild -ValidateConfigurationOnly -ServiceUrl https://51-170-186-61.sslip.io
git diff --check
```

Expected: both contract tests pass, endpoint policy passes, and diff check is silent.

- [ ] **Step 10: Refresh Graphify, commit, and push Task 3**

Run:

```powershell
graphify update .
git add .github/workflows/ci.yml .github/workflows/release.yml scripts/validate-release.ps1 tests/release/release-validation-test.ps1 tests/release/release-workflow-test.ps1
git commit -m "ci: add verified release workflow"
git push origin feature/two-client-screen-stream
```

Expected: push succeeds without a tag or public release.

---

### Task 4: Download documentation and non-publishing release rehearsal

**Files:**
- Create: `tests/release/documentation-contract-test.ps1`
- Modify: `README.md`
- Modify: `deploy/oracle-free/README.md`

**Interfaces:**
- Consumes: fixed release asset names and installer commands from Tasks 1–3.
- Produces: user-facing install/uninstall and platform-support documentation.
- Produces: one successful `workflow_dispatch` rehearsal whose artifacts pass `scripts/validate-release.ps1`.

- [ ] **Step 1: Write the failing documentation contract test**

Create `tests/release/documentation-contract-test.ps1` asserting that `README.md` contains:

- the exact Windows and macOS one-line install commands;
- fixed direct asset names;
- Windows x64 production and macOS arm64/x64 preview labels;
- Windows `%LOCALAPPDATA%\Programs\Catro` and macOS `~/Applications/Catro.app` uninstall paths;
- SHA-256 verification guidance;
- macOS Gatekeeper/ad-hoc-signing warning;
- explicit macOS preview voice/screen limitation;
- the production service URL;
- wording that hardware acceleration minimizes but cannot eliminate game impact;
- wording that download URLs activate only when the gated first release is published.

Also assert that `deploy/oracle-free/README.md` points operators to the root download section without
duplicating desktop installation logic.

- [ ] **Step 2: Run documentation test to verify RED**

Run:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/release/documentation-contract-test.ps1
```

Expected: FAIL on missing download/install documentation.

- [ ] **Step 3: Update root and deployment documentation**

Add a compact Download and install section near the top of `README.md` with:

- Windows production and macOS preview tables;
- one-line commands and direct asset names;
- first-release activation notice;
- manual checksum examples;
- uninstall paths;
- Gatekeeper/privacy guidance that never tells users to disable security;
- the real Windows/macOS feature boundary;
- the production service health endpoint;
- the non-zero game-impact statement.

Add one link-sized operator note to `deploy/oracle-free/README.md`.

- [ ] **Step 4: Run documentation and all release contract tests**

Run:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/release/documentation-contract-test.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/release/release-validation-test.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/release/release-workflow-test.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/installers/windows-installer-test.ps1
git diff --check
```

Expected: all local contract/installer tests pass and diff check is silent.

- [ ] **Step 5: Refresh Graphify, commit, and push documentation**

Run:

```powershell
graphify update .
git add README.md deploy/oracle-free/README.md tests/release/documentation-contract-test.ps1
git commit -m "docs: add verified desktop downloads"
git push origin feature/two-client-screen-stream
```

- [ ] **Step 6: Run the non-publishing release rehearsal**

Run:

```powershell
gh workflow run release.yml --repo brutalstein/catro --ref feature/two-client-screen-stream
gh run list --repo brutalstein/catro --workflow release.yml --branch feature/two-client-screen-stream --limit 3
gh run watch <rehearsal-run-id> --repo brutalstein/catro --exit-status
```

Expected: all release workflow jobs pass; the publish job takes the manual-dispatch assembly path and
does not create a GitHub Release.

- [ ] **Step 7: Download and independently validate rehearsal assets**

Run:

```powershell
$dir = Join-Path $env:TEMP "catro-release-rehearsal-$PID"
gh run download <rehearsal-run-id> --repo brutalstein/catro --name assembled-release-v0.1.0 --dir $dir
./scripts/validate-release.ps1 -Tag v0.1.0 -AssetDirectory $dir
gh release list --repo brutalstein/catro
```

Expected: assembled assets validate and `gh release list` still shows no newly published release.

- [ ] **Step 8: Verify production service remains healthy**

Run:

```powershell
$key = Join-Path $HOME 'Downloads\ssh1.key'
ssh -i $key ubuntu@51.170.186.61 'cd /home/ubuntu/catro/deploy/oracle-free && sudo ./verify.sh'
curl.exe -fsS https://51-170-186-61.sslip.io/healthz
$metricsStatus = curl.exe -sS -o NUL -w '%{http_code}' https://51-170-186-61.sslip.io/metrics
if ($metricsStatus -ne '404') { throw "Expected private metrics route to return 404, got $metricsStatus" }
ssh -i $key ubuntu@51.170.186.61 "printf 'GET /v1/rtc HTTP/1.1\r\nHost: 51-170-186-61.sslip.io\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n' | openssl s_client -connect 51-170-186-61.sslip.io:443 -servername 51-170-186-61.sslip.io -quiet 2>/dev/null | head -n 1"
ssh -i $key ubuntu@51.170.186.61 'cd /home/ubuntu/catro/deploy/oracle-free && sudo docker compose --env-file .env exec -T coturn turnutils_stunclient -t 1000 -p 3478 127.0.0.1'
```

Expected: deployment reports healthy, HTTPS returns `{"status":"ok"}`, public metrics returns 404,
the WSS response starts with `HTTP/1.1 101`, and the coturn STUN utility succeeds. Record exact output
in the execution ledger. This is a service-availability check only: do not claim fresh full relay or
real two-computer acceptance from it; preserve the earlier full external relay evidence separately.

- [ ] **Step 9: Run final branch verification**

Run:

```powershell
./scripts/test.ps1 -Configuration Debug
Push-Location services/signaling
go test ./...
go test -race ./...
go vet ./...
Pop-Location
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/installers/windows-installer-test.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/release/release-validation-test.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/release/release-workflow-test.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/release/documentation-contract-test.ps1
git diff --check
git status --short
```

Expected: all available local checks pass; native macOS evidence comes from the successful rehearsal
run; only known user/generated `graphify-out/**` and `apps/windows/Catro/Assets/` dirt remains.

- [ ] **Step 10: Whole-branch review**

Create the Superpowers review package from the plan merge base through `HEAD`, dispatch one fresh
whole-branch reviewer when the multi-agent tool is available, fix every Critical/Important finding
with RED → GREEN tests, ledger deferred Minor findings, and push the final reviewed commits.

Do not create `v0.1.0`; public release remains blocked on the gaming UX and macOS media milestones.
