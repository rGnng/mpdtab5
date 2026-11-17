# MPD controller validation

## Fast Scroll sleep-activity correction — 2026-11-17

ColoPlay reports sleep activity alongside input, throttled to one message per
250 ms during a continuous gesture. Tab5 previously treated that activity as
a separate key action and cleared Fast Scroll history, so acceleration could
fail to enter or repeatedly drop out in Up Next and other lists. A production
LVGL reproduction with activity interleaved between detents failed in all
three themes before the correction.

The activity key now only refreshes idle time while awake. An actual wake still
clears velocity and consumes the wake input; normal actions, reversals and
idle expiry retain their reset behavior. No threshold, timing or stored setting
was changed. USB serial and BLE already deliver the same activity key, so no
ColoPlay firmware or protocol change is needed.

All **24 native UI CTests**, **22 Fast Scroll policy/config tests** and **8 idle
policy tests** pass. Up Next coverage includes interleaved activity, entry and
sustained acceleration across pending/recycled rows, its cursor color, idle
expiry, precise movement afterward and a real wake. Albums, Artists, Songs,
Playlists and Settings also retain acceleration under interleaved activity.
The config fixture now extracts only Fast Scroll assignments instead of
unrelated General settings with out-of-scope validation variables.

The Tab5 ESP-IDF build passes. OTA application **2,935,984 bytes**, ELF
SHA-256 `ded18373c1922bad4328d6a702abd6bec9859f60ba4a71e2fa9dd915a2e70e6c`, was boot-verified.
Live checks confirmed MPD, SD, USB-C CDC-NCM networking, ColoPlay USB control
transport and shared sleep feedback. The saved Fast Scroll settings remained
**enabled, one-page threshold, 800 ms trigger and 500 ms hold**; all Tab5 settings
and ColoPlay scenes/Global bindings matched the pre-OTA snapshots. ColoPlay
remains on **0.8.3-sleep-key**. The user tested continuous knob turns in Up Next
and a library list and confirmed: **“Works consistently now.”** Exact images,
previous firmware, the failing
reproduction and passing evidence are retained privately in
`coloplay/backups/fast-scroll-release/`.

## Shuffle All and demand queue loading OTA — 2026-11-17

The previous Shuffle All path copied every local song ID into an asynchronous
request, then allocated a second full queue snapshot. For the **20,916-song**
library, the request alone required **167,328 bytes**. Submission failures were
ignored by the UI, allowing a silent no-op under allocation pressure. The new
fixed-size request makes the server execute this MPD command list:

```text
clear
random 0
single 0
add "/"
shuffle
random 1
play 0
```

