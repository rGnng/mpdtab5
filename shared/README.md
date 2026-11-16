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
