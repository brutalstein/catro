# Oracle Free Production Rooms — Design

Status: approved by the user on 2026-09-29.

## 1. Purpose

Deliver the first Internet-deployable Catro environment for small private rooms without a recurring
hosting charge. Windows clients on different networks must be able to join the same authenticated
room, speak simultaneously, and share one screen with selected-application audio.

The deployment target is one Oracle Cloud Always Free ARM64 VM. Catro continues to use a peer mesh:
the server owns identity, membership, room signaling, screen-publisher arbitration, and TURN
provisioning, but it does not decode, mix, transcode, record, or inspect media.

This is a production baseline for a small private service, not a high-availability or unlimited-scale
offering. Oracle quotas, availability, reclamation policy, and free-tier terms must be rechecked when
the VM is created and before every public release.

## 2. Existing baseline

The implementation must extend the current system instead of replacing it:

- `services/signaling` already owns persistent identities, shared-server membership, one-use invites,
  short-lived RTC authorization, WebSocket SDP/ICE signaling, and ephemeral coturn credentials.
- `core/rtc` already creates one WebRTC peer connection per remote room participant and fans bounded
  voice, video, and stream-audio datagrams over unordered, no-retransmit DataChannels.
- `core/voice` already maintains separate jitter/Opus decoder state per remote stream and mixes active
  speakers through a bounded limiter.
- The Windows room, voice, and screen runtimes already share one authenticated RTC room.
- The Windows shell already requests RTC provisioning after the user joins an authorized server.

No SFU, new codec, browser client, managed media service, database, Kubernetes layer, or cloud-specific
client protocol is introduced by this milestone.

## 3. User-visible contract

- A voice room contains between one and five connected users.
- The configured room capacity may be any integer from two through five; the production default is
  five.
- Users may join and leave while the room is active.
- Every joined user may speak. Remote voices are decoded independently and mixed locally.
- At most one joined user owns screen publishing at a time.
- Screen ownership covers both H.264 screen video and selected-window application audio.
- A sixth user receives an explicit room-capacity error and does not partially join.
- A second screen-share request receives an explicit busy result identifying that another participant
  is already sharing.
- If the active publisher stops sharing, leaves voice, disconnects, or loses its authenticated socket,
  ownership is released and another user may claim it.
- A receiver accepts screen video and stream audio only from the signaling-authorized screen owner.
- Microphone mute/deafen behavior remains local and does not require signaling-server state.

## 4. Deployment topology

One ARM64 Ubuntu VM runs a small Docker Compose deployment:

```text
Internet
   |
   +-- TCP 80/443 ----------------------> Caddy
   |                                      |
   |                                      +--> catro-signaling (private network only)
   |
   +-- UDP/TCP 3478 --------------------> coturn
   |
   +-- UDP configured relay range ------> coturn
```

Caddy owns public HTTPS/WSS termination and automatic certificate renewal. The signaling container is
not directly published on a host port. Coturn exposes STUN/TURN and uses the same REST shared secret as
the signaling service.

The deployment requires a DNS hostname that resolves to the VM's public IP. The hostname may be a free
subdomain; no paid domain is required by the repository. The client package contains only the public
HTTPS control-plane URL.

The initial production TURN profile exposes UDP and TCP TURN on port 3478. TURN-over-TLS is not a
completion claim for this milestone; it is added only if real-network evidence shows that the target
users require it. WebRTC DTLS continues to protect Catro media whether the selected ICE path is direct
or relayed.

## 5. Room capacity

`CATRO_MAX_ROOM_PEERS` is the deployment-facing configuration value:

- accepted range: `2..5`;
- production default: `5`;
- invalid values fail service startup;
- the value is returned in authenticated RTC provisioning;
- each client configures its remote peer bound to `max_room_peers - 1`;
- the signaling room remains authoritative and rejects a join after the total participant count reaches
  the configured value.

Participant count is dynamic; the limit is not. No room allocates resources for five users in advance
other than existing bounded containers. A five-user room creates at most four remote WebRTC peer
connections per client.

The deployment-level limit is intentionally lower than internal media safety ceilings. Internal limits
remain defensive bounds, not product capacity promises.

## 6. Screen-publisher arbitration

The authenticated signaling room stores an optional `screen_owner_peer_id`.

Client-to-server room control messages:

- `screen_claim`
- `screen_release`

Server-to-client room events:

- `screen_state`, carrying an empty owner or the current owner peer id;
- `screen_busy`, returned only to a failed claimant.

Rules:

