# ColoPlay BLE controller

The separate ColoPlay peer driver adds bidirectional controls to the existing
Bluetooth controller selection. It builds on `a34fa8a` and uses its existing
`volume_grid_step` helper and MPD command worker. D100H discovery/heartbeat and
its input mappings remain in place. One controller can be connected at a time;
connecting ColoPlay retains other saved controllers.

On ColoPlay, enable General → Bluetooth and Start pairing. Scan/select ColoPlay
in Tab5's existing Bluetooth page. Encrypted bonding and reconnect reuse the
current supervisor and saved-controller list. The private UUID service is
`a91c0001-c169-4acb-9f5d-121d621d17b5`; the identical
`firmware/main/coloplay_ble_protocol.h` in both projects defines 20-byte frames.
The extension carries commands once, without also injecting their HID reports.

Supported controls: Volume Up/Down using Tab5's saved 1/16 or 1/64 grid, mute, Play/Pause,
Play, Pause, Stop, Previous/Next Track, bounded Fast Forward/Rewind and navigation
keys. The raw MPD volume, playing/paused/stopped state, seek direction, position,
track identity and bounded title/artist are sent back. No mixer is reported as
-1. ColoPlay step fields from older firmware are ignored. Existing local speaker
volume is independent. Keyboard support is unmodified arrows, Return/Escape,
Page Up/Down, brackets, C and digits 1–7. `*` toggles the currently playing song's
server star from any playback/library screen. Bind **Bluetooth HID key** to
**usage 85 (`0x55`, keypad multiply), modifiers 0** in ColoPlay's scene editor.
Shift+8 is also accepted: usage 37 (`0x25`), modifier 2 (left Shift) or 32 (right
Shift). These are HID usages, not ASCII codes. C is usage 6, modifiers 0, and
opens/closes the context menu on Now Playing. Other modified keys are rejected.

