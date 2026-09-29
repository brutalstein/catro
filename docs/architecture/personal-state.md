# Personal identity and server state

This slice gives every Catro installation a durable local identity and personal-server root without
pretending that local state is authentication.

## Identity

On first run Catro requests 128 bits from the operating system CSPRNG for each stable identifier:

- local user id;
- personal server id;
- default text-channel id;
- default voice-channel id.

Windows uses `BCryptGenRandom(..., BCRYPT_USE_SYSTEM_PREFERRED_RNG)`. macOS uses
`SecRandomCopyBytes`. The shared community core never falls back to `rand`,
`std::random_device`, timestamps, MAC addresses, host names, or device fingerprints.

Identifiers are opaque 128-bit values serialized as 32 lowercase hexadecimal characters. They are
identifiers only, **not credentials**. Future account authentication keys/tokens must live in
Windows Credential Manager / macOS Keychain or another purpose-built secret store.

## Personal server invariant

The first state contains:

- the local identity;
- one personal server owned by that identity;
- one `general` text channel;
- one `Voice` voice channel;
- the local identity as the only owner.

The domain model allows more channels and members later but always requires at least one text
channel, at least one voice channel, exactly one owner, and no duplicate member/channel ids. Only
`ServerRole::owner` has elevated authority.

Invite issuance is owner-authorized in the domain layer. Invite tokens carry 128 random bits and use
a 26-character canonical Crockford Base32 representation. The token format exists now, but a token
is not considered usable until a server-side invite registry is implemented. The UI must not claim
that local-only invite generation is a working remote invitation system.

## Persistence

The state schema is versioned as:

`catro.local-state / 1.0`

The JSON payload is bounded to 64 KiB and validated after decoding. Unknown/corrupt/incompatible
state is surfaced as an error and is never silently replaced, because replacing it would silently
change the user's identity.

Default locations:

- Windows: `%LOCALAPPDATA%\Catro\state-v1.json`
- macOS: `~/Library/Application Support/Catro/state-v1.json` through the Foundation application
  support directory API.

First-run creation is serialized across processes with a path-scoped lock. Writes go to a same-
directory staging file, are flushed, and then atomically renamed/replaced. A racing second process
therefore reopens the first committed identity instead of generating another one.

No realtime audio/capture/network thread touches this state file.

## UI boundary

The Windows server shell now reads the persisted personal-server state at startup and projects the
server name, local display name, member count, and default channel names into WinUI. If local state
cannot be loaded safely, Catro does not fabricate a replacement identity.

The authenticated service now exists behind the invite/join connection points. It treats client ids
and roles as untrusted input, authorizes membership/invite operations server-side, provisions RTC
rooms, and stores the bounded default text-channel history. Local identifiers remain identity
references rather than credentials; the per-install directory credential is stored separately with
DPAPI on Windows.
