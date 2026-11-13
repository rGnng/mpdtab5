# USB-C CDC network

USB-A HighSpeed Only (mode 0) allocates the HS controller to the existing USB
host stack and the FS controller to TinyUSB CDC-NCM. USB-C uses Tab5 FSLS PHY0
on GPIO24/25; Serial/JTAG pads stay disabled after remapping so GPIO26/27 remain
available to the I2S speaker. Mode 2 continues to allocate both controllers to
the host stack and does not install TinyUSB.

The independent ESP-netif has a stable locally administered MAC, a distinct
host NIC MAC, static `/24` addressing, gateway `0.0.0.0`, and route priority 0.
Wi-Fi retains its normal route priority and IP events. The USB link performs
no bridging, NAT, or IP forwarding. The same HTTP server listens on both links.

The default addresses are exactly `172.32.30.1` (Tab5) and `172.32.30.2`
(client). The web configuration validates both addresses before mutating any
settings and stores them under `/sdcard/mpd/config.json`; changes apply after
reboot. `/api/config` exposes `usb_net_ip` and `usb_net_client_ip`.

ESP-IDF 5.5's `esp_netif_dhcps_option()` rejects equal pool start/end addresses.
The USB service instead configures its own IDF DHCP handle on the TCP/IP
thread with identical pool endpoints. A lease callback is registered before
starting the server, and leases are cleared on detach/re-enumeration. Router
and DNS offers are disabled. `CONFIG_LWIP_DHCPS_ADD_DNS=n` also removes IDF's
fallback DNS-server option; Wi-Fi station DNS acquisition is unaffected.

The TinyUSB task only copies inbound frames into ESP-netif and reports link
events to a service task. Outbound frames are sent from the TCP/IP thread with
a bounded timeout. USB initialization failure leaves Wi-Fi/OTA available.

The ESP-netif driver has a persistent, non-null driver context. IDF aborts on
attachment if that mandatory handle is absent. A regression check runs the
production driver configuration through IDF's actual startup sanity check.
A boot guard retains panic addresses across a reset and skips USB networking
for that firmware after a USB-active panic, keeping Wi-Fi/OTA reachable. A new
firmware image clears that recovery state. Diagnostics expose executable
return addresses only, not arbitrary stack contents.

## Checks

- Firmware builds with ESP-IDF 5.5.6, pinned esp_tinyusb 3.0.0.
- Five production address-planning tests cover exact defaults, custom subnets,
  duplicate/out-of-subnet addresses, invalid host addresses, and malformed input.
- Five DHCP/startup tests compile IDF's actual DHCP server and the production USB
  DHCP setup against a transport shim. They check OFFER/ACK/renewal bytes,
  omission of gateway/DNS options, pool exhaustion, and replacement of a host
  after reconnection, plus the mandatory network driver handle.
- Five boot-guard tests check panic/watchdog recovery, normal reboots, and
  clearing recovery when the firmware image changes.
- Chromium checks load and save USB addresses as strings and preserve separate
  Wi-Fi and USB status display.

Run `python3 -m unittest discover -s tests -p 'test_usb_*.py' -v` with
`IDF_PATH` pointing to ESP-IDF 5.5.6 for DHCP source tests. Full HTTP access over a physical cable can be checked by opening
`http://172.32.30.1/` on the connected computer.

## Hardware validation (2026-11-13)

The Tab5 booted with USB-C attached, USB-A HS host active, and Wi-Fi/MPD
connected. Its logs confirmed a real DHCP assignment of `172.32.30.2`; receive
and transmit counters advanced with no dropped packets and recovery mode off.
The USB product and interface strings are both **MPD Tab5**.

On-device API checks rejected six invalid address/type combinations without
partially changing other settings, saved/read back a custom address pair,
restored the original defaults, and confirmed the running link stayed on its
boot-time addresses until reboot.

