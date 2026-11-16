# MPD controller validation

## Context menus and queue actions — 2026-11-16

Implementation: `b7709fb`, following the Page Up/Down correction in `fb0a595`.
The current [controls reference](controls.md) documents the user-facing behavior.

- **117 MPD tests passed**, including 60 production client/socket-peer tests.
  New queue checks cover append without replacing playback, explicit-position
  Tracks shuffle insertion, empty queues, ordered `prioid` promotion on standard
  MPD, duplicate occurrences, raw queue ID zero, stale connection epochs,
  moving an entry from before the current track, an entry becoming current
  before its move, removed priority heads, and 205-track batched appends.
- **21 headless LVGL tests passed** across Gray, Aqua, and Windows 95. Context
  checks exercise album/artist lists, their song rows, Songs, and Up Next;
  captured targets through playback changes; starring a non-playing song;
  album/artist append order; moving/removing one queue occurrence; and real
  pointer holds that do not trigger the ordinary tap on release.
- Image checks verify the requested NRK assets and theme/focus recoloring.
  Add to Queue uses `nrk:list-arrow-to-bottom-expressive`; Play Next uses
  `nrk:list-arrow-to-top-expressive`. Both have fully opaque list strokes.
- ESP-IDF 5.5.6 firmware build passed. Application-only OTA verified the running
  ELF hash below, with Wi-Fi, SD and the configured Tracks server available.
- A live Tracks Play Next insertion and Remove from Queue round trip restored
  the original 18-entry queue. The original paused track was restored. Standard
  MPD priority promotion is socket-peer coverage, not a live generic-MPD test.

Verified application ELF SHA-256 for this version:
`6f1f8c83251be0efff4e6a7c98af62bff66c669e613f3e31673c2c4e756c71aa`.

Re-run protocol tests with `python3 -m unittest discover -s tests -p 'test_mpd_*.py'`.
Browser tests require Playwright/Chromium. Build the native tests with
`-DIAP2TAB5_BUILD_UI_TESTS=ON`, then run `ctest --test-dir build --output-on-failure`.
Icon-only follow-up changes were checked with all 21 native tests and a firmware
build/OTA; protocol code was unchanged.

The 24 Wi-Fi recovery tests separately cover association/DHCP timeouts, missing
events, failed calls, bounded retries, and Wi-Fi-only driver recovery. Run
`python3 -m unittest discover -s tests -p 'test_wifi_recovery.py'` to repeat them.

## Earlier controller audit — 2026-11-13

The remaining sections record the earlier audit. Its library counts, image hash,
local-star checks, and Bluetooth availability describe that audit date. Current
MPD stars use the server's Stared playlist, as documented in the main README.

