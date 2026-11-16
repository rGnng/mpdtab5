# ColoPlay BLE controller

The separate ColoPlay peer driver adds bidirectional controls to the existing
Bluetooth controller selection. The current paired release is ColoPlay
**0.8.2-sleep-sync**, built from the working tree based on **`1f24f5d`**; image hashes
and live checks are in the [release record](../coloplay/docs/development.md#current-verified-release).
The driver originally built on `a34fa8a` and retains the existing
`volume_grid_step` helper and MPD command worker. D100H discovery/heartbeat and
its input mappings remain in place. One Bluetooth controller can be connected at a time;
connecting ColoPlay retains other saved controllers.

## Navigation feedback and direct-control scene

The optional navigation extension adds current view and focused-item feedback
without changing Tab5's existing key actions. On updated ColoPlay firmware,
choose **Controls → + Tab5 navigation**, then **Activate scene**. Its new scene
uses knob turn/press for navigation/select, Button 1 for Back, Buttons 2/3 for
previous/next view, and Button 4 for context. Holding Buttons 2/3 starts repeated
Page Up/Down after 350 ms; repeat/release timing belongs to ColoPlay. Press-and-
turn keeps volume control. Previous view on Now Playing retains the Queue
shortcut. Existing controller profiles and USB/D100H handling remain intact.

ColoPlay's navigation screen shows the focused item, or the focused command in
a context menu. On Now Playing it uses the playback screen. Both applications
must support `CP_CAP_NAVIGATION=128`; old peers continue their existing protocol.
No pairing reset or configuration migration is required on Tab5.

A UI-thread timer copies a read-only snapshot every 60 ms, using selected
library metadata for virtual rows and detail/queue tracks, and readable labels
for ordinary controls. It sends no LVGL pointers to the BLE task. A one-entry
mailbox coalesces changes. A changed snapshot wakes the BLE event loop
immediately; write completions advance the bounded stream alongside media/volume
traffic. It does not wait for the 250 ms supervisor timer. Unchanged snapshots
refresh every second.

All frames remain 20 bytes, with protocol version and session unchanged:

| Kind | Bytes 2–3 | Payload |
|---|---|---|
| 21 `CP_NAVIGATION` | Nonzero revision, wrapping 65535 → 1 | View in 8, flags in 9, zero-based index in 10–13, count in 14–17, UTF-8 focus length 0–95 in 18–19 |
| 22 `CP_NAV_TEXT` | Matching revision | Byte offset in 8, length 1–10 in 9, text in 10–19 |

Fields are little-endian. View IDs 0–6 are Now Playing, Albums, Artists, Songs,
Playlists, Bluetooth and Settings. Flags are 1 Context, 2 Editing, 4 Loading,
8 Focus available, 16 Queue and 32 Screen on. Counts are current list/focus-group
positions, not always the total number of library tracks. The receiver commits
only complete matching revisions and expires navigation separately after five
seconds. Rapid changes replace unsent text, preventing a long focus backlog.

The producer/legacy-peer fixture, complete UTF-8 fragmentation and all 21 native
UI tests pass. Native tests read real album/artist/song selection, detail-track
metadata, context commands, Settings and playlist labels, and confirm snapshot
reads do not move selection. Application-only OTA is also verified with ColoPlay 0.6.33. The current Tab5
ELF SHA-256 and live round-trip checks are recorded in
[navigation OTA validation](mpd-validation.md#coloplay-navigation-ota-verification--2026-11-16).

USB CDC-ACM is also available through Tab5's USB-A port; see the
[wired controller guide](coloplay-usb.md). Both transports use the same controls
and feedback; USB owns input after its handshake.

## Current-track star feedback

Release 0.8.1 replaces the star icon with a small gold dot below
the elapsed/total or remaining-time line, shown only for a confirmed starred
song. It also appears in Navigation's Now Playing view. The dot keeps its
footer position when time is hidden and is independent of the Status icon switch. This describes the
current track, independently of the item selected in a library/context menu.
No current track, stale/disconnected feedback or an older peer hides the icon.

Optional feature byte 17 advertises `CP_FEATURE_STAR` in ColoPlay capabilities
and the returning Tab5 HELLO. Negotiated `CP_STATE` flags use bit 2 for known
and bit 3 for starred; seek bits 0/1 and backend bit 7 retain their meanings.
Legacy peers receive the original flags. USB negotiates the same feature via
OFFER/HELLO. This adds no packets and makes no prediction of a star change.
The live saved-binding check confirmed both star states over USB and Bluetooth
standby, restored the original state and retained USB command ownership.

## Pairing and existing transport

On ColoPlay, enable Connection → Bluetooth and Start pairing. Scan/select ColoPlay
in Tab5's existing Bluetooth page. Encrypted bonding and reconnect reuse the
current supervisor and saved-controller list. The private UUID service is
`a91c0001-c169-4acb-9f5d-121d621d17b5`;
the [shared protocol header](../shared/coloplay_ble_protocol.h) defines 20-byte
frames for both applications. Compatibility headers retain their existing
include paths.
The extension carries commands once, without also injecting their HID reports.

Supported controls: Volume Up/Down using Tab5's saved Volume Scale, mute, Play/Pause,
Play, Pause, Stop, Previous/Next Track, bounded Fast Forward/Rewind and navigation
keys. The raw MPD volume, playing/paused/stopped state, seek direction, position,
track identity and bounded title/artist are sent back. No mixer is reported as
-1. ColoPlay step fields from older firmware are ignored. Existing local speaker
volume is independent. Keyboard support is unmodified arrows, Return/Escape,
Page Up/Down, brackets, C and digits 1–7. `*` toggles the currently playing song's
server star from any playback/library screen. Bind **MPD Tab5 → Tab5 key** to
**usage 85 (`0x55`, keypad multiply), modifiers 0** in ColoPlay's scene editor.
Shift+8 is also accepted: usage 37 (`0x25`), modifier 2 (left Shift) or 32 (right
Shift). These are HID usages, not ASCII codes. C is usage 6, modifiers 0, and
opens/closes context menus on Now Playing, Albums, Artists, Songs, and Up Next.
The menu acts on the selected item; its Star/Unstar action can target a song
that is not playing. Page Up/Down only page lists and never open Up Next.
Brackets switch tabs, with `[` toggling Up Next on Now Playing. See the
[controls reference](controls.md) for menu actions and HID usage values.
Other modified keys are rejected.

Volume uses normalized floats on BLE and fractional percentages on MPD, matching
[Tracks `628ee5b`](https://github.com/rGnng/tracks/commit/628ee5b).
For example, `setvol 6.25` requests raw scalar 0.0625. Status is parsed as the
actual mixer scalar, which can differ from the request after device quantization.
Squared targets use up to eight decimal percent digits; integer profiles send
whole percentages. This avoids the earlier whole-percent round trip that could
stall repeated turns at 5%. An explicit invalid-argument ACK from an
integer-only MPD server triggers
one rounded retry and selects integer commands until reconnect. Permission and
network errors are not retried; unrelated external mixer changes remain authoritative.

Commands enter the existing MPD worker queue. Results follow backend processing
and polling; initial Accepted means queued. The BLE driver targets a 400 ms
state interval when its queue has room; supervisor scheduling and GATT writes
affect the observed cadence. Results also wake a NimBLE event. The GATT driver keeps bounded queues,
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


Historical host-owned volume release (before squared profiles and prediction):
ColoPlay sends direction and detent count only.
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

### macOS mixer findings

The tested Tracks/macOS output accepts fractional MPD percentages but returns
quantized actual mixer values. Observed request/readback pairs include raw
25% → 25.3953% and 28.125% → 27.8319%. These fit a 128-level squared model,
`raw = (n / 127)²`, `n = 0..127`. This is an inference for that output, not a
guarantee about every macOS/CoreAudio device. Integer requests can also return
different values; integer profiles do not bypass hardware quantization.

The 16/64-step control grids are independent of that inferred device grid.
Each squared control position `x` requests raw `x²` through absolute `setvol`;
actual feedback maps to the nearest selected control target. For example,
control position 25% requests raw 6.25%, and raw readback 6.3488% maps back to
25%. Keeping target and echo separate avoids adding a fixed raw step to an
already quantized readback, while the common display mapping prevents the
earlier predicted/actual numerical bounce. External changes invalidate the
logical reference.

Both devices use 154 compile-time raw targets (616 bytes): 17/65 for the squared
profiles and 51/21 for the 2%/5% integer profiles. These are control tables, not
a hard-coded 128-entry macOS hardware table. Nearest lookup uses binary search;
there is no runtime square root or logarithm. Tab5 sends the selected scale,
actual raw scalar, confirmed raw target and completion watermark over BLE so
ColoPlay can predict promptly and reconcile pending turns. Its physical volume
screen shows only the icon and bar; raw values remain available in diagnostics.
See the [full findings and examples](../coloplay/docs/tab5-integration.md#observed-macos-volume-scale).


Scale validation (2026-11-15): 101 MPD tests, 8 scale/volume tests, the
extended/legacy BLE fixture and 21 rebuilt native UI CTests passed. Every
squared/linear table position round-trips; simulated device quantization maps
back to the intended squared control position. Native keyboard/touch and web
checks cover all four choices. Application-only OTA verified ELF SHA-256
`ff8d0e65d739760ce8ecd418658e95951cf83d530d5c6aca91676210f0da8c68`.
The live check synchronized all four profiles to ColoPlay, matched both
readouts, preserved raw playing volume and restored the saved profile.

## Detail navigation feedback (0.8.1)

Both updated images were OTA-tested on 2026-11-16. They send semantic feedback over
USB and Bluetooth without changing Tab5's input actions or saved settings:

| View | Heading | Focus and position |
| --- | --- | --- |
| Album detail | Album name | Play/Shuffle without a counter; songs start at 1/n |
| Artist detail | Artist name | Play/Shuffle without a counter; album and song positions count their own items |
| Playlist detail | Playlist name | Play/Shuffle without a counter; songs start at 1/n |
| Settings | Settings or selected section | Setting name/current value; actionable-row position |
| Clock editor | Set time | Field name/value through editing blink; Increase/Decrease controls without a field counter |

Settings values refresh during editing. Decorative headers and playback
buttons do not inflate song positions. The optional heading stream uses
`CP_FEATURE_NAV_HEADING`; see the [shared protocol](../shared/README.md) for its frame layout.
Legacy peers retain the original navigation frame format and generic headings.

## Shared sleep and wake (0.8.2)

Negotiated `CP_FEATURE_SLEEP` adds an independent Tab5 sleep/timer snapshot and
activity, sleep and wake requests. The UI policy stays on the Tab5 UI thread;
BLE callbacks only queue logical input. Playing holds its countdown, manual
blanking follows the existing policy and first wake input is consumed. ColoPlay
mirrors screen/LED state and keeps its own local timer only as a fallback when
feedback is unsupported or stale. USB keeps ownership when connected; the same
sleep messages continue over BLE when it takes over. The [shared contract](../shared/README.md#shared-sleep-and-wake)
defines bounds, freshness and byte layout. Physical versus host-fixture test
scope is recorded in the [OTA validation](mpd-validation.md#shared-sleep-and-wake-ota--2026-11-16).
