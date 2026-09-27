# Security

## Reporting a vulnerability

Report vulnerabilities privately to the maintainers. Do not open a public issue for them.
Include the affected platform, the commit, and steps to reproduce.

## Trust boundaries

- **Probe helper output is untrusted.** The service reads at most 1 MiB from the helper. It
  parses the output with a strict parser that rejects duplicate keys, unknown fields, and invalid
  enums. It also rejects non-canonical values and fragments that name another probe. A probe
  that is late, crashes, or writes malformed output becomes an explicit issue in the snapshot.
- **The helper process is isolated.** On Windows it runs in a job object that is killed on close.
  On macOS it runs in its own process group. Stdin is null, and the helper is terminated at its
  hard budget.
- **Imported reports are untrusted.** Report parsing rejects inputs over 1 MiB before it parses
  them.
- **The voice peer is an engineering transport, not a security boundary.** `catro-voice-peer`
  uses connected UDP with a fixed maximum voice datagram, strict packet parsing, and a code-enforced loopback/private-IPv4 address boundary, but it has no
  encryption, authentication, ICE, DTLS-SRTP, or identity layer. Its default documentation uses
  loopback; LAN use must be explicit. Do not expose it to the public Internet.

## Privacy

- Passive probes never show a prompt, start capture, or open an audio stream. On macOS, the
  screen-capture permission state comes from a non-prompting preflight check. If that check does
  not report a grant, the state is unknown.
- Human reports and the clipboard copy replace device display names by default. Identifiers
  such as adapter LUIDs, registry IDs, and endpoint IDs stay in place so that reports remain
  diagnosable, so a report is **not anonymous**. The JSON export is the complete report without
  redaction.
- Passive probes, reports, the diagnostics shell, and audio-check do not send telemetry. The
  `catro-voice-peer` tool sends Opus voice packets only when the user explicitly launches it with
  a peer address. The engineering transport is unencrypted.
