# MPD controller validation

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

Final verified ELF SHA-256:
`1932e788c87dbca4b2efaa9cb405ec8e3b1be7a9ceea2fa9c4fb5d05cb144d51`.
