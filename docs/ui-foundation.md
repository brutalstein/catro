# Native UI foundation

Catro uses a server-first native shell with the familiar gaming-chat layout: server rail, channel
rail, active channel, and member list. The visual tree is deliberately flat and effect-free so the
game and realtime media pipeline keep the machine budget.

## Product contract

First run creates one personal server for the local identity. The account/persistence milestone must
supply durable opaque identifiers; the UI never treats a display name as identity.

The personal server contract is:

- one identity owns its personal server;
- owner is the only elevated role;
- everyone else is a normal member;
- joining happens through an opaque invite code;
- the default server contains exactly one text channel (`general`) and one voice channel
  (`Voice`);
- voice is where mute, deafen, and screen-share controls live;
- the member pane always reflects server membership, not voice membership.

The current branch implements the shell plus durable local identity, authenticated shared-server
membership/invites, authoritative member rosters, bounded persistent text messaging, production
voice rooms, and screen streaming.
The UI still keeps one default text channel and one default voice channel per server; arbitrary
channel creation, DMs, presence, reactions, edits, and attachments remain outside this slice.

## Windows layout

The main window uses four bounded regions:

1. **64 px server rail** — personal server now; joined servers later.
2. **232 px channel rail** — server header, one text channel, one voice channel, local profile.
3. **active channel** — text timeline/composer or voice stage.
4. **216 px member rail** — server members and owner indication.

Below 920 logical pixels the member rail is removed. Below 760 logical pixels the channel rail
shrinks to 196 px. Resize handling is constant-time C++ with two comparisons and no visual-state
graph.

System diagnostics and Settings remain reachable from the server rail. Diagnostics is created only
when opened and released when leaving.

## Performance invariants

- no Electron, browser runtime, WebView UI, Mica, Acrylic, blur, backdrop, large shadow, or
  decorative bitmap;
- no continuous UI animation;
- solid semantic brushes only;
- only one top-level workspace attached to the window at a time;
- fixed-count server/channel chrome on first run, so ordinary navigation is O(1);
- dynamic message/member lists must use virtualization when their real models arrive;
- realtime media never executes on the XAML thread;
- speaking/meter state must be snapshot-driven and rate-bounded;
- diagnostics and device probing are not kept alive while the normal server surface is visible.

The UI policy test rejects expensive composition primitives and the stock blue accent colors from
the product XAML.

## Visual system

The application deliberately avoids a blue theme. Light and dark palettes use warm neutral
backgrounds with muted terracotta as the primary accent, desaturated sage for healthy/connected
state, and dusty rose only for secondary semantic emphasis.

The product surface stays dense: 36–54 px navigation rows, small labels, minimal empty-state copy,
and no marketing text inside the application.

## Integration boundaries

The shell should receive immutable/snapshot-style models from future services:

- **Identity**: stable user id, display name, avatar token.
- **Server**: stable server id, owner id, display name.
- **Invite**: opaque code, server id, expiry/revocation state.
- **Channels**: stable channel id, kind, name, ordering.
- **Members**: implemented as membership-authorized, owner-first virtualized snapshots containing
  user id, display name, and owner/member role. Presence is intentionally not inferred yet.
- **Text**: implemented as a virtualized native timeline plus bounded HTTPS history/send commands; polling runs only while the text channel is active and never on the XAML thread.
- **Voice**: join/leave/mute/deafen/share commands plus bounded speaking/session snapshots.

XAML controls do not own those records. The existing native audio/voice core remains below these
presentation contracts.
