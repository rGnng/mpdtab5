# MPD Tab5 controls and context menus

These are the current MPD firmware controls. The simulator uses the same UI
with a mock library. Historical iAP2 design notes can describe older bindings.

## Navigation

| Input | Now Playing, queue closed | Lists and open Up Next | Context menu |
| --- | --- | --- | --- |
| Up / Down | Previous/Restart / Next track | Move selection | Move menu selection |
| Enter | Play/Pause | Activate selected item | Run selected action |
| Back / Escape | Stay on Now Playing | Close Up Next or return from detail | Close menu |
| Page Up / Page Down | No list action | Page the list; do not switch tabs | No action |
| `[` | Toggle Up Next | Previous tab; at Now Playing, toggle Up Next | No action |
| `]` | Next tab | Next tab | No action |
| `C` | Open Now Playing menu | Open menu for selected album, artist, song, or queue entry | Close menu |
| `1`–`5` | Jump to Now Playing, Albums, Artists, Songs, Playlists | Same | No action |
| `6` | Back | Back | No action |
| `7` | Play/Pause | Play/Pause | Play/Pause |

On Now Playing, Previous/Restart follows the server's behavior. Tracks restarts
the current track after three seconds; near its start, it selects the previous
track. Inside Up Next, arrows select queue rows and Enter plays the selected
occurrence. Page keys page Up Next without opening or closing it. `[` and `]`
are two separate keys; `/` has no assigned action.

Opening a context menu preserves the underlying selection and scroll position.
A page rebuild dismisses its menu safely. Media keys remain available while a
menu is open; navigation is modal. The first input after screen blanking wakes
the screen and is consumed.

## Touch and pagination

Tap a row for its normal action. Hold an album, artist, song, or Up Next row to
open that row's context menu. Release after a hold does not activate the normal
tap action. Tap outside the menu or press Back to dismiss it.

Settings → Screen → Pagination offers **Off**, **On touch 64**, and
**On touch 96**. The latter values set the square button size in pixels.
The stacked buttons page their own list, including either pane in split views
and Up Next. They use 65% transparency, appear after touch input, and hide when
keyboard/knob navigation resumes. This setting controls the touch buttons;
physical Page Up/Down keys remain list-paging controls.

## Context menus

| Selected item | Actions |
| --- | --- |
| Album in Albums, or album header in artist detail | Play, Shuffle Play, Add to Queue, Play Next, Go to artist |
| Artist in Artists | Play, Shuffle Play, Add to Queue, Play Next |
| Song in album detail, artist detail, Songs or playlist detail | Play, Add to Queue, Play Next, Star/Unstar, Add to Playlist, Go to album, Go to artist |
| Entry in Up Next | Play Next, Remove from Queue, Add to Playlist, Go to album, Go to artist |
| Now Playing controls / queue closed | Go to album, Go to artist, Star/Unstar, Add to Playlist, Shuffle, Repeat |

Album/artist/song menus show the selected item's title and applicable actions.
Now Playing groups actions under Go To, Star, Playlists and Playback. Destinations without
an album/artist identity are omitted from library/queue menus.

- **Play** uses the view's existing playback action. Starting an album or artist
  replaces the queue. Playing a song from album detail starts that song and
  the following album tracks; artist and Songs playback retain their existing
  displayed-order rules.
- **Shuffle Play** replaces the queue with the selected album/artist, shuffles
  it, enables random playback, and then starts its first song.
- **Add to Queue** appends the selected song or all songs in the selected
  album/artist. It does not clear the existing queue or start/resume playback.
- **Play Next from the library** inserts immediately after the current track.
  With no current track, insertion starts at the front and does not start
  playback. Album/artist additions preserve their displayed song order.
- **Play Next in Up Next** moves the selected existing occurrence after the
  current track; it does not add a second copy. An entry that has become current
  before processing is left in place.
- **Remove from Queue** removes that occurrence by server queue-entry ID.
  Removing a currently playing entry, if playback advances while the menu is
  open, follows the server's normal delete behavior.
- **Add to Playlist** opens an existing-playlist picker for the captured song.
  Touch a destination or use Up/Down and Enter. Back returns to the song menu;
  Context or a backdrop tap closes it. The operation appends without changing
  playback, queue order, volume or shuffle. Repeated additions append repeated
  entries, following MPD's normal playlist behavior. Stared is excluded; use
  Star/Unstar for that list. Empty destination lists show a message and Back.
- **Star/Unstar** targets the selected song, including when another song is
  playing. MPD writes Tracks' server-side **Stared** playlist. Its confirmed
  server state controls the label; a network round trip may be visible.

