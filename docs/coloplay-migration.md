# ColoPlay repository migration

ColoPlay development now lives in this repository alongside the MPD Tab5
application. The original [rGnng/esp32colo](https://github.com/rGnng/esp32colo)
was a proof of concept for rebuilding the LifeSmart/ColoLight ColoPlay C169
firmware. Its code, factory-firmware findings and history remain a reference
for the Tab5 integration and other ColoPlay projects.

## History and layout

The source was imported into `coloplay/` with `git subtree add`, without
squashing. Import commit `354493c` has both the Tab5 `c65716f` and esp32colo
`0c741ec` histories as parents. Existing Tab5 source paths and build commands
stay in place; each device still builds its own application image.

- [Tab5 firmware](../firmware/README.md): ESP32-P4 application using its C6 BLE peer.
- [ColoPlay firmware](../coloplay/README.md): ESP32-S3 application, tools and hardware evidence.
- [Shared definitions](../shared/README.md): BLE protocol, volume grid and generated scale tables.

Thin compatibility headers forward existing includes to `shared/`. The wire
version, packet layout, scale IDs, table values and control behavior are
unchanged. `tools/gen_volume_scales.py` now writes the canonical shared table.
The integration test preprocesses both firmware include paths as C and C++
and verifies that they resolve to the same definitions.

## Local setup and recovery

Run ColoPlay build/test/update scripts from `coloplay/`; its relative paths
refer to that directory. Provision an ignored `firmware/include/secrets.h`
and a local PlatformIO environment as described in its
[build guide](../coloplay/docs/flash.md#tool-setup-and-build).

Credentials, compiled firmware, raw backups and build caches are not imported
into Git. The existing local provisioning and current recovery bundle were
copied to the new ignored `coloplay/` paths for this workstation. Historical
private backups remain in the original checkout. Use a recovery image that
supports the `CSR1` compact scene record introduced in 0.6.33; do not erase
NVS or replace factory bootloaders/partition tables for this migration.

## Validation on 2026-11-16

- Both production images build: ESP-IDF 5.5.6 for Tab5 and the pinned
  PlatformIO `coloplay` environment for ColoPlay.
- Shared definitions match the pre-import definitions, excluding comments.
  C/C++ include-path integration passes, and regenerated volume tables match.
- ColoPlay's `scripts/test_http_ota.sh` passes, including BLE, volume
  prediction, navigation, compact scene persistence, playback, USB and font checks.
- All 21 native Tab5 UI CTests pass.
- Tab5's full Python discovery runs 493 tests, with 25 skipped, eight failures
  and five fixture setup errors. The 13 failure/error identities exactly match
  the pre-import `c65716f` run; the new shared-header test passes. The inherited
  issues are WiFi credential tests and the album-artwork-sort, D100H-dispatch,
  fast-scroll, SD-presence and web-Bluetooth fixtures.
- Local documentation targets and anchors are checked before publishing.

This is a source migration, not a new OTA qualification. The last deployed
images and live navigation checks remain recorded in the
[Tab5 validation](mpd-validation.md#coloplay-navigation-ota-verification--2026-11-16)
and [ColoPlay release record](../coloplay/docs/development.md#current-verified-release).

## Next transport

BLE remains the working link. A direct USB CDC-ACM connection from Tab5's
USB-A host port to ColoPlay is planned, preserving Tab5's USB-C CDC-NCM network
connection. It needs framing, session ownership and duplicate suppression
before it can share controls/state with BLE. No USB transport or PC companion
is implemented by this merge; see the
[USB transport guide](coloplay-usb.md).

USB transport was subsequently implemented after this migration; see the
[wired controller guide](coloplay-usb.md) for the current behavior and validation.
