# Oracle Free production acceptance record

**Status: NOT VALIDATED**

Do not change the status above until every required item in this document has observed evidence.
Implementation, CI, a successful container start, or a same-machine test is not a substitute for
this real-network acceptance run.

## Build and deployment identity

- [ ] Date/time (UTC):
- [ ] Catro commit:
- [ ] catro-signaling image ID:
- [ ] Caddy image ID:
- [ ] coturn image ID:
- [ ] Oracle region / availability domain:
- [ ] VM shape:
- [ ] OCPUs / memory:
- [ ] Ubuntu version:
- [ ] Public hostname:
- [ ] TURN relay UDP range:
- [ ] No unresolved deployment-health failures:

## Network diversity

Use at least two genuinely different Internet access networks. Two machines behind the same home
router do not satisfy this gate.

- [ ] Client A network/provider recorded:
- [ ] Client B network/provider recorded:
- [ ] At least one client is outside the Oracle VM's LAN/VPC:
- [ ] Public HTTPS health succeeds from both networks:
- [ ] No client package contains signaling/TURN secrets:

Observed evidence / notes:

> Pending.

## Five-client voice capacity

- [ ] Five distinct Windows clients join the same authorized voice room:
- [ ] All five can transmit voice:
- [ ] Each client receives/mixes the other active speakers:
- [ ] Mute prevents microphone media transmission:
- [ ] Deafen outputs silence without creating a stale playback backlog:
- [ ] A sixth authenticated client receives the explicit room-capacity error:
- [ ] After one participant leaves, a replacement client can join:
- [ ] No sustained codec errors, render-full drops, worker resyncs, or runaway underruns:

Record client counters, packet loss, and any audible defects:

> Pending.

## Screen ownership and application audio

- [ ] One joined participant claims screen ownership:
- [ ] Window sharing reaches every other joined participant:
- [ ] Selected-window application audio reaches every other joined participant:
- [ ] Remote viewer can use embedded view:
- [ ] Remote viewer can use pop-out:
- [ ] Remote viewer can enter and leave full screen:
- [ ] Viewer preserves source aspect ratio during resize:
- [ ] Second participant's simultaneous share attempt receives the busy result:
- [ ] Non-owner screen video is rejected:
- [ ] Non-owner stream audio is rejected:
- [ ] Ownership clears when publisher stops:
- [ ] Ownership clears when publisher disconnects:
- [ ] Another participant can claim after release:

Observed evidence / notes:

> Pending.

## Forced TURN

This gate must prove relay behavior, not merely show that TURN credentials were provisioned.

- [ ] Direct peer connectivity is intentionally prevented for the test:
- [ ] Selected ICE candidate pair is recorded as relayed/TURN:
- [ ] Bidirectional voice remains usable over TURN:
- [ ] Screen video remains usable over TURN:
- [ ] Selected-window audio remains usable over TURN:
- [ ] No fallback to the unencrypted engineering UDP transport occurs:
- [ ] TURN credential expiry/rejoin behavior is observed without long-lived credentials:

Candidate evidence and logs:

> Pending.

## Restart and recovery

- [ ] Signaling container restart behavior observed:
- [ ] Clients receive a clear failure/disconnect rather than hanging indefinitely:
- [ ] Users can rejoin normally after signaling recovery:
- [ ] coturn restart behavior observed for a forced-relay room:
- [ ] VM reboot preserves identities and server membership:
- [ ] VM reboot preserves Caddy certificate state:
- [ ] backup.sh creates a valid retained backup:
- [ ] Seven-backup retention verified:
- [ ] restore.sh refuses while signaling is running:
- [ ] Offline restore recovers expected directory state:
- [ ] Failed-update rollback path exercised or an unresolved limitation is recorded:

Observed evidence / notes:

> Pending.

## Resource and quality measurements

Record measurements for both idle service and the five-client room. Do not enter estimates.

| Measurement | Idle | Five-client voice | Screen + audio to four viewers | Notes |
| --- | ---: | ---: | ---: | --- |
| Oracle VM CPU | Pending | Pending | Pending | |
| Oracle VM memory | Pending | Pending | Pending | |
| Oracle disk use | Pending | Pending | Pending | |
| Oracle outbound transfer | Pending | Pending | Pending | |
| Sender CPU | Pending | Pending | Pending | |
| Sender GPU | Pending | Pending | Pending | |
| Sender memory | Pending | Pending | Pending | |
| Viewer CPU/GPU | Pending | Pending | Pending | |
| Packet loss | Pending | Pending | Pending | |
| Voice quality/issues | Pending | Pending | Pending | |
| Glass-to-glass screen latency | Pending | Pending | Pending | |

Additional required observations:

- [ ] Memory remains bounded during at least a 30-minute room:
- [ ] Screen restart does not create a growing decode/backlog delay:
- [ ] Capture/encode/decode failure counters do not grow continuously:
- [ ] TURN relay allocation count returns after participants leave:
- [ ] Signaling active-room/screen-publisher metrics return to zero after the room closes:

## Security and exposure check

- [ ] TCP 8443 is not reachable from the public Internet:
- [ ] Caddy admin port 2019 is not reachable from the public Internet:
- [ ] /metrics is not reachable through the public hostname:
- [ ] SSH is restricted to operator source CIDR:
- [ ] Only required 80/443, 3478, and relay-range rules are public:
- [ ] .env mode is 0600:
- [ ] State and backup directories are distinct:
- [ ] No secret appears in docker image history/build arguments:
- [ ] No secret or peer/IP label appears in metrics:
- [ ] No paid Oracle service was added unintentionally:

Observed evidence / notes:

> Pending.

## Final disposition

Keep this section unresolved until every checkbox above is complete.

- Production validation result: **NOT VALIDATED**
- Blocking failures:
  - Pending.
- Accepted limitations:
  - Single VM; no high availability.
  - Maximum five simultaneous room participants.
  - One simultaneous screen publisher.
  - WebRTC peer mesh; no SFU.
  - TURN-over-TLS not claimed by this milestone.
  - macOS product interoperability not claimed.

When all evidence is complete, replace NOT VALIDATED with the observed result and link the raw
measurement/log artifacts used for that decision.
