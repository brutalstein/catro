# Server Code discovery and approval-based join requests — design

Date: 2026-09-30
Status: implementation target

## Goal

Add a privacy-preserving way to find a Catro server by a user-shareable identifier, request
membership, and let the server owner approve or reject the request.

This is deliberately separate from existing invite codes:

- invite code = capability-like direct join;
- Server Code = exact lookup followed by owner approval.

The feature must not turn the directory into a globally enumerable public-server search engine.

## External design references

The design intentionally borrows the useful parts of established systems without copying their
product surface:

- Discord Server Member Applications: pending applicants cannot view server content and are admitted
  only after administrator review.
  https://support.discord.com/hc/en-us/articles/29729107418519-Server-Member-Applications
- Matrix knocking: membership intent is represented as a state separate from joined membership.
  https://spec.matrix.org/
- OWASP API Security Top 10 2023: every endpoint receiving an object identifier must enforce
  object-level authorization; automated sensitive business flows require abuse/resource controls.
  https://api-security.owasp.org/editions/2023/en/0xa1-broken-object-level-authorization/
  https://api-security.owasp.org/editions/2023/en/0xa6-unrestricted-access-to-sensitive-business-flows/

## Identity boundary

Catro already has an internal opaque server id. That id remains an internal identity reference and
is not the discovery identifier.

Each directory server receives a separate persistent public Server Code:

`CAT-XXXX-XXXX-XXXX-XXXX-XXXX`

where the five four-character groups are the uppercase hexadecimal encoding of 10 random bytes
(80 bits of entropy).

Properties:

- generated with the operating system cryptographic RNG;
- unique inside the directory;
- persisted with the server;
- case-insensitive on input;
- exact-match only;
- never used for authorization;
- safe to display/copy/share;
- no prefix, fuzzy, or global search in this milestone.

Existing persisted servers without a code are migrated on directory open. Migration must be
persisted atomically before the service begins serving requests.

## Server lookup

Authenticated endpoint:

`GET /v1/server-lookup?code=<ServerCode>`

Response exposes only a minimal preview:

- public code;
- server display name;
- member count;
- relationship: `none`, `pending`, `member`, or `owner`;
- pending request id only when the requester already has a pending request.

It never exposes:

- internal server id to non-members;
- owner/user ids;
- member roster;
- channels;
- messages;
- invite codes;
- RTC configuration;
- credentials;
- IP addresses.

Unknown and malformed codes resolve to the same not-found behavior after canonicalization.

Lookup is protected by a dedicated per-source limiter independent from the normal mutation limiter.

Default: 30 exact lookups/minute/source.
Configured by `CATRO_DISCOVERY_READS_PER_MINUTE`, clamped to 1..300.

## Join-request state

Persistent record:

```
id
server_id
requester_id
message
status = pending | approved | rejected | cancelled
created_at
updated_at
expires_at
```

Bounds:

- optional request message: 280 UTF-8 bytes;
- 4096 retained requests globally;
- 64 pending requests per server;
- 16 pending requests per requester;
- pending lifetime: 7 days;
- resolved record retention: 24 hours;
- rejected requester/server pair cooldown: 1 hour.

There can be only one pending request for one requester/server pair. Repeating the same create call
returns the existing pending request rather than creating duplicates.

Expired requests are pruned before join-request reads/writes and during directory open.

## API

### Create

`POST /v1/join-requests`

Body:

```json
{
  "server_code": "CAT-....",
  "message": "optional reason"
}
```

Authenticated requester only.

Reject when:

- code is unknown;
- requester is already a member;
- requester is the owner;
- server is at member capacity;
- requester/server cooldown is active;
- requester pending-request bound is reached;
- server pending-request bound is reached;
- global retained-request bound is reached.

### Requester status

`GET /v1/join-requests?mine=1`

Returns the requester's retained requests, newest update first. This is how a Windows client notices
approval or rejection without requiring a push infrastructure milestone.

### Owner queue

`GET /v1/join-requests?server_id=<internal-id>`

Requires that the caller is the server owner. Returns pending requests only, oldest first, with
requester display name and optional message.

### Decision

`POST /v1/join-requests/decision`

Body:

```json
{
  "request_id": "...",
  "decision": "approve"
}
```

or `reject`.

Owner-only. Approval atomically:

1. re-validates owner authorization;
2. re-validates request is pending/unexpired;
3. re-validates requester still exists;
4. re-validates server member capacity;
5. adds `directoryMember{role:"member"}`;
6. marks request approved with 24-hour resolved retention;
7. persists the combined state;
8. rolls back both membership and request state if persistence fails.

