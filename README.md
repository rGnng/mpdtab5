# MPD Tab5

A Music Player Daemon controller for the M5Stack Tab5 (ESP32-P4 / C6), based on
[rGnng/iap2tab5](https://github.com/rGnng/iap2tab5) at
`76c99718d4ed5afa7ff3a3cb9f5cfb4db12dcd91`.

It keeps the original LVGL touch interface, Gray/Aqua/Windows 95 themes, album,
artist, song and playlist browsers, Up Next, USB input, Ulanzi Bluetooth knob,
screen/power settings, Wi-Fi configuration, web diagnostics and OTA. Playback
runs on **your MPD server's audio output**. The Tab5 is a remote control.

## First use

1. Boot with an SD card inserted. Existing Wi-Fi credentials can be imported
   from the previous firmware's NVS backup. Otherwise put the SSID and password
   on separate lines in `sdcard://mpd/wifi.txt`.
2. Open the address shown on the Tab5, or your forwarded
   [web interface](http://127.0.0.1:10082/).
3. Under **MPD Server**, enter a hostname/IP, port (normally `6600`), and optional
   password. Select **Save & connect**. The default hostname is **empty**.
4. The library loads automatically. MPD `idle` notifications refresh database
   and stored-playlist changes. **Resync library** is also available manually.

Playback controls include play/pause, previous/next, seek, volume, mute,
shuffle, repeat all/one, local stars, starting albums/artists/songs/playlists,
and jumping to entries in Up Next. Touch, USB, BLE and web inputs use the same
player functions. Hold previous/next on the Ulanzi to seek; hold the knob and
turn it to change MPD volume.

Standard MPD 0.21+ and [Tracks](https://github.com/rGnng/tracks) are supported.
The client detects servers without compatible filter/window queries and falls
back to a bounded streaming library import. Server permissions
must allow reading the database/queue/playlists and changing playback. Passwords
are preserved when the web password field is left blank; **Clear saved password**
removes one. Passwords are stored in the SD configuration and omitted from API
responses.

## Storage

All application files are below **`sdcard://mpd/`** (`/sdcard/mpd` in firmware):

| Path | Contents |
| --- | --- |
| `config.json` | Display, input, sound, Wi-Fi mode and power settings |
| `server.json` | MPD hostname, port and password |
| `wifi.txt` | Wi-Fi SSID and password, one per line |
| `bluetooth.txt` | Known controllers and reconnect preference |
| `uris.bin` | Active MPD URI lookup strings, streamed from SD |
| `lib/<server-id>/` | Per-server catalog, stars, thumbnail pack and cover caches |
| `lib/<server-id>/np/` | Now Playing RGB565 cover cache |
| `debug.log`, `debug.old.log` | Optional logs |
| `JPFONT.TTF` | Optional extra font (existing root font imported once) |

Temporary files and backups stay in the same directory. Existing iPod files are
left in place. Wi-Fi/BLE radio internals still use their ESP-IDF/NimBLE system
storage; this app imports existing credentials/controller preferences without
rewriting the old application's configuration.

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
USB flash and hardware details are covered in the inherited
[firmware guide](firmware/README.md); its Apple playback setup is not used here.
No MFi identity or Apple authentication material is needed.

## Development

The background MPD worker owns all network I/O. UI callbacks enqueue commands;
metadata updates are applied on the UI thread using the inherited catalog.
Database queries use batches of 256 songs where supported. Tracks uses its
supported `listallinfo` response instead. URI strings are stored on SD instead
of reserving a fixed PSRAM pool. URI-derived IDs stay
stable across imports, and AlbumArtist groups compilation albums. Connections
retry automatically after network/server failures. Server switches use separate
SD cache namespaces.

```bash
python3 -m unittest discover -s tests -p 'test_mpd_*.py' -v
cmake -S . -B build -G Ninja -DIAP2TAB5_BUILD_UI_TESTS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/tab5_sim --headless --screenshot /tmp/mpd-preview.png
```

The simulator uses the inherited demo library; firmware always uses MPD.
Browser tests need `playwright` and its Chromium installation; they are skipped
when Playwright is unavailable. Existing source assets and their notices are
retained. MPD framing and command semantics follow the
[official protocol documentation](https://mpd.readthedocs.io/en/stable/protocol.html).

Current bounds: 24,000 library/queue entries, 4,096 albums, 2,048 album artists,
32 stored playlists with up to 1,024 tracks each. Oversized
network responses fail with a status message. JPEG embedded/external artwork is
retrieved on track changes and cached as 360×360 RGB565; other formats currently
use the UI's fallback artwork. Date Added uses MPD's `added` timestamp, falls
back to `Last-Modified`, then to stable local discovery order. Album and song
Name/Year/Date Added sorts support both directions. Playback follows each
view's displayed order, and queue jumps use stable MPD entry IDs. Search and
playlist editing are not implemented.

Upstream base: `rGnng/iap2tab5@76c99718d4ed5afa7ff3a3cb9f5cfb4db12dcd91`.

For a read-only hardware view snapshot, use
`/api/library?view=albums&offset=0&limit=128`. Other views are `artists`, `songs`,
`playlists`, `queue`, `album_tracks`, `artist_tracks`, and `playlist_tracks`;
track detail requests accept a hexadecimal `parent` ID or playlist `index`.
Run `python3 tools/audit_mpd_views.py --screenshots` to check live sort controls,
view metadata and detail membership. It restores sort settings and writes its
report/screenshots under `/tmp/mpd-view-audit/`. It issues no playback commands.