1. A joined peer may claim when no owner exists.
2. Repeating a claim by the current owner is idempotent.
3. A non-owner cannot release another peer's ownership.
4. Disconnecting the owner clears ownership before the updated state is broadcast.
5. A newly joined peer receives current screen ownership as part of the initial joined state.
6. Caller-provided identity is ignored; the authenticated WebSocket connection supplies peer identity.
7. Signal and room-control messages share the existing per-connection rate bound and message-size bound.

The Windows shell starts capture only after its own peer id is confirmed as owner. Stopping capture
sends release, but socket teardown remains the final cleanup mechanism.

`RoomMeshTransport` tracks the server-authorized owner. Its video and stream-audio channel callbacks
drop datagrams whose sending peer is not the current owner. This prevents conflicting clients from
presenting unauthorized screen media even if they bypass the normal UI.

## 7. Signaling and TURN security

- Public signaling is `wss://` only.
- Caddy redirects HTTP to HTTPS.
- The signaling HMAC secret and coturn REST secret are separate random values of at least 32 bytes.
- Secrets live in a host-owned environment file with mode `0600`; they are never committed, baked into
  an image, written to `catro-network.json`, or returned by an API.
- RTC room tokens remain short lived and bound to server, channel, and peer identity.
- TURN usernames/passwords remain short lived and are derived server-side from the shared secret.
- Registration and invite mutation endpoints receive an application-level per-source-IP rate limit.
- Proxy addresses are trusted only inside the private Compose network. Direct public access to the
  signaling container is impossible.
- WebSocket join, signaling, and room-control validation remains strict and bounded.
- `/metrics` is private to the Compose network. `/healthz` may be exposed through Caddy but returns only
  service health.
- SSH is not managed by Catro; deployment documentation requires restricting it to the operator's
  source IP in Oracle Network Security Groups.

Rate-limit state is in memory and bounded. Restarting the service clears it. Distributed rate limiting
is out of scope because this design intentionally deploys exactly one signaling instance.

## 8. Persistence and recovery

The signaling state file is stored in a named persistent host directory, not in the container writable
layer. Container replacement and VM reboot must preserve identities, server membership, and invites.

The deployment includes:

- an atomic local backup command that copies the current directory state into a timestamped backup file;
- retention of the seven newest local backups;
- a restore command that refuses to overwrite running service state;
- documented use of Oracle volume backups as an additional operator action.

The JSON state file remains the source of truth for this scale. A database migration is not justified
for one process and a bounded private user set.

## 9. Deployment artifacts

The repository gains one focused deployment boundary under `deploy/oracle-free/`:

```text
deploy/oracle-free/
  compose.yaml
  Caddyfile
  coturn.conf
  .env.example
  install.sh
  update.sh
  backup.sh
  restore.sh
  README.md
```

Responsibilities:

- `compose.yaml`: pinned services, private network, volumes, health checks, restart policies, ARM64-safe
  images, and explicit port publication.
- `Caddyfile`: HTTPS/WSS proxying, security headers, request-size bounds, and no public metrics route.
- `coturn.conf`: REST authentication, no anonymous/legacy credentials, conservative relay port range,
  no loopback/private peer relaying, and bounded allocations.
- `.env.example`: names only, safe non-secret defaults, and generation instructions.
- `install.sh`: preflight checks, directory creation, secret generation, configuration validation, and
  first start. Re-running it is safe.
- `update.sh`: pulls/builds the exact repository revision, recreates changed services, waits for health,
  and restores the previous containers if health fails.
- `backup.sh` / `restore.sh`: bounded local state protection.
- `README.md`: Oracle VM, DNS, NSG/firewall, deployment, package build, upgrade, backup, restore, and
  two-machine validation instructions.

No script creates Oracle resources or stores Oracle credentials. Cloud account and network-resource
creation remain explicit operator actions.

## 10. Image and release rules

- The signaling image builds for Linux ARM64 and AMD64 from the existing multi-stage Dockerfile.
- Runtime containers run as non-root wherever the selected image supports it.
- Image tags used by Compose are pinned; `latest` is not allowed.
- The signaling image is built from the checked-out source revision instead of depending on a public
  registry account.
- Deployment scripts print the source commit and active image identifiers.
- Automatic unattended application updates are out of scope. The operator runs `update.sh`.

## 11. Failure behavior

- Missing or weak secrets, invalid capacity, missing TURN provisioning, insecure public signaling, or an
  unwritable state directory fail startup.