Volume uses normalized floats on BLE and fractional percentages on MPD, matching
[Tracks `628ee5b`](https://github.com/rGnng/tracks/commit/628ee5b).
For example, a fine upward step from 5% sends `setvol 6.25` and parses
`volume: 6.25` as 0.0625. Four decimal percent digits preserve every 1/64 step.
This removes the whole-percent round trip that could stall repeated turns at
5%. An explicit invalid-argument ACK from an integer-only MPD server triggers
one rounded retry and selects integer commands until reconnect. Permission and
network errors are not retried; unrelated external mixer changes remain authoritative.

Commands enter the existing MPD worker queue. Results follow backend processing
and polling; initial Accepted means queued. State publishes approximately every
500 ms and results also wake a NimBLE event. The GATT driver keeps bounded queues,
session nonces and recent IDs, and rejects stale callbacks/results after reset.
Custom seek flags are separate from USB/D100H holds; release/disconnect and a
10-second lease stop custom seeking. The existing MPD/backend timing is retained.

USB is pinned to its baseline component version 1.6.0 to prevent an incidental
component update during this change. No C6 firmware, partitions or bootloader
changes are needed; application-only OTA remains the deployment path.

Validation on the devices: encrypted pairing/reconnect, actual volume return,
100→98→100 native-64 and 100→94→100 coarse-16 round trips, pause/resume, both
seek directions/release, navigation acknowledgments, track metadata and continued
MPD/SD availability. ColoPlay screen/LED sleep leaves feedback running. API-driven
binding tests do not exercise physical buttons or establish over-the-air latency.
Previous/Next/Stop/Mute semantics are covered by the MPD fixture; those operations
were not used to disturb the current live queue for qualification.

Focused MPD (39), volume-grid (6), D100H lifecycle and ColoPlay GATT tests pass,
as do 21 native UI CTests. The untouched a34fa8a full Python suite already has
credential/legacy-fixture failures; comparing failure identities found no new
failures. See `tests/test_coloplay_ble.py` for production-driver tests covering
reordered characteristics, nonce/HELLO, raw state, deduplication, unsupported
keys, busy results and stale connection generations.


Fractional MPD update (2026-11-15): 100 MPD Python tests, 6 volume-control
checks, the ColoPlay driver fixture and 21 native UI CTests passed. The decimal
parser/sender regression covers escaping 5%, all 64 grid positions in both
directions, external changes and integer-only-server fallback. Application-only
OTA verified running ELF SHA-256
`045ef555a9034871641967235a2a317dae091a644c1dde34a453ce91e8ae5198`;
saved configuration and the MPD endpoint were preserved, BLE reconnected and
live feedback above 5% was observed. The evidence bundle is in the sibling
ColoPlay checkout's ignored `backups/volume-bar/tab5/` directory.


Host-owned volume control: ColoPlay sends direction and detent count only.
Before each volume command Tab5 polls MPD, computes the nearest grid point
in that direction, and issues absolute `setvol`. At exact grid points it moves
to the adjacent point. For a 1/64 grid, 25.40% moves down to 25% or up to
26.5625%. The first confirmed readback within half a grid step anchors the
requested point to the hardware's actual value. Subsequent equal readbacks
preserve that grid reference; external changes and MPD reconnect discard it, and a
changed grid uses fresh actual volume. The target is never substituted for BLE
feedback. Batches count all detents and use the same native grid. ColoPlay's
screen/ring show actual feedback with no local prediction.


Host-volume validation (2026-11-15): 102 MPD tests, 9 volume/grid tests, the
ColoPlay BLE fixture and 21 rebuilt native UI CTests passed. Application-only
OTA verified ELF SHA-256
`e03cd3fd693aa7a15b5d6cfd84728c7671c20a82838625757d7793c9f26fcece`.
Saved Tab5 settings and the MPD endpoint were preserved. Live repeated volume
reversals returned to the exact starting readback; the controller showed only
actual feedback and migrated its bindings to direction-only values.


Negotiated prediction extension: `CP_CAP_VOLUME_PREDICT` is advertised alongside
float feedback. HELLO reports the native grid. Extended `CP_VOLUME_STATE` keeps
actual float volume at bytes 8–11 and result at byte 12, adding step count at
13, the last completed volume request ID at 14–15 and confirmed grid target at
16–19 (-1 when unanchored). Target and completion ID are published atomically
with mixer state; a late result envelope may contain a newer snapshot watermark.
This permits immediate controller prediction and replay of unconfirmed turns
without overriding Tab5's absolute MPD operation or actual feedback. Older peers
continue receiving their original frame layout. No additional packets are used.


Four-profile volume scales: HELLO byte 8 and volume-state byte 13 carry IDs
16 (1/16 Squared Curve), 64 (1/64 Squared Curve, default), 2 (2% Integer) or
5 (5% Integer). `CP_CAP_VOLUME_SCALE` must be negotiated alongside prediction;
legacy peers fall back to raw feedback so they cannot misinterpret curved IDs.
The HELLO normalized increment is in control coordinates. State actual/target
floats remain raw MPD scalars. Display/prediction uses the selected scale and
its shared constant lookup tables. A squared display position `x` requests `x²`;
readback returns to the nearest logical position, including hardware-quantized
values. Integer profiles send whole MPD percentages. Saved choices use the
existing `volume_steps` JSON field; unrelated settings are preserved.


Scale validation (2026-11-15): 101 MPD tests, 8 scale/volume tests, the
extended/legacy BLE fixture and 21 rebuilt native UI CTests passed. Every
squared/linear table position round-trips; simulated device quantization maps
back to the intended squared control position. Native keyboard/touch and web
checks cover all four choices. Application-only OTA verified ELF SHA-256
`ff8d0e65d739760ce8ecd418658e95951cf83d530d5c6aca91676210f0da8c68`.
The live check synchronized all four profiles to ColoPlay, matched both
readouts, preserved raw playing volume and restored the saved profile.
