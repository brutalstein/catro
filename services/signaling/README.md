# Catro signaling and server directory

This service is Catro's production control plane for small native voice rooms.

It owns identity registration, shared-server membership, one-use invites, bounded persistent text
history, short-lived RTC credentials, WebRTC SDP/ICE signaling, and RTC provisioning. Voice, screen video, and stream audio
never pass through this service: peers exchange those bounded datagrams directly over WebRTC
DataChannels, with TURN relay available when direct ICE connectivity is impossible.

## Production requirements

- TLS at this service or at a trusted reverse proxy. Clients receive and require a `wss://`
  signaling endpoint.
- A random `CATRO_SIGNALING_SECRET` of at least 32 bytes.
- A persistent writable `CATRO_SIGNALING_STATE` path. It contains directory state and the bounded
  retained text history; back this file up.
- STUN plus TURN URLs in `CATRO_ICE_SERVERS`.
- A coturn REST shared secret in `CATRO_TURN_SECRET`, at least 32 bytes. Do not place long-lived
  TURN usernames/passwords in the ICE URLs.
- The built-in bounded per-source mutation limiter enabled (default 30 writes/minute, configurable 1..300).
- `CATRO_TRUST_PROXY_HEADERS=true` only when the immediate upstream is the private deployment proxy; direct public callers cannot override their source with `X-Forwarded-For`.
- Health monitoring of `/healthz` and private metrics scraping of `/metrics`.

The service derives a fresh coturn REST username/password for every 15-minute RTC room token.
The TURN shared secret never leaves the server.

Example production environment:

~~~sh
export CATRO_SIGNALING_SECRET='replace-with-32+-byte-random-secret'
export CATRO_SIGNALING_STATE='/var/lib/catro/directory.json'
export CATRO_PUBLIC_SIGNALING_URL='wss://catro.example.com/v1/rtc'
export CATRO_ICE_SERVERS='stun:turn.example.com:3478;turn:turn.example.com:3478?transport=udp;turns:turn.example.com:5349?transport=tls'
export CATRO_TURN_SECRET='replace-with-coturn-rest-shared-secret'

catro-signaling   --addr :8443   --tls-cert /run/secrets/fullchain.pem   --tls-key /run/secrets/privkey.pem
~~~

If TLS terminates at a trusted reverse proxy, the service can listen on a private HTTP socket behind
that proxy by using `--allow-insecure-http`; the externally returned
`CATRO_PUBLIC_SIGNALING_URL` should still be `wss://...`. The repository's
`deploy/oracle-free` bundle implements this boundary with Caddy and does not publish the signaling
container directly.

## Client flow

A packaged Catro client contains only `catro-network.json` with the HTTPS API base URL.

On first run, the Windows client creates a 256-bit install credential and stores it under
`%LocalAppData%\Catro` protected by Windows DPAPI. The backend stores only its SHA-256 hash.

The normal product flow is:

1. Register the stable local Catro identity.
2. Sync the user's personal server or list servers the identity already belongs to.
3. The owner creates a one-use invite from the server Invite button.
4. Another computer presses `+`, pastes the invite, and becomes a member of that same server.
5. Pressing Join in the voice channel requests a short-lived RTC token plus WSS/STUN/TURN
   provisioning for that exact server/channel membership.
6. The `# general` text surface reads/sends membership-authorized bounded history through HTTPS.
7. Voice, screen video, and selected-window application audio use the same WebRTC room mesh.

End users do not configure `CATRO_ROOM_TOKEN`, `CATRO_SERVER_ID`, or `CATRO_ICE_SERVERS`.

## Windows portable package

Build a production-ready portable ZIP with the public HTTPS control-plane endpoint:

~~~powershell
.\scripts\package-windows.ps1 -Configuration Release -ServiceUrl https://catro.example.com
~~~

The generated `catro-network.json` contains only the API URL. It never contains the signaling HMAC
secret, TURN secret, device credential, or room token.

Production packaging rejects plaintext HTTP, localhost/loopback service URLs, and URLs containing
credentials, query parameters, or fragments. The generated network file contains only the HTTPS API
base URL.

For local engineering only, a package without an Internet control plane can be made explicitly:

~~~powershell
.\scripts\package-windows.ps1 -Configuration Release -Engineering
~~~

Direct UDP `CATRO_VOICE_SLOT` / `CATRO_VOICE_BIND` / `CATRO_VOICE_PEER` settings are retained
only for local diagnostics and are not the normal production path.

## Engineering token minting

The legacy manual mint command remains useful for low-level room-runtime tests:

~~~sh
catro-signaling --secret "$CATRO_SIGNALING_SECRET" --mint-token   --server-id SERVER_ID --channel-id VOICE_CHANNEL_ID --peer-id USER_ID --ttl 15m
~~~

The product UI does not use this path.