- Failure to establish a direct ICE path falls back to configured TURN.
- If both direct ICE and TURN fail, the client surfaces an RTC connection failure; it does not silently
  switch to unencrypted UDP.
- Signaling restart disconnects active rooms. Clients may rejoin through the normal user action; automatic
  infinite reconnect loops are out of scope.
- Coturn restart may interrupt relayed peers. Directly connected peers are unaffected.
- A VM outage stops the service. No automatic failover is claimed.
- Backup or update failure leaves the last known state and containers intact and prints an actionable
  error.

## 12. Observability

The signaling metrics continue to expose active connections, accepted signaling messages, and rejected
messages. This milestone adds bounded counters for:

- room-capacity rejections;
- screen claims, releases, and busy rejections;
- registration/invite rate-limit rejections;
- active rooms and active screen publishers.

Container health checks cover Caddy, signaling, and coturn. Deployment documentation includes commands
for recent logs, container health, disk use, active room metrics, and TURN allocation inspection.

Metrics contain no access tokens, TURN credentials, invite codes, SDP, ICE candidates, IP-address labels,
display names, or media payloads.

## 13. Testing

Automated checks:

1. Go tests for capacity values `2..5`, invalid bounds, sixth-peer rejection, and capacity release after
   disconnect.
2. Go tests for screen claim, idempotent claim, busy rejection, invalid release, owner disconnect, and
   state delivered to a late joiner.
3. Go tests for rate-limit enforcement, expiry, memory bounds, and trusted-proxy handling.
4. C++ tests for provisioning capacity propagation and remote peer bounds.
5. C++ tests that accept authorized-owner screen media and reject non-owner video/stream-audio.
6. Existing voice multi-stream mixing, room transport, screen runtime, directory, packaging, and Windows
   test suites remain green.
7. `docker compose config` validates the deployment.
8. Shell scripts pass `shellcheck`; Go code passes `go test` and `go vet`.
9. A local container smoke test verifies HTTPS proxy routing where a test certificate is supplied,
   signaling health, persistent state across recreation, and TURN credential generation.

Manual production acceptance on the Oracle VM:

1. Two Windows computers on different Internet connections register and join through an invite.
2. Bidirectional voice works without direct LAN reachability.
3. Five clients can join and speak; a sixth receives the capacity error.
4. One client shares a window plus application audio; every other client receives it.
5. A second screen-share attempt is rejected until the first owner stops or disconnects.
6. A forced relay run confirms the selected candidate is TURN and voice/screen media remain usable.
7. Service, coturn, and VM restarts demonstrate the documented recovery behavior.
8. CPU, memory, disk, outbound transfer, packet loss, voice quality, and screen latency are recorded.

No claim of Internet production readiness is made until the manual acceptance record exists.

## 14. Cost and scaling boundary

The deployment is designed to fit current Oracle Always Free resources, but software cannot guarantee
that a cloud provider will keep an account, VM, public IP, quota, bandwidth allowance, or free-tier policy
unchanged. The operator must enable billing-budget alerts where Oracle makes them available and must not
select paid shapes, paid load balancers, managed databases, or paid network services.

Mesh cost grows quadratically with participants. At five users, each publisher sends to at most four
remote peers. This is the accepted ceiling. An SFU becomes a separate design milestone if measured client
upload, TURN egress, room size, or concurrent screen-publisher requirements exceed this boundary.

## 15. Non-goals

- High availability, multi-region deployment, or zero-downtime signaling failover.
- More than five simultaneous room participants.
- More than one simultaneous screen publisher per room.
- SFU routing, server-side media mixing, transcoding, recording, or moderation.
- Text chat, file storage, push notifications, account recovery, or public user discovery.
- Managed database migration.
- TURN-over-TLS completion without real-network evidence requiring it.
- macOS client completion.
- Automated Oracle account/resource provisioning.

## 16. Definition of done

This milestone is implementation-complete when:

- the deployment artifacts can start Caddy, signaling, and coturn on an ARM64 Oracle VM;
- public clients receive only HTTPS/WSS control-plane configuration and ephemeral TURN credentials;
- room capacity is configurable from two through five and enforced consistently by server and clients;
- up to five users can speak with existing local multi-stream mixing;
- screen ownership is server-authorized, automatically released, and enforced on received media;
- state survives container and VM restart and backup/restore procedures are tested;
- automated tests and deployment validation pass;
- unrelated generated/local files are not committed.

It is production-validated only after the manual two-network, five-client, single-screen-owner, forced-TURN,
restart, and resource-measurement acceptance run is recorded.