Standard MPD's [root-add compatibility](https://github.com/MusicPlayerDaemon/MPD/blob/master/src/command/QueueCommands.cxx)
and the [Tracks implementation](https://github.com/rGnng/tracks/blob/628ee5b/Sources/Audia/MPDServer.swift)
support this path; Tracks needs no new endpoint. The UI now reports accepted
or unavailable submissions. Successful replacement invalidates old occurrence
handles, including raw MPD ID zero with a retired epoch.

The old queue refresh downloaded full metadata for every entry and performed
SD URI-cache reads even when it only needed a membership check. Early live
runs of the server-side shuffle fix still took **28.67 / 56.41 seconds** to
publish the full snapshot. Queue count/current song now come from status, with
no eager metadata request or temporary full-size staging allocation. Only row
access requests `playlistinfo start:end`, in pages of **64**. RAM URI membership
avoids per-song SD reads. A post-fetch status check discards stale queue
versions/counts, and connection changes cannot publish an old page. Unsupported
ranges do not fall back to a full queue download. Up Next uses 15 recycled row
widgets over the entire remaining queue, removing the 100-item cap and its
per-row widget growth. The scrollbar spans the full
remaining count; knob bursts, page keys and touch scrolling select logical
positions without waiting for metadata. Cached page arrivals preserve selection
and context menus retain exact occurrence IDs. The 24,000-entry bound remains;
library catalog sync is still separate. Standard MPD Play Next priority IDs are
revalidated after structural changes without scanning or loading uncached rows.

Host checks passed: **82** production MPD/socket tests, **2** full-library PID lookup
tests, **2** playback boundary tests, the request ownership fixture, all
**24** native UI tests (including Shuffle All Enter and the 20,916-entry virtual
Up Next fixture in all three themes), and the Tab5 ESP-IDF build. Coverage includes a 20,916-entry count with allocation
disabled and no wire request, current-row seeding, bounded/cache-hit/far/last
pages, stale revisions/counts, range errors/ignored ranges, pending row
navigation, bounded widget reuse, final-occurrence jumps, modal focus isolation, empty libraries, server errors and retired queue handles.

Tab5 accepted OTA and booted application **2,935,856 bytes**,
ELF SHA-256 `f20e0a5ae778f79e755fdffc3c4bb33c4552cf07067270577f67341c95762941`. ColoPlay stayed on
**0.8.3-sleep-key**, with its image and all saved scenes/Global bindings unchanged.
The completed catalog sync was observed at uptime **29 seconds** with
**20,916 queue entries, one cached current row and zero pending pages**. No
queue read was issued before that observation. Playback metadata was already
available during the separate catalog sync. Near and distant API page reads
loaded independently; repeated reads did not increase cache size. Actual Up
Next acquired a selectable row. Two consecutive selections of the Albums
Shuffle All row then started random playback with different queue heads in
**6.14 / 5.93 seconds**, with **64 cached queue rows** each.

A further live check reached logical row **171 / 20,914** through Down/Page Down,
with only **192 cached queue rows**, then scrolled the touch viewport farther.
A screenshot confirms the populated viewport and full-length counter. Across
**41 device row-refresh samples**, median work was **964 us**, maximum
**2,311 us**. These measure row updates, not complete display refresh or physical
USB/BLE input latency. The remaining count excludes the current/already-played
entries. Host tests additionally reach the final occurrence of the full queue
with a constant 15-widget pool. The final image recheck reached
**131 / 20,915** with **192 cached queue rows** and confirmed MPD and saved
settings again before returning to Now Playing.

Now Playing was restored; the full-library queue was intentionally left
playing. Volume, repeat, playlists, saved Tab5 settings and ColoPlay scenes and
Global bindings matched the snapshots. MPD, SD, USB-C CDC-NCM networking,
ColoPlay USB control transport and shared sleep feedback remained healthy.
Exact images, previous known-good firmware, settings and test snapshots are in
the ignored `coloplay/backups/shuffle-all-release/` bundle.

## Sleep key OTA — 2026-11-16

ColoPlay **0.8.3-sleep-key** was OTA-verified in `ota_app1`, MD5
`90a6cc61316fe838898552ded8723d76` (application **3,267,424 bytes**).
Tab5 stayed on ELF SHA-256
`a3e0946d7af9ebe14ad4ff68ab5b1869bee6d809e27f250ba18c0b1f2bfebe48`.
The matching ColoPlay recovery image was prepared before saving the new key
value. Button 1 Hold was added to the active Tab5 Navigation scene at **600 ms**,
repeat off, Sleep key value **386**, no modifiers or sound, inherited light and
unchanged action screen. Every pre-existing binding, Global binding, appearance
setting and active scene was preserved. The new action survived a reboot.

The saved-binding API invoked Hold and both screens plus ColoPlay LEDs turned
off and remained off. Waking preserved the selected playlist detail; a following
Button 1 short press still went Back. Now Playing was restored awake, with
playback, volume, modes, playlists and other saved settings unchanged.
Physical 600 ms recognition, no repeat/release Back and continued-hold wake
suppression are covered by the production recognizer/follower fixtures; the
live action was invoked through its saved-binding API.

The full ColoPlay regression suite, production USB router and BLE send-adapter
fixtures pass. Browser fixtures at 375/1280 pixels cover Sleep search/selection,
modifier removal and non-repeating 600 ms configuration without writes. The
production build passes. Exact images, source patch, previous firmware,
settings and test evidence are retained privately in
`coloplay/backups/sleep-key-release/`.

## Shared sleep and wake OTA — 2026-11-16

ColoPlay **0.8.2-sleep-sync** and Tab5 were application-only OTA-updated. Running
identities match ColoPlay MD5 `a3befdb762e0e7378c1aa65c3d374e7e` and Tab5 ELF SHA-256
`a3e0946d7af9ebe14ad4ff68ab5b1869bee6d809e27f250ba18c0b1f2bfebe48`.
Tab5 owns connected screen/LED sleep, manual wake and its 0/30/60-minute timer.
The stored local ColoPlay fallback and all saved scene/Global/appearance/network
settings matched the snapshots. No NVS or on-disk configuration format changed.

Host validation passes the full ColoPlay regression suite, all 21 native UI
CTests, eight screen-idle policy tests, five controller transport/ownership/I/O
fixtures and 34 input-dispatch tests. New coverage exercises negotiated sleep
frames, legacy peers, malformed fields, stale sessions/feedback, rollover,
bounded activity, held wake suppression, UI-thread sleep/wake dispatch and
asleep media/volume suppression. Browser fixtures at 375/1280 pixels verify
linked timer/status, read-only controls, fallback/draft restoration and no
writes or JavaScript errors. Both firmware builds pass.

The live USB CDC-ACM test passed Tab5-to-ColoPlay and ColoPlay-to-Tab5 sleep/wake,
including zero screen/ring/button output while asleep. A first remote knob
command woke both devices without moving the playlist focus; a second command
navigated normally. Now Playing was restored. Ten samples showed matching timer
settings and a decreasing synchronized countdown, a stable USB session, fresh
secure BLE standby and healthy MPD, SD and USB-C CDC-NCM networking. Playback,
volume, modes, playlists and saved settings were unchanged by these checks.

The USB cable remained connected, so a physical BLE-only sleep/wake check was
not performed. Production BLE host fixtures pass; secure BLE feedback was
observed in standby during the USB check. The private evidence bundle is retained under
`coloplay/backups/sleep-sync-release/`.

## Remove from Playlist OTA — 2026-11-16

Regular playlist-detail song menus now include Remove from Playlist, while
Stared retains Unstar. Native UI checks cover keyboard and touch menus, a
selected duplicate, stale selections, playback/queue preservation, full names
and removal of the final entry. Production socket tests verify quoted full
names, zero-based occurrence positions, visible-order revalidation, list-size
changes, the 1024-entry display limit, stale connections and server errors.
Request tests cover copied snapshots, bounds, offline/full queues and allocation
failures. The worker rereads the stored list before `playlistdelete`; MPD offers
no atomic compare-and-delete, so another client can still edit between those
two commands. The existing on-disk cache format and NVS settings are unchanged.

The 73-case MPD client suite, request ownership fixture and all 21 native UI
CTests pass. Tab5 accepted OTA and rebooted into the **2,930,784-byte**
application with ELF SHA-256
`6fff2a4c16c35b082ea0de3496683c673b92e3ce56eb27a191a71ecb73385e7e`.
ColoPlay stayed on **0.8.1-navigation-details**, MD5
`16786370425fc7188f3416d290527a2f`. Saved Tab5 settings, ColoPlay scenes, Global
bindings and active scene matched their pre-OTA snapshots.

The user tested Remove from Playlist after reboot and confirmed removing the
sole song from playlist **1**. The playlist remained present with zero entries.
This concurrent physical test explains the initial automated before/after
playlist-count mismatch; that check stopped before sending input. A subsequent
automated check confirmed Stared exposes Unstar without Remove from Playlist,
cancelled its context menu and restored Now Playing. It made no playlist
writes, and playlist contents, playback, volume and modes matched its own
post-user-test baseline.

Ten stability samples kept the USB controller session healthy, with fresh
secure BLE standby, MPD, SD and USB-C CDC-NCM networking online, and no new
dropped, invalid or stale frames or lease timeouts. Exact images, previous
known-good Tab5 firmware, snapshots and test evidence are retained privately in
the ignored `coloplay/backups/remove-from-playlist-release/` bundle.

## Add to Playlist OTA — 2026-11-16

Tab5 adds a song-context destination picker, including song
rows inside playlist details. The native UI fixture covers append/cancel, Back
to the parent menu, preserved playback/queue/selection, track changes while the
picker is open, deleted destinations, empty/full 32-playlist lists, cleanup and
touch holds. Production MPD socket fixtures cover quoting, missing/reserved
names, permission errors, full destination names and connection-generation
rejection. Request ownership tests cover caller-buffer changes, allocation
failures, offline/full queues and cleanup. These checks do not modify a live
server. The firmware builds and all 21 native UI CTests pass; the 67-case MPD
client suite and request/playback/queue fixtures pass.

Tab5 accepted OTA and rebooted into ELF SHA-256
`7c6a1e56968040e6eecfb554c3c723a059586c35fa3aa5fa50fb29204f650a29`
(application 2,929,200 bytes). ColoPlay stayed on **0.8.1-navigation-details**,
MD5 `16786370425fc7188f3416d290527a2f`. Saved Tab5 settings, ColoPlay scenes,
Global bindings and active scene matched the pre-OTA snapshots.

The live USB test opened Add to Playlist from Now Playing, checked the regular
destination and Back entries, confirmed Stared was excluded, returned to the
parent menu and closed it. Playlists, playback, volume, shuffle and repeat were
unchanged. No live playlist append was performed; writes and failure paths were
tested against the production MPD socket fixture and native UI mock. MPD, SD,
USB-C CDC-NCM networking and the ColoPlay USB session remained healthy over ten
post-test samples, with fresh secure BLE standby and no new drops, invalid or
stale frames, or lease timeouts. Private snapshots and exact images are retained
in the ignored `coloplay/backups/add-to-playlist-release/` bundle.

The paired hashes below are historical release records.

## ColoPlay Global and star-feedback OTA — 2026-11-16

The 0.8.0 paired release uses source **`5aeb1c3`**, ColoPlay
**0.8.0-global-bindings** in `ota_app0`, and Tab5 ELF SHA-256
`102fbf7bb669c06dca09d461f56eeb56d3cd51cad252a4cb984bdaf4380ffa3d`.
Both application identities were verified after OTA. MPD, SD and USB-C CDC-NCM
remained online while USB-A carried ColoPlay CDC-ACM controls.

The schema-9 migration retained the active Navigation scene and all 14 saved
bindings. Global became a protected shared-binding scope; it could not be
activated, targeted or reached by Next/Previous scene cycling. Other saved
settings matched the pre-OTA snapshots.

The existing Button 4 Hold binding starred/unstarred the current song through
the saved-binding Run path. Both USB and Bluetooth standby feedback matched
Tab5's confirmed state; the initial unstarred state was restored. BLE sent no
duplicate commands while USB owned input. View switching returned to Now
Playing. Device-served web pages passed responsive layout/connection checks;
18 samples over 43 seconds showed no resets, invalid/stale frames, drops or
lease timeouts. Physical gesture timing and wireless-only command routing were
not retested for this release.

Exact image hashes, recovery compatibility and evidence scope are maintained
in the [ColoPlay release record](../coloplay/docs/development.md#current-verified-release).
Earlier dated checks below retain their original image identities.

## ColoPlay navigation OTA verification — 2026-11-16

Application-only OTA verified Tab5 ELF SHA-256
`dce72107fb57e5c062ce7cb493f7961f0a88e533c7ee3d48b1e5430bcc8ab0f8` with the ColoPlay navigation extension. WiFi, SD and MPD
are healthy, and the saved system/MPD configuration matches its pre-update
snapshot. No C6 firmware, bootloader or partition table was flashed.

The matching ColoPlay **0.6.33-navigation-nvs** has the new direct-navigation
scene active. Saved bindings were exercised through BLE: previous/next view,
knob focus, both page directions, context menu movement and Back, and the Queue
shortcut. Labels and positions were checked in the returned navigation stream.
An isolated navigation test retained playback state and raw volume. ColoPlay's
scene persistence fix was verified separately by a reboot that restored all
settings and fresh BLE. Its retired USB experiment was removed.

Device-served ColoPlay browser checks at 375/1280 pixels passed without writes
or JavaScript errors. Exact paired-image fingerprints, configuration snapshots
and round-trip evidence are in the ColoPlay workspace's ignored
`backups/navigation/ota/` directory. Tab5 source is based on `d854616` plus the
navigation working-tree changes.

## ColoPlay focus feedback source validation — 2026-11-16

The optional navigation stream is a read-only UI snapshot copied every 60 ms,
then transmitted by the existing ColoPlay BLE worker. No MPD commands or
existing USB/D100H input handlers are changed. The matching ColoPlay application
adds the direct-control scene and hold-to-repeat paging.

- The production GATT fixture passes legacy negotiation, navigation capability,
  fragmented UTF-8 labels, revisions/positions, updates and unchanged-focus
  heartbeats alongside existing media/volume assertions. A changed focus wakes
  the BLE event immediately and drains through write acknowledgements with no
  housekeeping-clock advance; identical snapshots do not enqueue extra events.
- All 21 native UI CTests pass, including focus reads for virtual lists,
  album-detail track names rather than row numbers, context commands, playlist
  and Settings labels, UTF-8 truncation, and no selection changes from reads.
- ESP-IDF 5.5.6 builds the Tab5 application. ColoPlay production and optional USB
  experiment builds pass, with its full host suite and 320/375/1280 browser checks.
- The broad Tab5 Python run has 8 failures and 5 setup errors; the identical 13
  failing cases reproduce on untouched `d854616` in a separate checkout. They
  concern existing Wi-Fi credential fixtures and source-extraction harnesses;
  this work leaves those unrelated sources unchanged.

These are build/host results, not a new hardware OTA verification. Current
controls and the preceding deployed image are recorded below.

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


## ColoPlay USB CDC-ACM verification — 2026-11-16

The Tab5 application was OTA'd and verified by ELF SHA-256
`48d23f77130fef8b47d745e0e9fc67c2233fa02e38105eb04e837d370ba79de2`, paired with ColoPlay 0.7.2-usb-lease.
USB-A carries CDC-ACM controls and feedback; USB-C CDC-NCM remains connected to
the computer/MPD server. Saved Tab5 configuration matches the pre-OTA snapshot.
MPD and SD remain available. Live USB navigation, paging, context/Back and
volume up/down checks pass with no duplicate BLE commands or framing drops.
The user confirmed USB and wireless physical controls, and a subsequent USB
session reconnect is observed. Protocol, ownership, state and build/UI checks
are detailed in the [USB guide](coloplay-usb.md) and
[ColoPlay release record](../coloplay/docs/development.md#current-verified-release).
