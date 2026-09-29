# Oracle Free production operations

Status: deployment runbook for the single-VM Catro production baseline.

This runbook deploys the repository's small private-room control plane and TURN relay on one
Ubuntu VM. It does not create Oracle resources automatically and it does not turn implementation
completion into production validation.

## 1. Provisioning boundary

Use an Oracle Cloud Ubuntu VM on an Always Free-eligible Ampere A1 shape when the account, home
region, capacity, and current Oracle terms permit it. Keep the selected compute, boot volume,
public IP, and networking inside the account's current free allowances. Do not add a paid load
balancer, managed database, NAT gateway, or other paid managed service for this milestone.

Before deployment record:

- Oracle region and availability domain.
- VM shape, OCPUs, and memory.
- public IPv4 and VNIC private IPv4.
- DNS hostname resolving to the public IPv4.
- operator public IPv4/CIDR for SSH restriction.
- configured TURN relay range; repository default is UDP 49160-49200.

Oracle free-tier capacity is not guaranteed by Catro. Recheck Oracle's current Free Tier
documentation and console pricing before creating or resizing resources.

## 2. Network Security Group ingress

Create only these inbound rules for the VM:

| Protocol | Port/range | Source | Purpose |
| --- | --- | --- | --- |
| TCP | 22 | operator public IP/CIDR only | SSH administration |
| TCP | 80 | 0.0.0.0/0 | ACME HTTP challenge and HTTPS redirect |
| TCP | 443 | 0.0.0.0/0 | Catro HTTPS/WSS control plane |
| TCP | 3478 | 0.0.0.0/0 | TURN over TCP fallback |
| UDP | 3478 | 0.0.0.0/0 | STUN/TURN |
| UDP | 49160-49200 by default | 0.0.0.0/0 | TURN relay allocations |

If CATRO_TURN_MIN_PORT or CATRO_TURN_MAX_PORT changes, change the NSG and host firewall to the
same exact range. Do not expose the signaling container port 8443, Caddy admin port 2019, or
signaling metrics publicly.

Allow normal outbound Internet access so Caddy can obtain/renew certificates and the operator can
pull pinned container images. Catro itself does not require a public database or media backend.

## 3. Host firewall

Keep the Ubuntu firewall equivalent to the NSG. When UFW is used, confirm SSH access from a second
session before enabling it.

Example, replacing the operator CIDR and relay range:

~~~sh
sudo ufw default deny incoming
sudo ufw default allow outgoing
sudo ufw allow from 198.51.100.25/32 to any port 22 proto tcp
sudo ufw allow 80/tcp
sudo ufw allow 443/tcp
sudo ufw allow 3478/tcp
sudo ufw allow 3478/udp
sudo ufw allow 49160:49200/udp
sudo ufw enable
sudo ufw status verbose
~~~

Do not copy the example operator address.

## 4. DNS and first install

Create an A record for the production hostname and wait until it resolves to the VM public IPv4.
The installer refuses a mismatched hostname/public-IP pair.

From a clean Catro checkout:

~~~sh
cd deploy/oracle-free
sudo CATRO_HOSTNAME=catro.example.com \
     CATRO_PUBLIC_IP=203.0.113.10 \
     ./install.sh
~~~

install.sh:

1. verifies Ubuntu, architecture, Docker/Compose, DNS, and required host tools;
2. detects the VNIC private IPv4 unless CATRO_PRIVATE_IP is explicitly supplied;
3. creates independent 48-byte signaling and TURN secrets;
4. writes .env with mode 0600, including independent bounded mutation and exact-discovery rates;
5. prepares signaling state, backup, and Caddy persistence directories;
6. builds the signaling image from the checked-out source revision;
7. starts Caddy, signaling, and coturn;
8. waits for every service health check.

Keep deploy/oracle-free/.env off backups that leave the trusted operator boundary.

## 5. Health and diagnostics

~~~sh
cd deploy/oracle-free
sudo ./verify.sh
sudo docker compose --env-file .env ps
sudo docker compose --env-file .env logs --tail=200 signaling caddy coturn
~~~

