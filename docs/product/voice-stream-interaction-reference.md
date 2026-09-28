# Voice + Stream Interaction Reference

Research date: 2026-09-28

This document records the interaction contract Catro should learn from Discord's current official
desktop documentation. It is a behavior reference, not a pixel-for-pixel visual clone: Catro keeps
its own branding and visual identity while matching the useful voice/stream interaction model.

## Official references

- Discord — Go Live and Screen Share:
  https://support.discord.com/hc/en-us/articles/360040816151-Go-Live-and-Screen-Share
- Discord — Voice Channels FAQs:
  https://support.discord.com/hc/en-us/articles/19583625604887-Voice-Channels-FAQs
- Discord — Voice and Video Troubleshooting Guide:
  https://support.discord.com/hc/en-us/articles/360045138471-Discord-Voice-and-Video-Troubleshooting-Guide
- Discord — Windows application-window capture:
  https://support.discord.com/hc/en-us/articles/9410427556375--Windows-Capturing-Application-Window-for-Screen-Share-and-Go-Live
- Discord — Video Calls:
  https://support.discord.com/hc/en-us/articles/360041721052-Video-Calls

## Voice-room contract

1. Joining voice is independent from watching a stream.
2. Voice controls expose mute and deafen state clearly.
3. Per-user volume is a user-level control; it must not alter the room's global mix.
4. Voice permissions conceptually separate Connect, Speak, and Video/screen-sharing capability.
5. Leaving a watched stream must not leave voice.

## Broadcaster contract

1. Screen sharing starts from the voice surface.
2. The user chooses either an application/window or a whole display before going live.
3. The final action is explicit (Discord calls this "Go Live"); source selection alone must not
   begin capture.
4. Resolution and frame rate are configurable while streaming.
5. A local preview/PiP is presentation-only. It must never be required for capture, encode, or
   transport.
6. Catro should pause or suppress its local preview while a game is focused, because rendering a
   self-preview is wasted GPU work when the user cannot see it.
7. Changing the captured window should be a first-class future action rather than requiring a full
   voice reconnect.

## Viewer contract

1. A live stream is advertised separately from voice participation.
2. The viewer explicitly chooses "Watch Stream".
3. Staying in voice without watching is valid and should avoid decode/presentation GPU work.
4. "Leave Stream" stops only viewing. Voice remains connected.
5. The viewing surface is resizable and must preserve the source aspect ratio.
6. A future pop-out mode should support arbitrary resizing and optional always-on-top behavior.
7. Stream audio should eventually have independent mute/volume control.
8. For multiple simultaneous streams, a future grid/focus model should let one stream become the
   primary surface without forcing all streams to decode at full quality.

## Natural-size / aspect-ratio rule

The selected source owns the geometry.

- Catro does not force a 16:9 viewing box onto a 16:10 game, 4:3 game, portrait window, browser
  window, or any other source.
- Encode dimensions remain bounded by the user's quality ceiling, preserve source aspect ratio, do
  not upscale smaller sources, and stay legal for NV12/H.264.
- Presentation uses the decoded stream's actual width/height to fit the largest possible viewport
  inside the available UI bounds without stretching.
- If the source window changes size while live, capture/encode/presentation must reconfigure to the
  new geometry.

## Windows capture guidance

Discord's official Windows note documents Windows Graphics Capture as a modern application-window
capture path and notes that exclusive fullscreen games are a special case. It recommends borderless
mode for best screen-share behavior.

Catro intentionally does not inject code into games. Its policy remains:

- ordinary windows: Windows Graphics Capture;
- full displays and fullscreen/game-like windows: DXGI Desktop Duplication;
- same-adapter GPU processing;
- no raw uncompressed frame readback to CPU.

## Catro implementation state after this iteration

Implemented:

- voice membership remains independent from stream viewing;
- remote stream presence is detected while decode/presentation stays off;
- explicit Watch Stream / Leave Stream controls;
- leaving a stream keeps voice and RTP room transport alive;
- decoder/presenter GPU resources are lazy and only activated for a viewer;
- remote and local stream surfaces preserve the actual encoded/decoded aspect ratio;
- resizing/reconfiguration follows changing source dimensions;
- local self-preview remains independently suspendable.

Future Discord-like interaction work:

- participant identity + LIVE badge on the actual remote member row;
- stream preview policy/thumbnail before watching;
- in-stream quality changes without restarting the share;
- Change Window while live;
- viewer stream-volume control once application audio transport exists;
- pop-out + always-on-top;
- multi-stream grid/focus model;
- viewer count / watching indicator;
- automatic local PiP suspension when the game becomes foreground.