Validated on 2026-11-13 against the Tab5 and Tracks' MPD server, with ESP-IDF
5.5.6. Reference server implementation:
[rGnng/tracks@8d3636b](https://github.com/rGnng/tracks/blob/8d3636bf34643c2cebe299cb9fbbc53513a75fa4/Sources/Audia/MPDServer.swift).

## Fixed issues

- Tracks does not implement the client's original filter/window query. Detect
  the empty filtered response and stream its complete `listallinfo` response.
  The device now imports **20,866 tracks, 2,129 albums, and 1,090 album artists**.
- Tracks reports current wall time as `db_update`. Use a dedicated nonblocking
  `idle` subscription for database/playlist changes, avoiding periodic false
  reimports that disrupted browsing.
- Preserve MPD `added` dates, falling back to `Last-Modified` and stable local
  discovery order. Keep album and song sorting independent and deterministic
  in both directions, including equal dates/years.
- Use each view's displayed ordering for playback. Remove the inherited
  512-track selection limit; order album tracks correctly without Disc tags.
- Keep artist view offsets alphabetical without changing artist identity.
  Filter empty artists and retain selected artist/playlist through updates.
- Capture MPD queue entry IDs before enqueueing jumps, so duplicate tracks and
  server queue changes do not turn a queued action into a different position.
- Tolerate multiline metadata and blank response lines emitted by Tracks.
  Paused and empty playback no longer tear down the protocol connection.
- Clip metadata on complete UTF-8 code points so long Japanese/emoji tags remain
  valid in LVGL and JSON. Move URI strings to `/sdcard/mpd/uris.bin` rather
  than reserving a fixed 3 MiB PSRAM pool.
- Restore local stars after catalog rebuilds. Populate Now Playing's catalog
  artist relationship and open the correct detail for Go to Album/Artist.

## Automated checks

**131 tests passed:**

- 66 MPD protocol, model, firmware snapshot, artwork scheduler/transfer, and
  Chromium setup/progress tests.
- 21 headless LVGL integration tests, including every MPD view/action in Gray,
  Aqua and Windows 95, context navigation, and empty/stopped Now Playing.
- 18 live catalog persistence/reconciliation tests.
- 6 indexed library membership/performance tests.
- 13 album scroll/idle/batch-budget tests and 7 persistent thumbnail identity/RAM-policy tests.

Playback-list checks cover sorted Songs, album/artist Play and Play From Here,
700-track selections, duplicate playlist occurrences, local stars, queue jumps,
seek, play/pause, and shuffle/repeat/mute command semantics.

## Hardware checks

- OTA completed; running ELF SHA-256 matched the local application. Wi-Fi, SD,
  configured server, controller preferences, font and sort settings persisted.
- Read live metadata from Albums, Artists, Songs, Playlists and Up Next.
- Verified both directions of Date Added/Name/Year album sorts using first/last
  128-row samples of the full library. Verified Songs Name/Year on equivalent
  samples; per-track date order also has exact model coverage.
- Checked sample album/artist detail membership and track ordering, and every
  stored playlist's count. Captured and inspected the six main screens.
- Paused the real server and observed a healthy connection with no malformed
  response errors, then restored the original playing state. Empty/stopped
  queues were checked in protocol and real LVGL tests without clearing the
  user's live queue.
- Two simultaneous Chromium tabs loaded the embedded web interface without
  JavaScript errors or failed requests. Original sort preferences were restored.
- Idle artwork caching fetched previously unplayed albums from Tracks. More
  than 500 cached album covers survived OTA/reboot, and the visible Albums
  list rendered their thumbnails after restart.
- Inspected Settings → MPD Server and the web page with live artwork counts.
  The counter continued advancing while the catalog revision stayed unchanged;
  cover arrivals no longer rebuild or reorder the page.
- Kept the growing MPD thumbnail pack on SD instead of preloading it into RAM.
  The final warm boot retained over 5 MiB free PSRAM after library import, and
  full device screenshots succeeded alongside background caching.

Run `python3 tools/audit_mpd_views.py --screenshots` to repeat the read-only view
checks. The bounded `/api/library` endpoint exposes model snapshots for the same
views. Reports/screenshots stay under `/tmp/mpd-view-audit/` by default.

Physical Bluetooth input still depends on the controller being awake; its
paired entry and rendered settings were checked, but a physical BLE link was
not available during this audit.

Artwork now has a separate MPD worker and persistent album-PID caches under
`/sdcard/mpd/lib/<server-id>/covers/`. Visible albums load after the 140 ms
scroll-stop delay, then a whole-library pass starts after three seconds without
input. New thumbnails update placeholders without publishing catalog changes
or rebuilding the page. The original two-cover/4 ms list loading budget remains
in place. `tests/test_mpd_artwork.py` checks visible priority, idle delays,
warm-cache reuse, retry/absence behavior, chunk cancellation, and stale server
and track identities against production code and a socket peer. `/api/status`
reports `artwork_cached` and pass progress in `artwork`; album snapshots include
`cover_cached`. The Tab5's Settings → MPD Server page shows a single normal
Artwork Cache count row; the existing clock-area sync icon stays active during
library/artwork passes. The waiting screen has one `Connecting to <host>:<port>`
message in the existing status font/color. The web interface retains detailed
cache progress and paused/active/completed states. All six MPD view/notification
checks passed across the three themes after this settings simplification.

Verified ELF SHA-256 for the 2026-11-13 audit:
`1932e788c87dbca4b2efaa9cb405ec8e3b1be7a9ceea2fa9c4fb5d05cb144d51`.
