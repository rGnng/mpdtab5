# Shared controller protocol

Tab5 and ColoPlay build the same definitions from this directory:

- `coloplay_ble_protocol.h`: control/state messages, capabilities and navigation snapshots.
- `volume_grid.h`: volume-scale mapping and confirmed-grid cursor logic.
- `volume_scale_tables.h`: generated constant targets for the four volume scales.

Compatibility headers at the existing firmware/backend include paths forward
here, so both applications keep their current build layout. Update shared
definitions once and validate both firmware images before release.

Regenerate the constant tables from the repository root:

```sh
python3 tools/gen_volume_scales.py
python3 -m unittest discover -s tests -p 'test_shared_controller.py' -v
```

The current wire version and volume mappings are unchanged by the repository
merge. USB CDC-ACM transport for the Tab5 link is planned; it is not implemented
by this import. Existing BLE control remains available.
