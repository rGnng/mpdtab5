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

Supported controls: volume host-step or explicit 1/16 and 1/64, mute, Play/Pause,
Play, Pause, Stop, Previous/Next Track, bounded Fast Forward/Rewind and navigation
keys. The raw MPD volume, playing/paused/stopped state, seek direction, position,
track identity and bounded title/artist are sent back. No mixer is reported as
-1. Explicit grids do not modify the saved native grid. Existing local speaker
volume is independent. Keyboard support is unmodified arrows, Return/Escape,
Page Up/Down, brackets, C and digits 1–7. `*` toggles the currently playing song's
server star from any playback/library screen. Bind **Bluetooth HID key** to
**usage 85 (`0x55`, keypad multiply), modifiers 0** in ColoPlay's scene editor.
Shift+8 is also accepted: usage 37 (`0x25`), modifier 2 (left Shift) or 32 (right
Shift). These are HID usages, not ASCII codes. C is usage 6, modifiers 0, and
opens/closes the context menu on Now Playing. Other modified keys are rejected.

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
