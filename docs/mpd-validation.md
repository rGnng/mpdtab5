# MPD firmware validation

Validated on 2026-11-13 with ESP-IDF 5.5.6 and the existing Tab5/C6 hardware.

- ESP32-P4 firmware built successfully; application fits the existing 3.5 MiB
  OTA slots with approximately 25% headroom.
- 23 native MPD tests passed: socket framing/fragmentation, UTF-8, binary
  transfer, ACK recovery, timeouts/disconnection, response limits, quoting,
  URI identity, album grouping, metadata fallback, paginated imports,
  queue replacement/jump, stored playlist streams, shuffle/repeat and mute.
- 5 Chromium setup-form tests passed: saved address/port, preserving/replacing/
  clearing passwords, empty hostname, and port validation.
- All 18 inherited headless LVGL UI integration tests passed.
- OTA upload completed and the running application's ELF SHA-256 matched the
  local build. Wi-Fi, SD, controller preferences and server settings persisted
  across reboot. The initial MPD hostname was empty.
- A configured MPD server connected and supplied live playback metadata,
  library and artwork. No playback interruption commands were injected as
  part of live validation.
- Two simultaneous Chromium tabs loaded the actual embedded web interface
  with no failed requests or JavaScript errors while MPD remained connected.
- The existing optional Japanese font was imported into `/sdcard/mpd/` and
  loaded successfully. A hardware framebuffer capture was inspected.

Final verified ELF SHA-256:
`7c629e52f35cef7cd63b7c2d36f7dc4e7960f0203028c3040db76fae28202836`.

Real Bluetooth reconnect depends on the physical controller being awake;
retention of its paired entry was verified. Transport/control behavior has
native coverage; live testing did not modify the user's MPD queue or volume.
