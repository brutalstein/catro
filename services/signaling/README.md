# Catro signaling service

This service authenticates voice-room membership and relays only WebRTC SDP/ICE signaling.
Media never passes through this process in the small-room mesh mode.

Production requirements:

- TLS (wss://) at this service or at a trusted reverse proxy.
- A random CATRO_SIGNALING_SECRET of at least 32 bytes.
- STUN plus TURN credentials configured in the Catro clients.
- Firewall access for the signaling HTTPS port and the separately deployed TURN service.
- Health monitoring of /healthz and metrics scraping of /metrics.

Mint a development/self-hosted room token:

~~~sh
catro-signaling --secret "$CATRO_SIGNALING_SECRET" --mint-token \
  --server-id SERVER_ID --channel-id VOICE_CHANNEL_ID --peer-id USER_ID --ttl 24h
~~~

Run with TLS:

~~~sh
catro-signaling --secret "$CATRO_SIGNALING_SECRET" \
  --tls-cert /run/secrets/fullchain.pem --tls-key /run/secrets/privkey.pem
~~~

--allow-insecure-http exists only for localhost/LAN engineering tests and must not be used as
an Internet production configuration.
