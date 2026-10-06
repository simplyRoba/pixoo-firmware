# Installation and operation guide

This guide covers the replacement firmware in this repository. It replaces the
mainboard ESP32 firmware only; it does not reflash the panel MCU. Read the
[hardware reference](hardware.md) before connecting a programmer.

> [!WARNING]
> Installing this firmware is not a beginner flashing procedure. You must be able
> to identify the documented board revision and test pads, distinguish 3.3 V
> logic from 5 V power, verify wiring before applying power, and recover an ESP32
> through ROM download mode. Incorrect wiring, voltage, flash image, offset, or
> handling can permanently damage the mainboard or panel, or leave the device
> unbootable. Stop if any connection, measurement, or instruction is uncertain.

## Requirements and safety

The documented target is a Pixoo64 with the mainboard and panel revisions listed
in the [README](../README.md#compatibility). Compatibility with other revisions is
unknown.

You need:

- a USB-C power supply for the display;
- a 3.3 V USB-UART adapter and jumper wires for the mainboard `TX`, `RX`, `GND`,
  and `IO0` pads;
- a host that can run the repository's documented
  [production build](../CONTRIBUTING.md#production-build).

Disconnect power before attaching wires. Connect a common ground and cross the
serial lines: adapter TX to board RX, adapter RX to board TX. Use **3.3 V logic**;
do not connect a 5 V UART signal to the board. Power the display through USB-C
only—leave the adapter power/VCC disconnected. The USB-C port is power-only, so
it is not a serial connection. Avoid shorts around the test pads and do not
operate the panel from an inadequate power source.

## Backup before any write

A complete full-flash backup created by you from the same unit before changing
its flash is the only restore image this documentation uses. A dump may contain
plaintext credentials; keep it private and do not commit or publish it.

1. Put the ESP32 in ROM download mode: bridge `IO0` to `GND`, cold-power the
   display through USB-C while holding the bridge, then release it.
2. Capture the full flash and its SHA-256 checksum:

   ```bash
   ./tools/esptool-readonly.sh --port /dev/tty.usbserial-0001 --dump --dump-baud 230400
   ```

   The helper checks the security state and detected flash size before dumping.
   Review its output; if it cannot establish that flash encryption and Secure
   Boot are disabled, it refuses the dump. Those states were observed on the
   documented sample, not guaranteed for every unit. **Stop:** If the helper
   reports Secure Boot or flash encryption enabled, this build is unsupported
   and must not be flashed.
3. Verify the checksum file emitted beside the backup:

   ```bash
   shasum -a 256 -c /path/to/full-flash.bin.sha256
   ```

Do not flash until the command reports `OK` and the backup is stored safely.

## First flash

Create the environment and local deployment secrets using the contributor
[setup procedure](../CONTRIBUTING.md#setup), then follow the
[production-build procedure](../CONTRIBUTING.md#production-build). Replace every
example secret before using the image on hardware.

Write the factory image reported by that build at offset `0x0` while the board is
in ROM download mode:

```bash
.venv/bin/python -m esptool --port /dev/tty.usbserial-0001 --baud 230400 \
  write-flash 0x0 <factory-image-from-your-build>.bin
```

Use the factory-image path printed by your successful build. Confirm the image
and port before writing. Keep USB-C power connected throughout the transfer.
After a successful write, disconnect USB-C power, confirm that `IO0` is no longer
connected to `GND`, then reconnect USB-C power for a normal cold boot. Judge boot
success only after this power cycle. If the device still does not boot, return to
ROM download mode and restore only the verified full backup created by you from
that same unit.

## Wi-Fi, Home Assistant, and API credentials

After a successful first boot, the fallback access point is named `Pixoo64 Setup`.
Its password is the `fallback_ap_password` that you set in
`esphome/secrets.yaml`. Join it and use the captive portal to enter Wi-Fi
credentials. ESPHome stores the station credentials in device preferences and can
reopen the fallback portal when it cannot use the saved network.

The native API uses `api_encryption_key`; Home Assistant or another native-API
client needs the matching key. The same key authenticates encrypted OTA;
`ota_password` authenticates legacy plaintext OTA. See [OTA update](#ota-update)
for upgrade behavior. Keep both values private. The sample values in
`secrets.example.yaml` are committed examples and must be replaced.

The configured Home Assistant/native-API surface includes:

- `Pixoo64 Panel` power and brightness; `Solar Brightness`; `Day Brightness`;
  `Night Brightness`; `Pixoo64 Text`; dashboard and timezone selects; location
  and weather-refresh settings; a sound switch; and diagnostic sensors.
- `notify` (message, optional title, severity, duration, optional sound),
  `reaction`, `clear_overlay_queue`, `now_playing_configure` (`entity_id`,
  `home_assistant_url`), `now_playing_clear`, `stopwatch_start`,
  `stopwatch_stop`, `stopwatch_reset`, `timer_set` (`duration_ms`),
  `timer_start`, `timer_stop`, `timer_reset`, and `reboot` API actions. `reboot`
  safely restarts the device
  without clearing persisted preferences. An empty notification title keeps the
  one-line banner; a title adds a line above the message. Notifications
  support `info`, `success`, `warning`, and `error`. Reactions are `laughing`,
  `love`, `crying`, `angry`,
  `poop`, `approve`, `disapprove`, `celebrate`, `thinking`, `surprised`, `fire`,
  and `eyes`.

`now_playing_configure` stores one `media_player.*` entity ID and the Home
Assistant HTTP base URL used for relative artwork references. The URL must be
absolute `http://` or `https://`, with no query, fragment, or embedded
credentials; include any reverse-proxy path prefix. The firmware does not derive
it from the native API connection. A successful configure or clear action safely
reboots the display. Playback metadata uses the encrypted native API; artwork is
fetched only while the now-playing dashboard is visible and a reference is
pending. Supported JPEG and PNG bodies must be no larger than 512 KiB, with
source dimensions no greater than 4096 pixels per axis. Baseline and progressive
JPEG support 8-bit Huffman-coded grayscale, RGB, and YCbCr images with one or
three components and ordinary sampling; arithmetic-coded, lossless, and 12-bit
JPEG are unsupported. Progressive JPEG consumes all scans before producing the
final image, with limits of 64 scans, 3.25 MiB of decode memory, and a 10-second
cooperatively checked decode deadline. Its full-resolution intermediate buffers
can exceed the memory budget well below the dimension limit, even for a small
encoded body. These limits do not guarantee on-device decode latency. Animated
or interlaced PNG and PNG bodies with an alpha channel or `tRNS` transparency
are rejected. Missing, unsupported, and failed artwork use a deterministic
64×64 fallback, including when entering the dashboard.

## Controls and features

`Solar Brightness` is an opt-in schedule, not a lux sensor. Using SNTP and the
saved location, it blends from Night Brightness (20% by default) at -6° to Day
Brightness (100% by default) at +6° solar elevation. Both accept 5–100%.
Missing time or location keeps the current brightness; it never changes power.

The power button toggles the panel when released after 50–2,000 ms. The
brightness button cycles 25%, 50%, 75%, and 100% after 50–1,999 ms; manual
adjustment disables Solar Brightness. Releasing it after **2–10 seconds** toggles
Solar Brightness without changing power. Other hold lengths do nothing.

A power-button hold released after **10–60 seconds**, inclusive, resets ESPHome
preferences and safely reboots. Holds shorter than 50 ms, from 2,001–9,999 ms, or
longer than 60 seconds have no defined firmware action. The reset occurs on
release. Its exact storage scope is an ESPHome implementation detail; verify the
resulting provisioning and settings state after reset.

Available dashboards are text, now-playing, forecast weather, landscape weather,
equalizer bars, equalizer waveform, Game of Life, split-flap clock, analog clock,
binary clock, digital clock, stopwatch, timer, and DDP. Weather needs configured
location and network access; equalizer views use the panel microphone.
Notifications, reactions, and sound are exposed through the native API.

Select `ddp` to receive RGB images over IPv4 UDP port 4048. Map 4,096 pixels in
row-major order from the top-left corner, with one byte each for red, green, and
blue. The receiver accepts DDP version 1 ordinary writes to destination ID 1,
RGB24 type `0x0B` or legacy LedFX type `0x01`, and at most 1,440 payload bytes per
packet. PUSH publishes the accumulated image; a zero-length PUSH is supported.
Timecodes, queries, replies, storage commands, and other types or destinations
are rejected.

Reception starts when the dashboard becomes visible and stops when it is hidden,
including display-off and firmware-update presentation. Notifications remain
over the live image; reactions freeze the displayed background while reception
continues. Entry starts black, and the last published image remains when the
sender stops. Hiding clears both the displayed image and unpublished writes.
Partial updates retain other pixels; lost or reordered packets can leave stale
regions, with no retransmission or sequence-based recovery. On-device throughput
and LedFX interoperability have not been verified.

## OTA update

OTA updates require the development environment and real
`esphome/secrets.yaml` described in [CONTRIBUTING.md](../CONTRIBUTING.md#setup).
The computer and display must be able to reach each other over the network. Keep
the display on stable USB-C power for the complete update.

From the repository root, build, upload over the network, and follow the rebooted
device log with:

```bash
.venv/bin/esphome run esphome/pixoo64.yaml --device pixoo64.local
```

With ESPHome 2026.9.1, the configured native-API key automatically offers OTA
encryption once that firmware is running. The CLI prefers encryption using
`api_encryption_key` when the target supports it. The YAML retains
`password: !secret ota_password`, so legacy password-authenticated plaintext
uploads remain accepted; encryption is not required. An upgrade from ESPHome
2026.7 firmware needs the existing `ota_password` for the first upload, which
is plaintext. Keep the existing key and password unchanged for that upload.
Encrypted OTA behavior has not been verified on this hardware. See the
[ESPHome OTA reference](https://esphome.io/components/ota/esphome/#encryption)
for protocol details.

If mDNS does not resolve `pixoo64.local`, replace it with the device's IPv4
address. Confirm that the
upload reaches 100%, the device reboots, and logs reconnect before treating the
update as complete. The display renders a firmware-update message when the OTA
writer starts. Do not interrupt power during the upload.

Use the generated OTA application image only through ESPHome's network upload;
do not write it at UART offset `0x0`. A failed update that does not return to the
network may require the restore procedure below.

## Restore

Enter ROM download mode and write only the complete,
checksum-verified backup created by you from that same unit:

```bash
.venv/bin/python -m esptool --port /dev/tty.usbserial-0001 --baud 230400 \
  write-flash 0x0 <your-full-flash-backup>.bin
```

Do not substitute an image from this repository or another device for that
backup. This repository supplies no stock firmware. After the write succeeds,
disconnect USB-C power, confirm that `IO0` is no longer connected to `GND`, and
reconnect USB-C power to boot the restored image.

## Logs

For logs over the network or an attached 3.3 V UART adapter:

```bash
.venv/bin/esphome logs esphome/pixoo64.yaml --device pixoo64.local
.venv/bin/esphome logs esphome/pixoo64.yaml --device /dev/tty.usbserial-0001
```

The configured serial logger uses 115200 baud. The USB-C connector itself does
not provide serial data.

While DDP is visible, the `pixoo64.ddp` log tag reports five-second windows and a
final partial window when hidden:

- `received` and `rejected` count datagrams, including empty or malformed input.
- `publications` counts accepted PUSH commands, not complete-frame coverage.
  `rendered_revisions` counts distinct latest images drawn, not panel FPS;
  reactions can pause drawing while publications continue. `latest_revision` is
  the publication counter for the current visible session.
- `receive_avg_us` and `receive_max_us` measure receive passes, including empty
  reads. `loop_gap_max_us` is the longest interval between active receiver-loop
  entries, including other work and idle time. `socket_errors` counts listener
  setup and receive failures.
- `internal_free_bytes` and `psram_free_bytes` sample available memory at report
  time on ESP32.

The existing render and end-to-end frame sensors publish five-second windows.
For controlled streams and sender pause/overload tests, use the
[contributor test sender](../CONTRIBUTING.md#tools). None of these counters alone
establishes packet loss or sustainable on-device throughput.

## Troubleshooting

- **No serial response or flash connection:** confirm ROM download mode, common
  ground, crossed TX/RX wiring, a 3.3 V adapter, and USB-C power. Do not connect
  adapter VCC.
- **Configuration/build fails:** follow the clean-environment and production-build
  steps in [CONTRIBUTING.md](../CONTRIBUTING.md).
- **No Wi-Fi connection:** join `Pixoo64 Setup` with your configured fallback
  password and submit new station credentials through the captive portal.
- **Resets under load:** use a suitable USB-C power supply and check the panel
  power connections. The production configuration limits Wi-Fi transmit power
  because full power caused brownouts on the documented mainboard.
- **Weather is unavailable:** check location, Wi-Fi, and the external weather
  service; the weather dashboard fetches only when it is visible and its data is
  stale.
- **Solar Brightness does not change:** verify SNTP time has synchronized and
  Latitude and Longitude are valid. This is a solar schedule, not a response to
  room lighting.
- **Now-playing shows unconfigured:** invoke `now_playing_configure` with a valid
  `media_player.*` entity and the Home Assistant HTTP base URL to use for relative
  artwork references.
- **Metadata appears without cover artwork:** check the resolved artwork URL from
  the display's network. It must return HTTP 200 without a redirect. The response
  must satisfy the [artwork format and resource limits](#wi-fi-home-assistant-and-api-credentials).
  Progressive JPEG can fail the decode-memory or work limits even when its
  encoded body is small; re-encode or resize the image if necessary.

## Privacy and limitations

The configuration contains no Divoom cloud client, MQTT client, or web server.
DDP input is unauthenticated and unencrypted; any sender that can reach UDP port
4048 can supply pixels while the DDP dashboard is visible. Use only on a trusted
network and do not expose this port to the Internet. DDP does not grant control
over dashboard selection, panel power, brightness, or native-API actions.

Weather requests go to `https://api.open-meteo.com/v1/forecast`
and include latitude and longitude rounded to four decimal places plus weather
query fields. The request sends no credentials.

Solar Brightness uses local SNTP time and the persisted location only; it sends
no solar, ambient-light, or Home Assistant `sun` requests.

Now-playing metadata uses the encrypted native API. The configured entity ID and
Home Assistant base URL persist in device preferences. Artwork requests use the
relative or absolute URL supplied by Home Assistant; signed query values are not
written to logs. The `http_request` logger tag is disabled because upstream
transport errors can log complete URLs; adapter diagnostics omit queries.
The shared HTTP client follows no redirects, and TLS certificate
verification is disabled for weather and artwork. SNTP is enabled; its server is
not specified here.

Current limitations include no SD-card reading, no Home Assistant raw-frame API,
no Divoom app/cloud compatibility, no panel-MCU reflashing, disabled HTTP certificate
verification, and no full-operation HTTP cancellation. Hardware compatibility
beyond the documented target is unknown.
