# ColoPlay USB CDC-ACM controller

Connect Tab5's **USB-A host port** to ColoPlay's **USB-C port** using a data
cable. Tab5's existing USB-C CDC-NCM connection to the computer remains in
place, including its MPD connection. No computer companion is required.

ColoPlay's default **CDC-ACM Serial + USB JTAG** mode supports the controller
link. Its alternative **CDC-ACM Serial + CDC-ECM Network** mode exposes the
same control protocol on its serial interface. These modes retain their
existing saved settings; this feature does not change USB descriptors,
bootloaders, partitions or the default mode.

## Operation

USB is selected after a successful ColoPlay handshake. Without that handshake,
the serial port remains a console and BLE remains the controller link. Pair
BLE once if wireless fallback is wanted. BLE may remain connected while USB
owns input, but its control packets are not executed during that time.

The same bindings send navigation, media and volume commands over the selected
transport. Both links return playback, track/artist text, selected volume
scale, raw volume, completion watermark and current focused item. ColoPlay
uses the same prediction, playback clock and rendering code for either link.
Tab5 remains authoritative for MPD state and volume scale.

General's USB & Network panel shows **Controller link**. ColoPlay's status JSON
adds `controller`, and Tab5's status JSON adds `coloplay_usb`. The latter
reports serial-open/handshake state, session, frame counts, invalid/dropped
frames and open attempts. Bluetooth diagnostics continue to describe BLE.

## Framing and sessions

The shared [stream envelope](../shared/coloplay_usb_frame.h) wraps the existing
20-byte protocol payload in a 32-byte frame:

| Bytes | Meaning |
| --- | --- |
| 0–3 | Marker `c7 50 35 55` |
| 4 | USB envelope version 1 |
| 5 | Probe, Offer, Hello, Ready, Data or Ping |
| 6–7 | Payload length 20, reserved zero |
| 8–27 | Existing command/state payload |
| 28–31 | Little-endian CRC32 of bytes 4–27 |

The bounded parser accepts fragmented/coalesced frames, ignores console logs
and recovers after corrupt or truncated input. Frame writes are all-or-nothing
at ColoPlay's transmit queue; they share the console's nonblocking drain.
Binary input is kept out of the console's text-command parser. A dedicated
ColoPlay task owns serial RX/TX and USB heartbeats, independent of HTTP. Plain
console bytes go through a bounded queue to the main task. The worker starts
after USB mode initialization, avoiding races with the Serial/JTAG-to-OTG switch.

Tab5 probes with a fresh random challenge on every serial open. ColoPlay
offers a fresh session nonce and echoes that challenge. Tab5 returns Hello
with its scale/capabilities; ColoPlay replies Ready. Only then does Tab5 give
USB ownership. A changed challenge resets ColoPlay's pending prediction and
request sequence, including when USB reopens without a physical unplug.

Tab5 sends state approximately every 250 ms and navigation changes on its
USB task's next iteration. ColoPlay sends a heartbeat every 250 ms. Tab5
releases USB after one second without valid peer traffic; ColoPlay falls back
after 1.5 seconds without valid host traffic.
Tab5 handles physical disconnects through the CDC driver. ColoPlay ignores
brief Arduino Serial/JTAG SOF/connected-flag gaps while its validated protocol
lease remains fresh. These are lease limits,
not measured control-latency guarantees.

Each USB session has a bounded recent-request cache. MPD commands carry the
current ownership epoch as well as the session nonce; queued commands from
before a handover are rejected. BLE disconnect/reconnect cannot revoke a live
USB owner. Handover clears seek state and volume completion tracking. An
already issued MPD command can finish; uncertain commands are never replayed
automatically over the other transport.

## Driver and recovery

Tab5 adds Espressif's pinned `usb_host_cdc_acm` 2.4.1 driver on the existing USB
host stack, retaining USB 1.6.0 and the existing HID driver. Only Espressif VID
`303a`, PID `1001` (Serial/JTAG) or `4004` (ColoPlay composite) is considered;
the protocol handshake is still required. Existing devices are scanned as
well as new attachments. The host never selects 1200 baud or asserts RTS.

Both firmware images are application-only OTA updates. The local recovery
bundle is prepared before deployment; ColoPlay recovery must understand its
`CSR1` scene records. The normal factory bootloader and saved settings remain
in place. See [ColoPlay flashing and recovery](../coloplay/docs/flash.md).

Tests exercise the production host driver and device router with fake I/O,
MPD ownership transitions, malformed/fragmented framing, duplicate requests,
stale sessions, metadata, navigation, volume prediction and fallback. Physical
qualification is recorded separately from those host tests.

## Live qualification on 2026-11-16

Both images were OTA'd and their running identities verified. USB controls and
feedback were exercised through saved bindings: view switching, focus, paging,
context/Back and a volume up/down round trip. The displayed grid and actual
raw volume returned to their starting values, with no BLE control packets
sent during USB ownership. Frame error/drop counters remained zero. The user
confirmed physical controls over USB and wireless controls after moving the
power cable. USB subsequently reconnected with a new matching session and fresh
focus feedback, while Tab5's MPD, SD and USB-C network remained available.

The [ColoPlay release record](../coloplay/docs/development.md#current-verified-release)
contains image fingerprints, test coverage and the limits of this qualification.

Live cable checks used Serial + JTAG. The software CDC-ACM + CDC-ECM mode
shares the protocol and passes console/build fixtures; it was not selected
for this physical qualification.

Final ColoPlay 0.7.2 verification also holds a partial HTTP header beyond the
USB lease: the independent worker keeps heartbeats running. Forty-five
subsequent samples retain one session/open count with zero lease timeouts,
invalid/stale frames or drops. Early Serial/JTAG connected-flag gaps no longer
reset a fresh protocol session.
