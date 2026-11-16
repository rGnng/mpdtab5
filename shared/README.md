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
