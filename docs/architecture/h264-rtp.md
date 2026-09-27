# H.264 RTP packetization slice

This slice is the transport boundary immediately after the validated Windows hardware H.264 encoder.
It is portable C++ and intentionally contains no socket, Windows, Media Foundation, or UI types.

## Why RTP / RFC 6184

Catro should not invent a proprietary video framing format that later has to be thrown away for an
SFU. The packetizer therefore emits the standard RTP fixed header and the RFC 6184 H.264 payload
forms needed by the current encoder:

- one NAL unit per RTP packet when it fits the path MTU;
- FU-A fragmentation for larger NAL units;
- RTP marker on the final packet of an access unit;
- a 90 kHz video clock.

STAP aggregation is deliberately omitted for the first two-client path. It saves little for the
large screen-content NAL units that dominate bandwidth and adds parser complexity to the first
validation slice.

## Hot-path resource rules

The packetizer performs no heap allocation and does not copy encoded H.264 payload bytes. It exposes
a small stack prefix (12-byte RTP header plus optional 2-byte FU-A header) and a span that aliases the
original encoded access unit. A socket layer can send those two segments with scatter/gather I/O.

The reassembler also performs no packet allocation. Its maximum frame memory is supplied once by the
caller. Fragments are copied exactly once into that bounded frame buffer because a decoder needs one
contiguous Annex-B access unit. A frame that exceeds the configured storage is dropped rather than
growing memory under load.

The timestamp conversion uses exact integer ratio 9/1000 from 100 ns units to the RTP 90 kHz clock;
no floating-point operation exists in the media packet timing path.

## Loss behavior

The first reassembler tracks a single live frame. Any RTP sequence gap marks that frame damaged. The
damaged frame is discarded at its marker and never reaches a decoder. A new timestamp starts a new
frame, so stale/incomplete data cannot accumulate latency or memory.

This slice intentionally does not add retransmission, FEC, congestion control, encryption, sockets,
or decoder/display code. Those belong to the two-client transport/runtime layer above this bounded,
portable packet format.