Reject changes only request state and persists atomically.

### Cancel

`POST /v1/join-requests/cancel`

Requester can cancel only their own pending request.

## Authorization model

Server Code knowledge grants only minimal preview and the ability to request membership.

It never grants:

- membership;
- roster visibility;
- text history;
- RTC provisioning;
- invite creation;
- join-request decision rights.

Every endpoint re-authorizes using the authenticated directory identity and current persistent state.
No client-supplied role is trusted.

## Windows experience

### Add-server button

The existing `+` button becomes **Add server**.

First dialog offers two existing-compatible paths:

- Find by Server Code;
- Join directly with Invite Code.

Server Code path:

1. user enters exact Server Code;
2. client performs lookup in the background;
3. preview dialog shows server name and member count;
4. if relationship is none, user may enter an optional 280-byte request note;
5. user sends request;
6. pending state is shown without exposing any server content.

Invite path remains the current direct-join behavior.

### Requester synchronization

MainWindow owns a five-second outgoing-request timer after directory bootstrap.

Rules:

- one status request in flight;
- network work off the XAML thread;
- approved request triggers a fresh authoritative `GET /v1/servers`;
- newly joined server appears automatically in the server rail;
- rejected/cancelled state is reflected in the Add Server tooltip/status;
- stale async results cannot overwrite a later bootstrap generation.

### Owner access panel

The server header gains an owner-only **Access** control.

Opening it shows:

- read-only selectable Server Code;
- pending-request count;
- virtualized/selectable pending request list;
- requester display name;
- optional message;
- submitted time;
- Approve;
- Reject.

Approval/rejection always goes back to the service. UI state is never treated as authoritative.

The existing Invite button remains available and intentionally bypasses the request queue because an
invite is an owner-created direct-join capability.

## Performance

- no discovery or request processing on realtime media threads;
- exact lookup only, no search index;
- no background global discovery crawler;
- requester status polling: 5 seconds while app is running;
- owner queue polling: 5 seconds only while an owned server page is loaded;
- one request in flight for each poll family;
- bounded request list and native virtualization;
- no effect on voice/screen/message transport queues.

## Abuse and privacy

Controls:

- 80-bit non-sequential Server Code;
- exact-match lookup;
- authenticated lookup/request;
- dedicated lookup rate limiter;
- existing mutation rate limiter on create/decision/cancel;
- pair deduplication;
- pending bounds;
- global retained bound;
- request expiry;
- rejection cooldown;
- member-capacity revalidation on approval;
- generic lookup failure;
- minimal preview;
- owner-only request queue and decisions;
- no user/IP labels in metrics.

A future moderation milestone may add block lists, bans, request questions, code rotation, multiple
moderator roles, push notifications, and fully public discovery. Those are intentionally not
silently folded into this slice.

## Persistence migration

`directoryState` gains `join_requests`.

On open:

- initialize nil maps;
- generate and persist missing Server Codes;
- reject duplicate/malformed persisted codes;
- validate every join request references an existing server and requester;
- validate status/time/message bounds;
- prune expired records;
- reject duplicate pending requester/server pairs;
- enforce global/per-server/per-requester pending bounds.

## Tests

Backend:

- Server Code format/uniqueness/migration;
- lookup minimality and relationship states;
- lookup limiter;
- create dedupe;
- server/requester/global pending bounds;
- already-member rejection;
- rejection cooldown;
- outsider cannot read owner queue;
- non-owner cannot decide;
- approve atomically creates membership;
- reject does not create membership;
- requester cancel;
- expiry/pruning;
- malformed persisted request state rejected.

Windows:

- strict Server Code/preview/request JSON parsing;
- Add Server keeps invite direct join;
- approved outgoing request refreshes server rail;
- owner Access control only for owner;
- join-request poll timers are bounded and single-flight;
- UI policy keeps dynamic request lists virtualized.

## Production acceptance

Two real packaged Windows clients on different networks must prove:

1. Client A displays its Server Code.
2. Client B finds the exact server by that code.
3. Client B cannot see roster/messages/channels before approval.
4. Client B submits a request.
5. Client A sees the pending request without joining voice.
6. Client A approves.
7. Client B automatically gains the server in its rail.
8. Both clients converge on the authoritative member roster.
9. A rejected request does not grant access.
10. Restart/backup/restore preserves pending requests and Server Codes.