The Add to Playlist implementation is built, host-tested and OTA-verified. The
live check exercised the picker and cancel without writing to user playlists.
It uses up to the 32 regular server playlists supported by the
existing catalog. Create a new playlist in the MPD client/server first. The
picker freezes full names and the server connection identity; a later list
reorder does not redirect the selected destination. The worker verifies the
name still exists immediately before `playlistadd`, then refreshes stored
playlists after success. Queued acceptance is shown as a request; server errors
remain in MPD diagnostics.

Menus capture item IDs when opening. Queue handles also carry a connection
epoch, so an old menu cannot edit a reused ID after reconnect. Queue ID zero is
valid, and duplicate copies of the same song have different occurrence IDs.

Tracks uses explicit insertion positions to preserve order even with shuffle
on. Standard MPD servers advertising `prioid` use priority for Play Next while
random is on; Tab5 promotes the following requested entry as playback advances.
That ordered promotion is maintained by the connected Tab5 client.

Add to Queue uses **nrk:list-arrow-to-bottom-expressive**; Play Next uses
**nrk:list-arrow-to-top-expressive**. Both icons have fully opaque list strokes
and use the active theme/focus color. Normal edge antialiasing is retained.
See the [icon sources and licenses](../assets/icons/README.md).

## ColoPlay and USB input

With both applications updated, ColoPlay offers an opt-in **Tab5 Navigation**
scene: knob turn/press selects, Button 1 goes Back, Buttons 2/3 switch views
(hold to page), and Button 4 opens/closes the context menu. Tab5 returns its
current view and focused item for ColoPlay's display. The [navigation extension
guide](coloplay-ble.md#navigation-feedback-and-direct-control-scene) records the
setup and feedback contract. This profile does not alter the key semantics above.

ColoPlay accepts these unmodified keyboard HID usages:

| Action | HID usage |
| --- | --- |
| Up / Down | `0x52` / `0x51` |
| Enter / Escape | `0x28` / `0x29` |
| Page Up / Page Down | `0x4b` / `0x4e` |
| `[` / `]` | `0x2f` / `0x30` |
| Context (`C`) | `0x06` |

For ColoPlay Star/Unstar, use keypad multiply `0x55` (85), modifiers zero, or
Shift+8 (`0x25`, left Shift `0x02` or right Shift `0x20`). This shortcut toggles
the **currently playing song**. ColoPlay's Playback screen and Navigation's
Now Playing view show a small gold dot below the time line only for a confirmed
starred song. Unstarred, unknown, stale or disconnected state hides it. The
former star icon is removed in release 0.8.1; the **Status icon**
switch now controls only the playback-state icon. To star a
different song, select it and choose
Star/Unstar in its context menu. Other modified navigation keys are rejected.

These ColoPlay bindings use USB CDC-ACM when its handshake is active and
Bluetooth otherwise. **Global** contains shared bindings for every scene;
local bindings override matching Global key/event pairs. Global cannot be
activated or used as a scene-switch target. The navigation preset is editable:
the verified device adds Button 4 Hold for Star, but a newly created preset
retains Button 4 Single for Context and does not install that extra binding.

USB keyboard Page Up/Down, brackets, and C enter the same UI actions. The
asterisk bindings above are ColoPlay bindings; the USB keyboard mapper does
not currently map asterisk to Star. D100H's dedicated Context and Star gestures
remain available. See the [ColoPlay guide](coloplay-ble.md).

## Web and serial debugging

POST `/api/key?k=<action>` uses the same UI dispatch:

| Action names | Behavior |
| --- | --- |
| `up`, `down`, `enter`, `back`, `context` | Navigation and context |
| `pageup`, `pagedown` | List paging only |
| `tabprev`, `tabnext` | Bracket navigation |
| `queue` | Explicitly toggle Up Next on Now Playing; separate from Page Up |
| `nowplaying`, `albums`, `artists`, `songs`, `playlists` | Select a tab |
| `prev`, `next`, `play`, `star`, `shuffle`, `repeat` | Global media actions |
| `volup`, `voldown`, `mute` | MPD volume actions |
| `screenoff`, `wake` | Blank/wake the display |

Web `prev`/`next` mean tracks; `tabprev`/`tabnext` mean tabs. Global `star`
targets the playing song; the selected-song Star action is inside its menu.

Where a serial console is available, `w`/`s`, Enter, `b`, `p`/`n`, `[`/`]`,
`c`, and digits `1`–`7` map to navigation, paging, context, and playback as above.
There is no separate serial `queue` word command. USB-C normally provides
[CDC-NCM networking](usb-network.md), so use the web debug page on running MPD
firmware rather than relying on a USB-C Serial/JTAG console.
