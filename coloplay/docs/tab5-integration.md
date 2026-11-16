# Bluetooth controls and Tab5 feedback

Current ColoPlay source: **0.8.1-navigation-details**; the verified OTA
is in the [release record](development.md#current-verified-release).
Both applications now live in the [MPD Tab5 repository](../../README.md), using
the common definitions in [shared/](../../shared/README.md).
Volume-scale support follows Tab5 commit
[`80acc5d`](https://github.com/rGnng/mpdtab5/commit/80acc5de9439fda8e1da57e541adb66d2f1cc20a), using matching
shared protocol and volume-grid headers. The original recorded Tab5 peer driver is
[`79c5dac`](https://github.com/rGnng/mpdtab5/commit/79c5dacd1f38a955f3ea2a8abff5b719d5d616d3),
built on [`a34fa8a`](https://github.com/rGnng/mpdtab5/commit/a34fa8a2c7547573f128b47061d7359f3f504a81).
The existing MPD volume-grid helper and worker remain authoritative. Tab5's
D100H support, USB, iAP2, storage, player UI and configuration remain in place.

## Pairing

1. In ColoPlay **Connection → Bluetooth**, enable Bluetooth and choose **Start pairing**.
2. In Tab5's existing Bluetooth page, scan and select **ColoPlay**.
3. Check Connected, extension support and fresh feedback in ColoPlay's status.

Pairing is open for 120 seconds. Devices use encrypted Secure Connections
Just Works bonding. Settings and bonds survive restart; known peers reconnect
without opening pairing again. Forget clears ColoPlay's peer and bonds; also
forget it on Tab5 before pairing again. Tab5 supports one connected controller:
selecting ColoPlay replaces the active connection, retaining other saved peers.
Screen/LED inactivity sleep leaves Bluetooth connected and receiving state.
Bluetooth is initially disabled on a new installation.

## Star feedback

ColoPlay shows a small gold dot below the playback elapsed/total or remaining
time only when the current song is starred. The former star icon is removed.
This is Tab5's
confirmed current-track state, including changes made directly on Tab5. The
existing keypad `*` HID shortcut still toggles the current track. No new key
mapping is installed and failed commands do not predict a star change.

The same indicator appears in Tab5 Navigation's Now Playing view. Its visibility
is independent of the playback Status icon switch and remains in the footer
when time is hidden. Unstarred, stale/disconnected state, missing track and
older Tab5 firmware hide it. Both application images need
the star-feedback feature for the indicator to appear. The 0.8.0 applications
and the 0.8.1 replacement dot image were OTA-tested on 2026-11-16. The saved Button 4
hold binding starred/unstarred the current track over USB; both USB and
Bluetooth feedback confirmed each state, and the original star state was restored.
USB retained command ownership. Wireless-only command routing was host-tested;
the cable was left connected for this release check.

Optional feature byte 17, bit `CP_FEATURE_STAR`, is advertised in ColoPlay's
GATT capabilities / USB OFFER and echoed by Tab5 HELLO. Supporting peers use
`CP_STATE` flags bits 2/3 for known/starred; bit 7 retains backend connectivity
and bits 0/1 retain seeking. Older peers receive the original flag layout.
No extra packets, polling, assets or settings records are needed.

ColoPlay is the peripheral/GATT server; Tab5 is the central/GATT client.
Standard HID and a custom command/state service share the connection. Tab5
uses the extension for all ColoPlay commands, preventing duplicate HID delivery.
Without the extension, ColoPlay can emit keyboard and consumer HID reports;
generic computer/phone HID interoperability still needs physical host testing.
Standard HID alone supplies neither exact fractional steps nor volume feedback.
[Bluetooth HID over GATT](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/hogp_v1-2_1777405843/HOGP_v1.2/out/en/index-en.html)

## Actions and screens

The offline command selector groups local actions by purpose and remote actions
under **MPD Tab5**. The same remote commands use USB CDC-ACM or Bluetooth, with
USB taking priority after its handshake. PC-companion serial actions remain
deferred. Legacy `bluetooth_*` API identifiers are retained for saved bindings.

| Group | Controls |
|---|---|
| Device Scenes | Next Scene, Previous Scene, Switch to Scene |
| Device Display / Lighting | Screen/ring/button brightness, rainbow, static color |
| Device Speaker / System | Local speaker volume, tone, sleep/wake, time sync |
| MPD Tab5: MPD volume | Volume Up/Down; Tab5 selects its configured grid |
| MPD Tab5: MPD playback control | Play/Pause, Play, Pause, Stop, Previous Track, Next Track, Fast Forward, Rewind, Mute |
| MPD Tab5: Tab5 key | Keyboard HID usage and modifier mask |

Play/Pause, Previous Track and Next Track follow
[Apple remote-command naming](https://developer.apple.com/documentation/mediaplayer/mpremotecommandcenter).
HID identifiers use Consumer page `0x0C` and Keyboard page `0x07`, rather than
platform virtual-key codes. See the shared protocol header for exact values.

**MPD volume** and **Playback** are available as both idle
screens and action screen effects; Clock and Blank remain available. Volume
shows a speaker icon and horizontal bar, independently of the
ColoPlay speaker. The screen and LEDs predict immediately using Tab5's
synchronized grid. Actual mixer feedback remains separately available in
diagnostics. Pending turns are reconciled by completed request ID.
Proportional LED feedback fills the ring using that same remote source.
A disconnected/stale source shows the connecting icon; an unavailable mixer
shows an empty bar, never a fabricated zero. Request IDs, pending state
and outcomes remain in Device diagnostics; volume command transitions also
appear in the web log. Playback shows state, track text and seek direction, with position on the
configurable LED ring. Metadata uses an embedded 16 px Noto Sans JP/CJK bitmap font with 10,702 unique
characters: common Japanese, the top 1,500 GB2312 Han by frequency, 2,350 common
Hangul syllables plus Jamo, Latin and audited library supplements. Shared Unicode
Han uses one Japanese glyph. Decomposed modern Hangul is composed for display.
Unsupported characters, including emoji and rarer syllables/ideographs, use `?`.
The [font guide](../assets/fonts/noto-sans-jp/README.md) records the repertoire,
source priority and complete-library audit. Composition and glyph lookup happen
on ColoPlay; Tab5 continues sending its existing UTF-8 metadata over BLE.

A binding holds up to three ordered actions, including keyboard taps. This
supports short sequences; a dedicated 16-step macro editor is not implemented.
Generic HID supports modifier masks. The Tab5 adapter currently accepts only
unmodified navigation usages: arrows, Return, Escape, Page Up/Down, brackets
(previous/next screen), C (context), and digits 1–7. It returns Unsupported for
other keys/modifiers. These commands enter Tab5's existing input queue.

### Volume behavior

ColoPlay sends only Volume Up/Down and detent count. Tab5's **Volume Scale**
selects the control coordinate and absolute MPD target:

| Choice | Control positions | Raw MPD target for position `x` |
|---|---|---|
| 1/16 Squared Curve | `i / 16` | `x²` |
| 1/64 Squared Curve (default) | `i / 64` | `x²` |
| 2% Integer | 0%, 2%, 4%, …, 100% | `x` |
| 5% Integer | 0%, 5%, 10%, …, 100% | `x` |

The integer profiles provide straight integer-percent targets for Linux/Windows
and integer-only MPD servers. Switching profiles preserves the playing volume;
it changes the readout mapping and subsequent commands. The existing saved
`volume_steps` IDs 16/64 remain selected as the corresponding squared profile;
new/missing configurations default to 64. IDs 2 and 5 select the integer profiles.

Both devices share 154 compile-time float targets (616 bytes). Squared readback
is mapped to the nearest lookup position before stepping; its displayed value
is that position's control fraction. Integer profiles use directional snapping
for external off-grid values: from 61%, 2% Integer goes up to 62% or down to 60%.
Commands always use absolute `setvol`; every batched detent is counted.

For example, displayed 25% in a squared profile sends raw `setvol 6.25`.
A hardware return of 6.3488% maps back to the same displayed 25%. Actual raw
feedback stays in diagnostics. The confirmed raw target is kept separately
from its readback; external changes, missing mixer and MPD reconnect invalidate
that reference. This avoids accumulating quantization offsets.

[Tracks `628ee5b`](https://github.com/rGnng/tracks/commit/628ee5b) accepts decimal
percentages. Older integer-only servers retain an explicit invalid-argument
fallback, but the Integer profiles are the appropriate choice for their targets.
MPD `-1` means no mixer. Mute retains the selected scale's saved restore behavior.

### Observed macOS volume scale

With the tested Tracks/macOS output, accepting a fractional MPD `setvol` does
not guarantee identical mixer readback. Tracks accepts the requested percentage;
the system/output volume path can quantize it, and status reports the actual
returned scalar. Here, "raw" means that normalized mixer value (`0..1`), not a
log message or the controller's logical position.

The observed values fit `raw = (n / 127)²`, for integer `n` from 0 through 127:

| Requested raw MPD percentage | Observed raw percentage | Matching `n` |
|---|---|---|
| 6.25% | 6.3488% | 32 |
| 25% | 25.3953% | 64 |
| 28.125% | 27.8319% | 67 |

This is an inferred 128-level squared model for the tested output, not a
universal CoreAudio guarantee. Other devices, drivers and mixer implementations
can use different scales. Integer requests can also be quantized; sending 5% or
61% does not force this output to return exactly those values.

A 64-step controller and this inferred 128-level output are separate grids.
Adding a fixed raw `1/64` or `1/16` to each rounded readback shifts the next
target away from the intended control grid. Showing a predicted raw target and
then replacing it with its quantized echo also caused the visible
28.125% → 27.8319% correction in the earlier implementation.

The squared profiles instead define a logical position `x = i / N`, with
`N = 16` or `64`, and request raw target `x²` using absolute MPD `setvol`.
Readback maps to the nearest target in that profile, and a confirmed target/echo
pair preserves the logical reference until an external change invalidates it.
For example, control position 25% requests raw 6.25%; the observed raw 6.3488%
maps back to the same 25% position. ColoPlay's bar and ring use that position;
the physical volume screen shows no number. Actual raw feedback remains in
diagnostics, separate from the target and predicted display coordinate.

The firmware does not hard-code the inferred 128-level hardware model. The
shared lookup tables contain the four selectable control profiles: 17 + 65 +
51 + 21 = 154 raw targets (616 bytes per device). Nearest-target lookup uses
binary search, with no runtime square root or logarithm. Tab5 announces the
selected profile over BLE; ColoPlay uses the same table for immediate prediction
and reconciles pending turns against confirmed feedback. The 2% and 5% Integer
profiles provide linear whole-percent commands for Linux/Windows or integer-only
servers; they cannot remove quantization imposed by an output device.

### Responsive prediction

Tab5 announces its four-profile scale setting during connection setup and refreshes it
when settings change. With negotiated prediction support, volume feedback adds
its confirmed logical grid reference and the last completed volume-command ID.
ColoPlay waits for valid synchronization, then predicts each queued turn using
the same directional grid arithmetic as Tab5. Commands remain direction/count
only, with no step selector in the ColoPlay editor.

A bounded queue retains unconfirmed turns. A reply retires commands through its
snapshot's completion watermark and replays only newer turns. Accepted results
do not complete commands; older snapshots cannot rewind newer feedback. A
quantized readback is kept separate from its logical reference (for example,
actual raw 6.3488%, target raw 6.25%, displayed 25%). This preserves consistent prediction through rapid
reversals and host grid changes.

Prediction stays visible while turning or awaiting confirmation. After all
turns finish and input has been quiet for 150 ms, the display level blends to
actual feedback over 100 ms. Disconnect, stale/missing state, rejection or a
five-second timeout disables optimism until synchronization recovers. Peers
without the new capability continue to use raw feedback only. Older prediction peers do not interpret the new scale IDs. This uses
bounded static storage and no extra BLE packets or runtime asset work.

Old ColoPlay bindings with ±16 or ±64 are normalized to ±1 on load/save, keeping
their direction, sound, effects, duration and other scene settings. The web
editor offers only Volume Up and Volume Down. The NVS v7 layout is unchanged.

Fast Forward and Rewind are stepped `seekcur +5` / `seekcur -5` operations,
not changes in playback speed. Configure **Hold → Start** and **Hold then
release → Stop**. Click/turn bindings can use a bounded pulse. Scene/config
changes send Release; disconnect and a 10-second Tab5 lease also stop seeking.
The custom seek state is separate from existing USB/D100H hold state.

### Direct navigation profile (0.6.32)

With both applications updated, choose **Controls → + Tab5 navigation** on
ColoPlay, then **Activate scene**. The new scene appends without replacing the
previous Tab5 BLE scene or any other settings. Knob turn/press navigates and
selects, Button 1 goes Back, Buttons 2/3 switch views, Button 4 toggles the
context menu. Holding Buttons 2/3 pages up/down from 350 ms, repeating every
200 ms; release after paging does not switch views. Press-and-turn retains
volume control. Previous view on Now Playing retains Tab5's Queue shortcut.

The idle screen shows the real remote view and focused item, including context
commands, with a position counter when available. On Now Playing it uses the
playback layout. BLE capability `CP_CAP_NAVIGATION=128` negotiates this optional
stream. Tab5 takes a bounded UI-thread snapshot and the BLE worker sends only
copies. Legacy peers continue their existing media/volume protocol.

All frames remain 20 bytes with version/session fields unchanged. Kind 21
(`CP_NAVIGATION`) has revision in bytes 2–3, view/flags in 8/9, zero-based index
in 10–13, count in 14–17 and focus UTF-8 byte length (0–95) in 18–19. Kind 22
(`CP_NAV_TEXT`) uses the same revision, byte offset in 8, length (1–10) in 9,
and text in 10–19. The consumer publishes only a complete matching revision;
new snapshots supersede unsent text. A one-second metadata heartbeat refreshes
unchanged focus. Partial/stale packets cannot mix labels across views, and
navigation expires independently after five seconds. Flags and telemetry are
listed in the [API reference](api.md#scenes-and-gesture-bindings).

### Previous test profile

The **Tab5 BLE** scene uses navigation bindings matching Tab5's existing UI:

| Input | Binding |
|---|---|
| Knob counterclockwise / clockwise | Up / Down item |
| Knob press | Return / Enter: activate focused item or confirm an edit |
| Button 1 | Escape / Back |
| Button 4 first, then Button 2 | Page Up |
| Button 4 first, then Button 3 | Page Down |
| Button 2 first, then Button 1 | `[` previous screen/menu |
| Button 2 first, then Button 4 | `]` next screen/menu |

The same Button 2/4 pair therefore has two actions depending on which is held
first. Keep the modifier down and tap targets repeatedly. Simultaneous presses
have no modifier order; they only match an explicit Any order binding, if one
exists. Ordered bindings take priority over unordered bindings for matching
orders. Shared Global scene-switch bindings remain available.

Tab5 handles Enter through the current page/menu handler, falling back to
`focus_activate(true)`; edit mode confirms/exits instead. Back uses the existing
page/back behavior. `[` on Now Playing opens its queue; elsewhere it selects
the previous enabled screen. `]` selects the next enabled screen. Bluetooth
settings are skipped by screen navigation, as in the existing Tab5 UI.

Idle **Playback** uses a small drawn playback-state icon at the
top, without Playing/Paused/Stopped labels, with 16 px title/artist lines surrounding the vertical center. Long text is
measured and ellipsized at UTF-8 boundaries, or scrolls when enabled on the
[Playback page](firmware.md#playback). A slim LCD progress bar/time sits
below; the configured LED-ring progress remains independent. There are no emoji
or decorative boxes. By default, when feedback is unavailable, both remote playback/volume screens show
only the locally bundled `andrsrxn:bluetooth-connecting` icon, without connection
text. Glyph data stays in flash; no whole font is loaded into RAM.

Scene **Ring display → Playback progress** fills the
physical LED ring from elapsed/duration using scene colors and brightness.
Playing uses a shared smoothed local clock between position packets; repeated
heartbeats/ACKs do not reset it. Pause freezes, Stop clears,
and unknown/stale state clears the progress fill. Partial pixels smooth the nine
LED segments. The progress indicator is independent of the LCD idle screen.
**Playback → Lights** offers independent state/channel plans and
**Progress fill** / **Volume feedback fill** choices: Clockwise or Bottom to top. Symmetric fill lights both
lower sides together, rising to the top at 100%, using the same partial-pixel
renderer. The scene-level fill menu is removed; first initialization copies the
active scene's legacy style into both shared settings without changing scenes.
The page also controls ring/button colors and brightness, unfilled dimming and
Playing, Paused and combined Stopped/Disconnected behavior. **Screen** controls visible elements,
long-text handling and state-specific Clock/Blank/connecting fallbacks.
Knob action light effects temporarily take priority over playback lighting.
Button actions affect only their own LEDs; combinations affect all participating
buttons. Each target returns to playback lighting after the configured duration.
Sleep turns both groups and the screen off. The BLE wire format and Tab5 settings
are unchanged by this local appearance feature.

Device **Sound effects volume** is saved independently of brightness and remote
MPD volume. It scales local generated tones, including scene-change cues; 0% is
silent. Existing scene-cue enable and high/low direction tones are retained.
The default effect gain is 70%; the existing codec speaker level remains separate.

## Wire protocol and execution

The [shared protocol header](../../shared/coloplay_ble_protocol.h) defines version
1 for both applications. Every frame is 20 bytes, fitting the default ATT payload without requiring
an MTU upgrade. All multibyte integers are little endian.

| Characteristic | UUID suffix | Direction |
|---|---|---|
| Service | `0001` | Base `a91c0001-c169-4acb-9f5d-121d621d17b5` |
| Capabilities | `0002` | Tab5 reads version, capabilities and session nonce |
| Command | `0003` | ColoPlay notifies commands; encrypted CCCD |
| State/result | `0004` | Tab5 writes with response over encryption |

Common header: version byte 0, kind byte 1, request ID bytes 2–3, session
nonce bytes 4–7. Command bytes 8–15 contain usage, count, parameter and pulse
duration as four uint16 values. New volume commands contain only the Up/Down
usage and detent count: parameter is the fixed legacy host marker 1, duration
and bytes 16–19 are zero. Tab5 accepts legacy step fields during rolling updates
but ignores their grid selection.
Release cancels custom
seeking. Session checks and a bounded recent-request cache prevent duplicate
controls and stale replies after reconnect. Queues are bounded; no old command
is replayed after disconnect. Neither firmware performs MPD socket I/O in the
render/input or BLE callback path.

HELLO reports the native volume grid. Capability bit `CP_CAP_VOLUME_FLOAT`
(0x10) in the capabilities byte and the returning HELLO byte 16 negotiates exact
volume feedback. HELLO bytes 12–15 carry the normalized native step. A companion
`CP_VOLUME_STATE` (kind 20) carries normalized float volume in bytes 8–11 and the
request result in byte 12, using the same request ID/session as STATE. -1 means
unavailable. Negotiated `CP_CAP_VOLUME_PREDICT` plus `CP_CAP_VOLUME_SCALE` adds the scale ID in
byte 13, last completed volume request ID in bytes 14–15, and the confirmed
raw target (float, -1 when unanchored) in bytes 16–19. These fields describe
the same published mixer snapshot, independently of the envelope's request ID.
New peers use this frame for volume and command outcomes; the
legacy STATE remains intact for playback/timing and older peers. All frames
still fit the default 20-byte ATT payload, with full 32-bit elapsed/duration.

STATE carries legacy integer percentage volume, playback
(0 stopped / 1 playing / 2 paused / 255 unknown), backend availability, seek
direction, request outcome, elapsed and duration in milliseconds. Outcomes are
Pending, Accepted, Applied, Failed, No change, Unsupported and Busy. Applied
is sent after the existing MPD worker processes the request and refreshes its
state; Accepted is only a queue acknowledgment. Track identity/revision and
bounded title/artist text are separate frames, up to 63 bytes per string.

Periodic state is scheduled at 400 ms on Tab5's existing 250 ms supervisor,
usually yielding about 500 ms updates; command results also schedule an event
immediately. Feedback older than five seconds becomes unavailable. External
MPD changes and natural track transitions publish state without creating new
commands. Live volume and playback frames refresh at up to 20 fps on ColoPlay;
Clock remains 5 fps. Incoming feedback does not wake a sleeping gadget.

The existing MPD worker checks queued commands between roughly 100 ms wait
chunks. Returned volume and screen/ring feedback keep local response
independent of that backend cadence. The requested BLE connection interval is
7.5–15 ms; this is not an end-to-end latency measurement. Live API tests include
HTTP-relay overhead and cannot establish physical button-to-screen latency.
ColoPlay enables Wi-Fi modem sleep before BLE starts to permit radio coexistence
on the pinned Arduino/IDF SDK. Tab5 continues using its existing hosted BLE stack.

## Validation and limits

Both application images are OTA'd through the HTTP relays, preserving
bootloaders, partition tables and Tab5 C6 firmware. Live checks verified encrypted
pairing, bond reconnect, raw MPD volume return, native/coarse volume round trips,
Play/Pause state, both seek directions and release, Page Up/Down acknowledgment,
track metadata and continued MPD connectivity. Original settings were compared
against saved snapshots. Physical key recognition and generic PC HID require
hands-on testing; API run tests dispatch saved bindings directly.

Host tests cover packet validation, NVS v3→v4 migration, grid arithmetic,
unsupported/stale controls, seek release, duplicate requests, reordered GATT
characteristics, disconnect generations and the existing D100H lifecycle.
Tab5's full baseline suite has existing credential/legacy-fixture failures;
comparison against untouched `a34fa8a` found no new failing cases. Its focused
MPD/volume/BLE tests and native UI CTests pass. See the release record for exact
image fingerprints and remaining live qualification limits.

## USB CDC-ACM transport

The wired controller link uses Tab5's USB-A host port and ColoPlay's USB-C port.
It carries the same controls, playback, volume-scale and navigation feedback as
BLE. USB is preferred after the handshake; BLE remains available wirelessly.
Tab5's existing USB-C CDC-NCM computer/MPD network stays in place. The default
ColoPlay Serial + JTAG mode works without changing USB settings.

The [USB transport guide](../../docs/coloplay-usb.md) covers framing, sessions,
input ownership, stale commands, reconnects and diagnostics. Serial logs cannot
be interpreted as controls. CDC-ACM is USB serial; CDC-ECM/NCM are networking.

A PC companion and general-purpose keyboard macros are outside the current
MPD/Tab5 focus. The original proof-of-concept firmware remains available as a
reference for separate projects.

Tab5's existing HTTP controls remain available for IP integrations:
`POST /api/mpd/volume`, `/api/mpd/control` and `GET /api/status`. Root `volume`
is Tab5's feedback speaker setting; `mpd_volume` is MPD playback volume.
No BLE-versus-Wi-Fi latency comparison has been measured.

## Detail navigation feedback (0.8.1)

Both updated images were OTA-tested on 2026-11-16. They send semantic feedback over
USB and Bluetooth without changing Tab5's input actions or saved settings:

| View | Heading | Focus and position |
| --- | --- | --- |
| Album detail | Album name | Play/Shuffle without a counter; songs start at 1/n |
| Artist detail | Artist name | Play/Shuffle without a counter; album and song positions count their own items |
| Playlist detail | Playlist name | Play/Shuffle without a counter; songs start at 1/n |
| Settings | Settings or selected section | Setting name/current value; actionable-row position |
| Clock editor | Set time | Field name/value through editing blink; Increase/Decrease controls without a field counter |

Settings values refresh during editing. Decorative headers and playback
buttons do not inflate song positions. The optional heading stream uses
`CP_FEATURE_NAV_HEADING`; see the [shared protocol](../../shared/README.md) for its frame layout.
Legacy peers retain the original navigation frame format and generic headings.