The public health endpoint contains only service health:

~~~sh
curl -fsS https://catro.example.com/healthz
~~~

Metrics are intentionally private. Inspect them through Caddy from inside the Compose networks:

~~~sh
sudo docker compose --env-file .env exec caddy \
  curl -fsS http://signaling:8443/metrics
~~~

Expected production metrics are label-free counters/gauges. They must not contain access tokens,
TURN credentials, invite codes, Server Codes, join-request ids, SDP, ICE candidates, peer/user/
server ids, IP-address labels, or media data.

For coturn, inspect recent logs and allocation behavior:

~~~sh
sudo docker compose --env-file .env logs --tail=200 coturn
sudo docker compose --env-file .env exec coturn \
  turnutils_stunclient -t 1000 -p 3478 127.0.0.1
~~~

## 6. Backup and restore

Create a local state backup before changes and on a regular operator schedule:

~~~sh
cd deploy/oracle-free
sudo ./backup.sh
~~~

The script copies the atomically-persisted directory state — including memberships, Server Codes,
pending/resolved retained join requests, and bounded text history — and retains the seven newest
local backup files.

Restore is deliberately offline:

~~~sh
sudo docker compose --env-file .env stop signaling
sudo ./restore.sh /var/backups/catro/directory-YYYYMMDDTHHMMSSZ.json
sudo docker compose --env-file .env up -d signaling
sudo ./verify.sh
~~~

restore.sh rejects a running signaling service and invalid JSON. Use Oracle volume backups as an
additional operator-controlled recovery layer; local backups do not survive loss of the boot/block
volume.

## 7. Update and rollback

Check out the exact revision to deploy and ensure the worktree is clean:

~~~sh
git fetch origin
git checkout feature/two-client-screen-stream
git pull --ff-only
git status --short
cd deploy/oracle-free
sudo ./update.sh
~~~

update.sh records the source commit, creates a state backup, saves the previous signaling image,
and uses the gitignored out/deploy-active directory to retain the last successful Compose, Caddy,
and coturn definition. It then builds the current revision, recreates services, and runs health
verification. A failed health gate restores the previous signaling image and previous deployment
definition before re-running health checks.

Do not run unattended application updates for this milestone. Review each Caddy, coturn, Go, base
image, and Catro revision before deploying it.

## 8. Secret rotation

CATRO_SIGNALING_SECRET and CATRO_TURN_SECRET are separate.

- Rotating the signaling secret invalidates outstanding short-lived RTC room tokens. Schedule the
  change when rooms can rejoin.
- Rotating the TURN secret must update signaling and coturn together because signaling mints the
  REST credentials that coturn validates.
- Generate new secrets with a CSPRNG, keep at least 32 bytes of entropy, update .env with mode 0600,
  then recreate the affected services and run verify.sh.
- Never place either secret in catro-network.json, a Windows package, Git, logs, metrics, or image
  build arguments.

## 9. Windows production package

Build the portable Windows client with only the public HTTPS API base URL:

~~~powershell
.\scripts\package-windows.ps1 -Configuration Release -ServiceUrl https://catro.example.com
~~~

Production packaging rejects HTTP, localhost/loopback endpoints, and URLs carrying credentials,
queries, or fragments. Direct-peer engineering packages remain explicit:

~~~powershell
.\scripts\package-windows.ps1 -Configuration Release -Engineering
~~~

## 10. Production acceptance boundary

Deployment health is not the final product gate. Complete
docs/validation/oracle-free-production-acceptance.md on real infrastructure. Until every required
field has observed evidence, describe this deployment as implementation-complete but NOT VALIDATED
for Internet production.

External references:

- Oracle Cloud Free Tier: https://docs.oracle.com/en-us/iaas/Content/FreeTier/freetier.htm
- Docker Engine for Ubuntu: https://docs.docker.com/engine/install/ubuntu/
- Caddy automatic HTTPS: https://caddyserver.com/docs/automatic-https
- coturn container documentation: https://github.com/coturn/coturn/tree/master/docker/coturn
