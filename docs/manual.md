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
client needs the matching key. OTA authentication uses the separate
`ota_password`. Keep both values private. The sample values in
`secrets.example.yaml` are committed examples and must be replaced.

The configured Home Assistant/native-API surface includes:

- `Pixoo64 Panel` power and brightness; `Pixoo64 Text`; dashboard and timezone
  selects; location and weather-refresh settings; a sound switch; and diagnostic
  sensors.
- `notify` (message, optional title, severity, duration, optional sound),
  `reaction`, `clear_overlay_queue`, `stopwatch_start`, `stopwatch_stop`,
  `stopwatch_reset`, `timer_set` (`duration_ms`), `timer_start`, `timer_stop`,
  and `timer_reset` API actions. An empty notification title
  keeps the one-line banner; a title adds a line above the message. Notifications
  support `info`, `success`, `warning`, and `error`. Reactions are `laughing`,
  `love`, `crying`, `angry`,
  `poop`, `approve`, `disapprove`, `celebrate`, `thinking`, `surprised`, `fire`,
  and `eyes`.

## Controls and features

The power and brightness buttons act on release after a 50–2,000 ms press:

- Power toggles the panel.
- Brightness moves through 25%, 50%, 75%, and 100%, reversing at each end.

A power-button hold released after **10–60 seconds**, inclusive, resets ESPHome
preferences and safely reboots. Holds shorter than 50 ms, from 2,001–9,999 ms, or
longer than 60 seconds have no defined firmware action. The reset occurs on
release. Its exact storage scope is an ESPHome implementation detail; verify the
resulting provisioning and settings state after reset.

Available dashboards are text, forecast weather, landscape weather, equalizer
bars, equalizer waveform, Game of Life, split-flap clock, analog clock, binary
clock, stopwatch, and timer. Weather needs configured location and network
access; equalizer views use the panel microphone. Notifications, reactions, and
sound are exposed through the native API.

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

The command uses the `ota_password` from local secrets. If mDNS does not resolve
`pixoo64.local`, replace it with the device's IPv4 address. Confirm that the
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

## Privacy and limitations

The configuration contains no Divoom cloud client, MQTT client, web server, or
raw-frame API. Weather requests go to `https://api.open-meteo.com/v1/forecast`
and include latitude and longitude rounded to four decimal places plus weather
query fields. The request sends no credentials, but TLS certificate verification
is disabled in the current configuration. SNTP is enabled; its server is not
specified here.

Current limitations include no SD-card reading, no raw RGB streaming API, no
Divoom app/cloud compatibility, no panel-MCU reflashing, disabled certificate
verification for weather, and no full-operation HTTP cancellation. Hardware
compatibility beyond the documented target is unknown.
