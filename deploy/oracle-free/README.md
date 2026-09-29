# Oracle Free production bundle

This directory is the first small-room Internet deployment boundary for Catro. It runs the
signaling/directory service behind Caddy and runs coturn on the host network. Media remains
peer-to-peer WebRTC when ICE can connect directly and uses TURN only when a relay is required.

## Topology

- Caddy 2.11.4-alpine: public TCP 80/443, HTTPS/WSS termination, only /v1/* and /healthz.
- catro-signaling: private Compose network only, non-root distroless runtime, persistent state bind mount.
- coturn 4.18.0-r0: host network, TURN/STUN TCP+UDP 3478 and the configured UDP relay range.

The signaling service never receives media. coturn relays encrypted WebRTC packets without
decoding them.

## Host prerequisites

Use Ubuntu on ARM64 or AMD64 with Docker Engine + Compose v2, a public IPv4, and a DNS hostname
already resolving to that address. Oracle networking must allow TCP 80/443, TCP+UDP 3478 and the
configured UDP relay range. Restrict SSH to the operator source address.

Oracle Cloud free-tier capacity and terms are external constraints. Confirm the selected shape,
region, quotas and billing settings in the Oracle console before deploying.

## First install

From the repository checkout:

~~~sh
cd deploy/oracle-free
sudo CATRO_HOSTNAME=catro.example.com \
     CATRO_PUBLIC_IP=203.0.113.10 \
     ./install.sh
~~~

The installer detects the VM private IPv4, generates independent 48-byte signaling and TURN
secrets, writes .env with mode 0600, prepares persistent directories and starts the pinned
services. Re-running it preserves existing secrets unless explicitly overridden.

Static source validation without starting containers:

~~~sh
VERIFY_ONLY=1 ./verify.sh
~~~

If Docker Compose is installed, static validation also runs docker compose config. Without
Docker it reports that gate as unavailable rather than claiming it passed.

## Operations

~~~sh
./verify.sh
./backup.sh
./update.sh
docker compose --env-file .env logs --tail=200 signaling caddy coturn
docker compose --env-file .env ps
~~~

Backups are atomic copies of the signaling JSON state and the seven newest local backups are kept.
To restore, stop signaling first and pass one backup file:

~~~sh
docker compose --env-file .env stop signaling
./restore.sh /var/backups/catro/directory-YYYYMMDDTHHMMSSZ.json
docker compose --env-file .env up -d signaling
./verify.sh
~~~

update.sh requires a clean checkout, backs up state before recreation, retains the previous
signaling image temporarily and restores it if health verification fails.

## Security boundary

- .env is host-only and ignored by Git.
- Signaling and TURN secrets are independent and at least 32 bytes.
- Only Caddy publishes HTTP ports; the private metrics endpoint is not routed publicly.
- Signaling trusts forwarded client addresses only from a private/loopback proxy peer.
- coturn uses time-limited REST credentials and blocks relay targets in private/link-local IPv4 ranges.
- coturn 4.18 has CLI support disabled by default; this bundle does not enable it.
- TURN-over-TLS is not claimed by this milestone. WebRTC media is still protected by DTLS.

This bundle is implementation infrastructure, not proof of production readiness. The separate
real-network acceptance gate must still exercise multiple Internet connections, forced TURN,
restart/recovery and measured resource/latency behavior.
