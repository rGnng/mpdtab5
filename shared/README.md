# Shared controller protocol

Tab5 and ColoPlay build the same definitions from this directory:

- `coloplay_ble_protocol.h`: control/state messages, capabilities and navigation snapshots.
- `volume_grid.h`: volume-scale mapping and confirmed-grid cursor logic.
- `volume_scale_tables.h`: generated constant targets for the four volume scales.
- `coloplay_usb_frame.h`: bounded USB serial framing and CRC32 around those payloads.

Compatibility headers at the existing firmware/backend include paths forward
here, so both applications keep their current build layout. Update shared
definitions once and validate both firmware images before release.

Regenerate the constant tables from the repository root:

```sh
python3 tools/gen_volume_scales.py
python3 -m unittest discover -s tests -p 'test_shared_controller.py' -v
```

The BLE wire version and volume mappings remain unchanged. The
[USB CDC-ACM link](../docs/coloplay-usb.md) wraps the same payloads in its own
versioned stream envelope. Existing BLE control remains available.

Star feedback uses optional feature byte 17, bit `CP_FEATURE_STAR`, in ColoPlay's
GATT capabilities or USB OFFER and the returning Tab5 HELLO. Only a supporting
peer receives the `CP_STATE` flag bits `CP_STATE_STAR_KNOWN` (bit 2) and
`CP_STATE_STARRED` (bit 3). These describe Tab5's confirmed current track;
missing track/backend or older peers leave them clear. Packet lengths, wire
version, volume/timing fields and USB framing are unchanged. ColoPlay clears the
visible marker on stale/disconnected feedback and hides it when state is unknown.

Navigation headings use `CP_FEATURE_NAV_HEADING` (bit 1 of feature byte 17),
negotiated with the existing navigation capability. Updated peers use kind 23
`CP_NAVIGATION_HEADING`: view/flags and 32-bit index/count stay at 8–17; byte 18
is focus UTF-8 byte length, byte 19 is heading UTF-8 byte length (each 0–95).
Kind 22 carries focus fragments; kind 24 `CP_NAV_HEADING_TEXT` carries heading
fragments with the same revision, byte offset at 8, length 1–10 at 9 and text
at 10–19. Both fields must complete before publishing a revision. Legacy peers
retain kind 21 with the original uint16 focus length and no heading fragments.
Empty headings restore the generic view title. Stale/partial revisions do not
mix an old heading with a new focus label.

## Playback metadata

`CP_TRACK` identifies a new track; repeating its existing key does not clear
text or reset playback timing. `CP_TEXT` carries title (field 1) and artist
(field 2), a byte offset, up to 11 data bytes and a final-fragment flag. Each
field is a complete UTF-8 prefix of at most 63 bytes. Fragments may split a
code point, but the receiver publishes only a contiguous completed field.
Tab5 sends text on change and refreshes it about every five seconds over USB
and BLE without another `CP_TRACK`. This repairs a lost fragment while keeping
the current display and clock. These are existing messages, with no wire-version
or capability change. Artist takes precedence over AlbumArtist; missing/blank
Artist falls back to AlbumArtist, then Unknown artist.

## Shared sleep and wake

`CP_FEATURE_SLEEP` is bit 2 of feature byte 17 in capabilities/USB OFFER and
Tab5 HELLO. Only negotiated peers receive kind 25 `CP_SLEEP_STATE`. It carries
flags at byte 8 (awake=1, countdown running=2, playing=4, manual blank=8),
reserved zero at 9, timeout minutes at 10–11 and remaining milliseconds at
12–15; bytes 16–19 are zero. Timeout values are Tab5's 0, 30 or 60 minutes.
The UI thread publishes this snapshot independently of MPD availability and
navigation; transport tasks copy it through bounded mailboxes. USB sends it
with its 250 ms state cadence; BLE uses its 400 ms cadence. Frame size, wire
version and existing state/time fields remain unchanged.

Kind 5 `CP_SLEEP` requests use the normal session, nonzero request ID and
repeat-count field of 1. Usage at 8–9 is Activity=1, Screen Off=2 or Wake=3;
bytes 12–19 are zero. Activity and Wake reset Tab5's timer without invoking a
key binding; Screen Off uses its existing manual blank policy. Requests use
normal transport ownership, deduplication and result routing. Input arriving
while Tab5 is asleep wakes it and is consumed, including volume/media commands.
ColoPlay suppresses a physical wake gesture through release, and throttles
held/rotary activity updates. No sleep command is emitted to a legacy HID peer.

Tab5 owns connected screen/LED sleep. ColoPlay follows fresh sleep state and
extrapolates the displayed remaining time without independently deciding when
to blank. Sleep freshness expires after two seconds; missing support or lost
feedback restores the unchanged local fallback timer. Neither remaining time
nor negotiated settings are written to ColoPlay NVS.

The ColoPlay key picker also defines `CP_KEY_SCREEN_OFF=0x0182`: a qualified
Generic Desktop/System Sleep identifier, distinct from keyboard-page usage
`0x82`. It is stored in a `bluetooth_key` action without modifiers. Both ColoPlay
send adapters translate it to `CP_SLEEP_OFF` before wire validation/encoding,
so no raw extended keyboard code or native computer power report is emitted.
The existing negotiated sleep protocol and Tab5 firmware remain unchanged.
