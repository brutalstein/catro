# Bounded persistent text channels — design

Date: 2026-09-30
Status: implementation target

## Goal

Turn the existing native `# general` surface into a real membership-authorized text channel without
mixing chat traffic into the realtime RTC media path.

The first production text slice is intentionally small and bounded:

- one existing default text channel per server;
- authenticated server members can read and send;
- message history survives signaling-service restart;
- the Windows client renders a virtualized timeline and sends from the existing composer;
- the client polls only while a text channel is active, with at most one request in flight;
- all message/state sizes have explicit hard bounds.

Direct messages, threads, reactions, edits, deletion, typing indicators, presence, search, attachments,
rich embeds, notifications, and arbitrary channel creation are outside this slice.

## Architecture

The existing boundaries stay intact:

```text
core/community
  stable server/text-channel/user identifiers
            |
            v
platform/windows/directory_client
  HTTPS authenticated message API
            |
            v
services/signaling
  membership authorization + bounded persistence
            |
            v
Windows ServerView
  virtualized message snapshots + send command
```

Voice, screen video, and selected-window audio continue to use `core/rtc` and WebRTC. Text messages
never enter room media DataChannels.

## Server model

`directoryServer` gains `text_channel_id` beside `voice_channel_id`. Server sync supplies both
stable IDs from the local `PersonalServer`.

A message record contains:

- opaque random message id;
- monotonically increasing server-side sequence;
- server id;
- text channel id;
- author user id;
- UTF-8 content;
- server receive timestamp in Unix milliseconds.

The response additionally projects the current author display name from the directory user record.

Bounds:

- maximum content: 2,000 UTF-8 bytes;
- maximum retained messages per text channel: 512;
- maximum retained messages across the directory: 8,192;
- maximum page returned to a client: 100;
- existing 32 MiB directory-state file bound remains authoritative.

When a channel reaches 512 messages, the oldest record in that channel is removed before the new
record is committed. When the global message bound is reached, the oldest first record across
channel logs is removed. Persistence remains atomic through the existing staging-file + fsync +
rename path.

## HTTP API

### POST /v1/messages

Authenticated member-only mutation.

Request:

```json
{
  "server_id": "...",
  "channel_id": "...",
  "content": "hello"
}
```

The endpoint verifies:

- bearer identity;
- server membership;
- exact server text-channel id;
- bounded valid UTF-8 JSON string;
- non-empty content with no C0/DEL control characters.

It returns the committed message descriptor. The existing per-source mutation limiter wraps this
POST path.

### GET /v1/messages

Authenticated member-only query.

Query:

`server_id=<id>&channel_id=<id>&after=<sequence>&limit=<1..100>`

`after` is exclusive. A zero/missing cursor returns the retained window from the beginning. The
response includes ordered messages and `next_after`.

This slice uses bounded foreground polling rather than a second WebSocket protocol. That keeps text
independent from voice-room membership and avoids holding one realtime socket merely to view chat.
The Windows client polls once per second only while the text channel is visible and never overlaps
requests.

## Windows client contract

`DirectoryServer` gains `text_channel_id`.

`DirectoryMessage` is an immutable value with id, sequence, author id/display name, content, and
timestamp.

The directory client adds:

- `list_directory_messages(..., after, limit)`;
- `send_directory_message(..., content)`.

Both use the existing WinHTTP HTTPS boundary and response-size cap.

## UI contract

The existing disabled composer becomes active only when an authenticated directory server with a
text-channel id is selected.

The text surface uses a native `ListView` so containers remain virtualized. Each item shows:

- author display name;
- local receive/server timestamp presentation;
- wrapped message content.

Enter sends a single-line message. Empty/whitespace-only input is ignored. The composer is disabled
while its send is in flight so duplicate submissions cannot occur.

Switching server or leaving the text channel:

- invalidates the current message generation;
- resets the sequence cursor;
- clears the timeline;
- prevents late results from a previous server being applied.

Network work never runs on the XAML thread. UI mutation resumes on the captured apartment context.

## Failure behavior

- Unauthorized/forbidden reads and sends are explicit errors.
- A malformed server response does not partially update the timeline.
- Send failure keeps the user's composer text and surfaces a native dialog/error state.
- Poll failure does not destroy already rendered history; later polls can recover.
- Server persistence failure rolls back the in-memory mutation.

## Security and privacy

- Client-supplied author id/display name are never accepted.
- Membership is checked server-side for every read and write.
- Message content is plain user text, rendered through native text controls; no HTML is interpreted.
- Metrics receive only counters, never message text, ids, or author labels.
- Message API remains behind HTTPS/Caddy in production.
- Existing per-source POST rate limiting applies to message sends.

## Validation

Required automated evidence:

- membership authorization;
- outsider read/write rejection;
- correct channel enforcement;
- message ordering/cursor behavior;
- per-channel retention bound;
- persistence/reload;
- rate-limit coverage remains green;
- Windows compile;
- full existing CI matrix.

Real-machine acceptance later verifies two packaged Windows clients exchange text over the deployed
HTTPS service and retain history across service restart.
