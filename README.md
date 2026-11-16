# MPD Tab5

A Music Player Daemon controller for the M5Stack Tab5 (ESP32-P4 / C6), based on
[rGnng/iap2tab5](https://github.com/rGnng/iap2tab5) at
`76c99718d4ed5afa7ff3a3cb9f5cfb4db12dcd91`.

It keeps the original LVGL touch interface, Gray/Aqua/Windows 95 themes, album,
artist, song and playlist browsers, Up Next, USB input, Ulanzi Bluetooth knob,
screen/power settings, Wi-Fi configuration, web diagnostics and OTA. Playback
runs on **your MPD server's audio output**. The Tab5 is a remote control.

The optional [ColoPlay navigation extension](docs/coloplay-ble.md#navigation-feedback-and-direct-control-scene)
returns the current view and focused item over BLE, alongside existing playback
and volume feedback. ColoPlay's direct-navigation scene uses single buttons and
hold-to-page controls; existing Tab5 touch, USB and Ulanzi actions remain intact.

Current [controls and context menus](docs/controls.md), [validation results](docs/mpd-validation.md),
and [firmware build/hardware notes](firmware/README.md) are documented separately.

## ColoPlay firmware

This repository also contains the [ColoPlay ESP32-S3 firmware](coloplay/README.md),
imported with its full history from [rGnng/esp32colo](https://github.com/rGnng/esp32colo).
That project began as a proof of concept for rebuilding the LifeSmart/ColoLight
ColoPlay firmware. Development here focuses on ColoPlay as an MPD/MPD Tab5
controller; the original work remains a reference for other ColoPlay projects.

| Directory | Purpose |
| --- | --- |
| `app/`, `firmware/` | Existing Tab5 application and ESP-IDF firmware |
| `coloplay/` | ColoPlay application, hardware findings, tools and tests |
| `shared/` | Common BLE protocol and volume-scale definitions |

The two devices still build separate application images. Tab5's existing build
commands below are unchanged. For ColoPlay, follow its
[build and recovery guide](coloplay/docs/flash.md); run its scripts from `coloplay/`.
ColoPlay supports [USB CDC-ACM controls and feedback](docs/coloplay-usb.md)
through Tab5's USB-A port, with BLE for wireless use.
See the [migration record](docs/coloplay-migration.md) for history and validation.

## First use

1. Boot with an SD card inserted. Existing Wi-Fi credentials can be imported
   from the previous firmware's NVS backup. Otherwise put the SSID and password
   on separate lines in `/sdcard/mpd/wifi.txt`.
2. Open the address shown on the Tab5, or your forwarded
   [web interface](http://127.0.0.1:10082/).
3. Under **MPD Server**, enter a hostname/IP, port (normally `6600`), and optional
   password. Select **Save & connect**. The default hostname is **empty**.
4. The library loads automatically. MPD `idle` notifications refresh database
   and stored-playlist changes. **Resync library** is also available manually.

Wi-Fi distinguishes **Connecting…** from **Requesting IP…**. Association and
DHCP each have a 20-second timeout. A stalled DHCP client restarts once; if
there is still no address after another 20 seconds, Wi-Fi reconnects. Failed
connections retry with a 1–30 second backoff, restarting only the Wi-Fi driver
after every third failure. Recovery does not reboot the Tab5 or power-cycle
the C6/Bluetooth module. Disconnect reason codes and failed API calls appear
in the web debug log; enable **Debug log to SD** to retain logs across reboots.

Playback controls include play/pause, previous/next, seek, volume, mute,
shuffle, repeat all/one, server stars, starting albums/artists/songs/playlists,
and jumping to entries in Up Next. Touch, USB, BLE and web inputs use the same
player functions. Hold previous/next on the Ulanzi to seek; hold the knob and
turn it to change MPD volume.

**Shuffle Play** builds the selected album, artist or playlist queue, shuffles
the whole queue, and enables random playback before starting its first song.
Queue setup and shuffle belong to one request, so the album's first track is
not started before shuffling. **Play All** retains the displayed track order.

**Settings → Screen → Pagination** offers **Off** (default), **On touch 64**,
or **On touch 96**. The numbers set each button’s width and height in pixels.
On touch adds stacked page-up/down buttons at the lower right of every list,
including both panes in split views and Up Next. Each button pages only its own
list. Controls use 65% transparency, follow the existing content margins, and
hide when keyboard/knob navigation resumes or touch is disabled. The setting is
saved in `/sdcard/mpd/config.json` and is also available on the web System page.

**Settings → Volume Scale** offers **1/16 Squared Curve**, **1/64 Squared Curve**
(default), **2% Integer** and **5% Integer**. Squared profiles use control position
`x` with raw MPD target `x²`; readback maps to the nearest lookup position for
consistent control/display. Integer profiles use straight 2% or 5% targets for
Linux/Windows and integer-only MPD servers. Both devices share tiny compile-time
tables, with no runtime square root or logarithm. The selected scale is announced
over BLE on connect and when it changes; ColoPlay offers only Up/Down actions.
Changing scales preserves current playback volume and changes the readout and
subsequent steps. API/config `volume_steps` retains IDs 16/64 for squared profiles
and 2/5 for integer profiles. Confirmed raw targets remain separate from actual
readback; external changes reset the reference.
The [macOS mixer findings](docs/coloplay-ble.md#macos-mixer-findings) explain
observed quantization, the squared profiles and their device-specific limits.
The setting is saved on SD and is also available on the web System page.
Volume changes briefly replace the clock with a centered volume bar. Further
changes update its fill and extend the display for about two seconds. Mute
empties the bar; the clock or active sync icon then returns. The bar shows no
speaker icon or percentage text.

The screen wakes when MPD starts or resumes playback, including after a manual
screen-off. Blanking it while playback is already running stays effective until
a subsequent pause/stop and playback start, or touch/key input.

On Now Playing with the queue closed, **Down** skips to the next track,
**Up** uses Previous/Restart (Tracks restarts after three seconds), and **Enter**
toggles Play/Pause. This includes ColoPlay knob navigation and presses. Up Next
and the context menu retain Up/Down selection and Enter activation.

**Page Up/Down** only page lists, including Up Next when it is already open.
They do nothing on Now Playing with the queue closed. **[** and **]** retain
tab navigation; **[** on Now Playing toggles Up Next. The web **Up Next** button
uses a separate queue action.

Press **C** (including ColoPlay's Context binding), or hold a touch row, to open
its context menu. Albums and artists offer **Play**, **Shuffle Play**,
**Add to Queue**, and **Play Next**. Song rows in Albums, Artists, and Songs
offer **Play**, **Add to Queue**, **Play Next**, **Star/Unstar**, and navigation
to that song's album/artist. These actions use the selected item, including
when another song is playing. Releasing a touch hold does not activate the row.

**Add to Queue** appends without interrupting playback. **Play Next** inserts
after the current track, or at the front without starting playback when there
is no current track. Album/artist additions preserve their displayed song order.
In Up Next, the context menu offers **Play Next** and **Remove from Queue**:
moving an entry does not duplicate it, and removal uses its server queue-entry
ID so duplicate songs are distinguishable. Menus retain the page's selection
and viewport; a page rebuild safely dismisses a menu that belongs to it.
Tracks uses explicit queue positions. Standard MPD random mode uses `prioid`
when advertised, promoting each requested next entry as playback advances.

Tap or drag the Now Playing progress bar to seek, including with Up Next open.
The position previews while dragging and sends one seek on release. Paused
playback stays paused; changing track or leaving the view cancels an unfinished drag.

Stars use Tracks’ server-side **Stared** playlist. The Tab5 reads its membership,
adds songs with `playlistadd`, and removes them with `playlistdelete`. The star
icon and Stared view reflect confirmed server state; MPD mode does not read or
write local star files. Stored-playlist notifications refresh remote changes,
with a 10-second polling fallback for Tracks UI edits that omit notifications.
Stared appears once in Playlists, without a Local label, and retains server order.

Standard MPD 0.21+ and [Tracks](https://github.com/rGnng/tracks) are supported.
The client detects servers without compatible filter/window queries and falls
back to a bounded streaming library import. Server permissions
must allow reading the database/queue/playlists and changing playback. Passwords
are preserved when the web password field is left blank; **Clear saved password**
removes one. Passwords are stored in the SD configuration and omitted from API
responses.

## Album artwork

JPEG embedded/external artwork is cached as 360×360 RGB565 covers under
`/sdcard/mpd/lib/<server-id>/covers/`, with packed list thumbnails alongside the
library catalog. A dedicated artwork connection gives Now Playing and visible
albums priority after scrolling has stopped for 140 ms. Three seconds without
input starts a background pass over the remaining albums. Input pauses the pass, and partial downloads are discarded. Cached
covers survive reboots; missing images are skipped until a library rescan.
Other image formats currently use the UI's fallback artwork.

**Settings → MPD Server** on the Tab5 shows one **Artwork Cache** row with
`cached / total (percentage)`, styled like the other settings rows. The clock
area uses the original rotating sync icon while the library or artwork cache
pass is in progress. While disconnected, the Tab5 shows only
`Connecting to <host>:<port>` in the normal connection-message style. The web
interface retains its detailed progress bar, missing/failed counts, and
paused/active/completed states.

Opening another album retains the left viewport and its loaded covers; only
the detail pane is rebuilt. Playback changes to known tracks do not republish
the catalog or invalidate list artwork.
Covers update in place without rebuilding the album list. MPD keeps the growing
thumbnail pack on SD, using bounded row caches in RAM. **Resync library** retries
skipped albums.
This follows the idle caching approach in
[rGnng/headunit](https://github.com/rGnng/headunit), while retaining the original
Tab5 list's two-cover / 4 ms update budget after scrolling stops. Thumbnail SD
reads run on a separate worker; LVGL only installs completed images. Playback
polls update the mini-player only when visible fields change, and pagination
geometry updates are deferred until row layout has finished.
Text fades draw only the glyphs crossing the fading edge, while retaining the
same appearance over gradients and translucent selections.
The compiled Inter fonts handle supported characters; `JPFONT.TTF` supplies
fallback glyphs instead of routing all Latin text through runtime font rendering.

## ColoPlay Bluetooth controller

A separate ColoPlay peer driver supports MPD volume, playback/seek controls and
returned state over the existing bonded BLE connection. D100H support remains
available; one controller is active at a time. See [pairing, protocol and validation](docs/coloplay-ble.md).
For Star/Unstar, bind a ColoPlay **Bluetooth HID key** action to keypad `*`
(usage **85**, modifiers **0**). This toggles the currently playing song in the
server's Stared playlist. Context menu is **C** (usage **6**, modifiers **0**)
on Now Playing and the album, artist, song, and Up Next lists. Turn the knob
to choose an action and press it to confirm. Context Star/Unstar acts on the
selected song; the `*` shortcut still acts on the playing song. See the
[complete key mappings and menu actions](docs/controls.md).

## Storage

All application files are under **`/sdcard/mpd/`**:

| Path | Contents |
| --- | --- |
| `config.json` | Display, input, sound, Wi-Fi mode and power settings |
| `server.json` | MPD hostname, port and password |
| `wifi.txt` | Wi-Fi SSID and password, one per line |
| `bluetooth.txt` | Known controllers and reconnect preference |
| `uris.bin` | Active MPD URI lookup strings, streamed from SD |
| `lib/<server-id>/` | Per-server catalog, thumbnail pack and cover caches |
| `lib/<server-id>/covers/` | Album-PID 360×360 covers and temporary artwork downloads |
| `lib/<server-id>/np/` | Now Playing RGB565 cover cache |
| `debug.log`, `debug.old.log` | Optional logs |
| `JPFONT.TTF` | Optional extra font (existing root font imported once) |

Temporary files and backups stay in the same directory. Existing iPod files are
left in place. Wi-Fi/BLE radio internals still use their ESP-IDF/NimBLE system
storage; this app imports existing credentials/controller preferences without
rewriting the old application's configuration.

## USB-C network

In **USB-A HighSpeed Only** mode, USB-A remains a high-speed host and USB-C
exposes a **CDC-NCM network adapter** named **MPD Tab5**.

| Setting | Default |
| --- | --- |
| Tab5 USB address | `172.32.30.1` |
| DHCP client address | `172.32.30.2` |
| Netmask | `255.255.255.0` (`/24`) |
| DHCP pool | Exactly one address |
| Advertised gateway / DNS | None |

Connect USB-C with a data cable and open `http://172.32.30.1/` for the same web
settings, diagnostics and OTA interface. The attached computer can also run
MPD; configure its USB address (`172.32.30.2` by default) as the MPD server.
Wi-Fi remains available alongside USB, and the USB link does not route or
bridge Internet traffic.

The web interface's **System** section has **USB-C network IP** and
**USB-C DHCP client IP** fields. Choose distinct host addresses within the
same `/24` subnet, save, and reboot to apply. Settings are stored in
`/sdcard/mpd/config.json`. USB-C remains a host in **USB-A HS & USB-C OTG**
mode; USB networking starts only in **USB-A HighSpeed Only** mode.
`/api/status` reports the active USB addresses, link state, and packet counters
under `usb_network`, separately from Wi-Fi.

## Build and OTA

Use **ESP-IDF 5.5.6**, with the ESP32-P4 tools installed:

```bash
source ~/esp/esp-idf-5.5.6/export.sh
idf.py -C firmware build
python3 tools/ota_mpd.py --url http://127.0.0.1:10082
```

The application image is `firmware/build/mpdtab5.bin`. The OTA helper uploads
only the application and checks the running firmware's ELF SHA-256 after
reboot. The existing Tab5 partition layout and C6 firmware are reused. A full
USB flash and hardware details are covered in the
[firmware guide](firmware/README.md).
No MFi identity or Apple authentication material is needed.

## Development

The control worker owns playback, database and idle-subscription connections;
a separate artwork worker downloads and decodes one cover at a time. UI
callbacks enqueue commands;
metadata updates are applied on the UI thread using the inherited catalog.
Database queries use batches of 256 songs where supported. Tracks uses its
supported `listallinfo` response instead. URI strings are stored on SD instead
of reserving a fixed PSRAM pool. URI-derived IDs stay
stable across imports, and AlbumArtist groups compilation albums. Connections
retry automatically after network/server failures. Server switches use separate
SD cache namespaces.

```bash
python3 tools/gen_web_ui.py  # after editing assets/web/
python3 -m unittest discover -s tests -p 'test_mpd_*.py' -v
python3 -m unittest discover -s tests -p 'test_usb_*.py' -v
python3 -m unittest discover -s tests -p 'test_album_view.py' -v
python3 -m unittest discover -s tests -p 'test_thumb_identity.py' -v
python3 -m unittest discover -s tests -p 'test_web_input.py' -v
python3 -m unittest discover -s tests -p 'test_wifi_recovery.py' -v
cmake -S . -B build -G Ninja -DIAP2TAB5_BUILD_UI_TESTS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/tab5_sim --headless --screenshot /tmp/mpd-preview.png
```

The simulator uses the inherited demo library; firmware always uses MPD.
The two queue-action icons are fully opaque NRK Core Icons. To regenerate
their 32 px alpha assets, install CairoSVG 2.8.2 and Pillow 12.0.0, then run
`python3 tools/gen_context_icons.py`. Sources, modifications and attribution
are listed in [assets/icons/README.md](assets/icons/README.md).

Browser tests need `playwright` and its Chromium installation; they are skipped
when Playwright is unavailable. Existing source assets and their notices are
retained. MPD framing and command semantics follow the
[official protocol documentation](https://mpd.readthedocs.io/en/stable/protocol.html).

Queue refreshes allocate for the reported queue size. If memory is temporarily
unavailable, the previous queue remains visible and the refresh retries without
disconnecting MPD. Protocol errors in the device log include the command verb
and receive errors include the socket error number.
If Tracks accepts TCP connections but closes them before the MPD greeting,
check its 32-client connection limit. Fully quit and reopen Tracks to reset
the count; toggling its MPD service can leave old connection slots occupied.

Current bounds: 24,000 library/queue entries, 4,096 albums, 2,048 album artists,
32 stored playlists with up to 1,024 tracks each. Oversized network responses
fail with a status message. Date Added uses MPD's `added` timestamp, falls
back to `Last-Modified`, then to stable local discovery order. Album and song
Name/Year/Date Added sorts support both directions. Playback follows each
view's displayed order, and queue jumps use stable MPD entry IDs. Search and
general playlist editing are not implemented; starring and unstarring through
Stared are supported.

Upstream base: `rGnng/iap2tab5@76c99718d4ed5afa7ff3a3cb9f5cfb4db12dcd91`.

For a read-only hardware view snapshot, use
`/api/library?view=albums&offset=0&limit=128`. Other views are `artists`, `songs`,
`playlists`, `queue`, `album_tracks`, `artist_tracks`, and `playlist_tracks`;
track detail requests accept a hexadecimal `parent` ID or playlist `index`.
Run `python3 tools/audit_mpd_views.py --screenshots` to check live sort controls,
view metadata and detail membership. It restores sort settings and writes its
report/screenshots under `/tmp/mpd-view-audit/`. It issues no playback commands.
