# Pixoo64 replacement firmware

<p align="center">
  <img src="https://img.shields.io/github/license/simplyRoba/pixoo-firmware?link=https%3A%2F%2Fgithub.com%2FsimplyRoba%2Fpixoo-firmware%2Fblob%2Fmain%2FLICENSE" alt="GitHub License" />
  <img src="https://img.shields.io/github/actions/workflow/status/simplyRoba/pixoo-firmware/firmware-ci.yml?link=https%3A%2F%2Fgithub.com%2FsimplyRoba%2Fpixoo-firmware%2Factions%2Fworkflows%2Ffirmware-ci.yml%3Fquery%3Dbranch%253Amain" alt="GitHub Workflow Status" />
  <a href="https://github.com/simplyRoba/pixoo-firmware/issues"><img src="https://img.shields.io/github/issues/simplyRoba/pixoo-firmware?link=https%3A%2F%2Fgithub.com%2FsimplyRoba%2Fpixoo-firmware%2Fissues" alt="GitHub issues" /></a>
  <img src="https://img.shields.io/github/stars/simplyRoba/pixoo-firmware" alt="GitHub Repo stars" />
</p>

Open, Home Assistant-connected replacement firmware for the ESP32 in a Pixoo64,
with animated dashboards, weather, now-playing artwork, notifications, and
reactions—without the Divoom app or cloud.

It uses ESPHome for networking, provisioning, OTA, and the native API while
retaining the panel MCU that refreshes the LEDs.

<p align="center">
  <img src="docs/images/readme-showcase.png" width="900" alt="Five Pixoo64 firmware displays: daylight weather landscape, Nightfall by Neon Echo now playing, color waveform, analog clock with Door open warning, and celebration reaction over weather.">
</p>
<p align="center"><sub>Actual firmware renders with a simulated Pixoo64 LED grid: weather landscape, now playing, color waveform, analog-clock warning notification, and celebration reaction.</sub></p>

## Compatibility

**Tested/documented target:** a Divoom Pixoo64 with a `Pixoo64-Wifi Mainboard`
REV1.1 (`20210318`), ESP32-WROVER-IE, 8 MB flash, and the `Pixoo64 Wifi
LEDBoard` REV1.0 (`20210525`). Other board revisions and variants are unknown.

## Features

- Wi-Fi captive-portal provisioning, ESPHome native API encryption, and
  password-protected OTA updates
- Home Assistant controls for panel power/brightness, solar brightness scheduling,
  text, timezone, weather location and refresh interval, dashboard selection, and
  sounds
- Home Assistant `media_player.*` now-playing dashboard with cover artwork and
  playback progress
- Text, weather, microphone equalizer, Game of Life, clock, stopwatch, and timer
  dashboards
- Notifications, animated reactions, buzzer sounds, front-button controls, and
  diagnostic entities

## Documentation

- [Installation and operation guide](docs/manual.md)
- [Hardware and panel-protocol reference](docs/hardware.md)
- [Firmware architecture](docs/firmware.md)
- [Contributor setup, builds, tests, and generators](CONTRIBUTING.md)
- [Third-party notices](THIRD_PARTY_LICENSES.md)

## License

Copyright (C) 2026 simplyRoba. Original project material is licensed under the
GNU Affero General Public License, version 3 or later
([`AGPL-3.0-or-later`](https://spdx.org/licenses/AGPL-3.0-or-later.html)); see the
full license text in [LICENSE](LICENSE).
Checked-in OpenMoji artwork and bundled fonts have their own terms; see
[THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md).

> [!NOTE]
> This independent project is not
affiliated with or endorsed by Divoom; the Divoom and Pixoo64 names identify the
supported hardware.

**This project is developed with AI assistance, reviewed by a critical human.**
