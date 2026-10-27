# HyperDRM 0.14.0

Add an **Output** choice in the Receiver panel: `JSON` (default, unchanged) or
`FlatBuffer, all instances`.

HyperHDR's JSON API binds a connection to one LED hardware instance and rejects a
per-command `instance`, so only the first instance ever received HyperDRM colors.
The FlatBuffer output sends the same 160x90 synthetic plane to HyperHDR's
FlatBuffer server (the PicCap port, default 19400) with a Register request for
origin `HyperDRM` at priority 149. HyperHDR decodes the plane once and broadcasts
it through `GlobalSignals::SignalSetGlobalImage` to every running instance, so one
connection drives all LED hardware instances. Priority, the 1.5 s duration, the
clear on stop, retry and handover behavior are unchanged; a disconnect also clears
the priority on the receiver side.

Confirmed on the 65QNED87A6B with two instances (236 and 38 LEDs): both list the
HyperDRM input at priority 149 and follow the picture. Auto PicCap keeps its JSON
fade, which addresses the first instance only, while PicCap's own FlatBuffer
stream covers every instance during a handover.

New native tests cover the request encoder (alignment, vtables, union tags, image
plane, reply decoding) and the client (framing, registration, newest-frame
coalescing, server error propagation). The JSON output, API-token authentication
and the layout probe are unchanged.
