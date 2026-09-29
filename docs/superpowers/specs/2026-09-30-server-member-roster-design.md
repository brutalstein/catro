# Authenticated server member roster — design

Date: 2026-09-30
Status: implementation target

## Goal

Make the existing member rail truthful for shared servers. The rail must represent authoritative
server membership rather than continuing to show only the local identity while merely changing the
member count.

This slice deliberately implements membership roster only. Presence, online/offline state, typing,
voice activity, moderation, role editing, kicks/bans, avatars, and per-user voice controls remain
separate product work.

## Existing gap

The directory already persists `directoryServer.Members` and invite acceptance mutates that map.
However, `serverDescriptor` exposes only `member_count`, and the Windows `ServerView` keeps a
single hard-coded/local member row. This violates the documented UI contract that the member pane
reflects server membership, not voice membership.

## Server API

Add an authenticated read-only endpoint:

`GET /v1/members?server_id=<id>`

Requirements:

- bearer authentication is mandatory;
- requester must be a member of the requested server;
- response contains no credentials, invite data, IPs, RTC tokens, or media state;
- maximum returned members is the existing `maxDirectoryMembers` bound;
- each item contains only `user_id`, `display_name`, and `role`;
- roles are only `owner` or `member`.

Ordering is deterministic:

1. owner first;
2. remaining members by case-insensitive display name;
3. exact display name as tie-breaker;
4. user id as final tie-breaker.

The endpoint is a GET and does not consume the mutation rate limiter.

## Persistent-state hardening

Directory load validates the membership graph before serving:

- every member map key equals `member.UserID`;
- every member references an existing user;
- every role is exactly `owner` or `member`;
- the server owner exists in the member map;
- the owner member has role `owner`;
- no other member has role `owner`.

This converts malformed membership state into a startup error instead of rendering a misleading
roster.

## Windows directory client

Add:

```cpp
struct DirectoryMember {
    std::string user_id;
    std::string display_name;
    std::string role;
};

DirectoryMembersResult list_directory_members(
    const DirectoryServiceConfig&,
    std::string_view access_token,
    std::string_view server_id) noexcept;
```

The response parser enforces:

- valid bounded ids;
- bounded non-empty display names;
- valid role;
- at most `community::kMaxMembers` rows;
- no duplicate user ids;
- owner appears first and only once;
- deterministic ordering from the service is preserved.

## Windows UI

Replace the static member row with a native virtualized `ListView`.

Local-only fallback:

- before an authenticated directory session exists, render the local identity from persisted local
  state as the single owner row.

Shared-server mode:

- clear fallback rows on server context change;
- fetch the authoritative roster immediately;
- refresh at a bounded five-second interval only while the server page is loaded;
- allow at most one roster request in flight;
- use a generation token so a late response from a previously selected server cannot mutate the
  current rail;
- keep the last valid roster visible across transient network failures;
- update `MEMBERS — N` from the successful roster snapshot.

The member rail is server membership. Joining/leaving a voice room does not add or remove rows.

## Performance

- no network work on the XAML thread;
- no per-frame/presence polling;
- one GET every five seconds while visible;
- virtualized native list;
- at most 256 rendered data items, bounded by the server contract;
- no realtime media thread touches membership state.

## Security

- a non-member cannot enumerate a private server roster;
- display names are rendered as native text only;
- client role labels are descriptive; authorization remains server-side;
- metrics receive no member/user labels.

## Validation

Automated gates:

- owner/member roster visibility;
- outsider rejection;
- owner-first deterministic ordering;
- malformed persisted membership rejection;
- Windows client strict response validation;
- UI policy locks in virtualized member list and bounded refresh;
- full existing CI matrix remains green.

Real-network acceptance later verifies that accepting an invite on Client B causes Client A and
Client B to converge on the same roster without either client joining voice.
