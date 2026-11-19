# Changelog

## [3.0.1](https://github.com/simplyRoba/pixoo-firmware/compare/v3.0.0...v3.0.1) (2026-11-19)


### Bug Fixes

* let the analog second hand finish its wind after the geared hands ([0a36413](https://github.com/simplyRoba/pixoo-firmware/commit/0a364133804033e62298dd9e85caf331cd16af7e))


### Performance Improvements

* build only the GigaDevice flash driver ([31a04e4](https://github.com/simplyRoba/pixoo-firmware/commit/31a04e43d09e1b96fe2accd06f73b48179a87733))
* take microphone captures without a per-chunk copy ([b79ae52](https://github.com/simplyRoba/pixoo-firmware/commit/b79ae52ad3c0680cb3583db82c25ed9750815ca8))


### Code Refactoring

* drop microphone stop workaround ([db92c4c](https://github.com/simplyRoba/pixoo-firmware/commit/db92c4c63dcffe8e9ef639f7c9ec13d0af620d49))


### Dependencies

* bump esphome to 2026.11.0 ([73f55bb](https://github.com/simplyRoba/pixoo-firmware/commit/73f55bbe301e72d322eea1103468e90cfc07e2eb))

## [3.0.0](https://github.com/simplyRoba/pixoo-firmware/compare/v2.1.0...v3.0.0) (2026-11-16)


### ⚠ BREAKING CHANGES

* Remove pixoo64_content.render_metrics and replace the legacy drawing and end-to-end sensor set with frame preparation, rendered and sent FPS, late-frame, and busy-drop metrics. Update monitoring configuration and Home Assistant entity references accordingly.
* per-kind pause, resume, stop, minimize, and show actions and buttons are replaced by timing_pause, timing_resume, timing_stop, timing_minimize, and timing_show. The timing configuration requires the shared controls and a visible binary sensor.

### Features

* consolidate timing controls and publish visibility ([ceaec4a](https://github.com/simplyRoba/pixoo-firmware/commit/ceaec4ad5b339cf19ccecbc179589e97f2cde9e2))
* replace legacy monitoring with async frame metrics ([0dd072d](https://github.com/simplyRoba/pixoo-firmware/commit/0dd072d68b6e2b84175c5ece99461acc8169040b))
* run rendering and queued panel DMA on a dedicated core ([e23e970](https://github.com/simplyRoba/pixoo-firmware/commit/e23e9700cb5680edb9a649d9da59f009d65eb296))
* show timer and stopwatch banners by default ([6da5e1e](https://github.com/simplyRoba/pixoo-firmware/commit/6da5e1e58093daf13ee65c76e9fed4bff330f2c8))


### Bug Fixes

* build host JPEG decoder with the Ninja toolchain ([3e54a07](https://github.com/simplyRoba/pixoo-firmware/commit/3e54a070db9bd37e3a5a1d87d4aeafa32f6bdcf1))
* recover now-playing readiness after Home Assistant startup ([a9857e2](https://github.com/simplyRoba/pixoo-firmware/commit/a9857e2cef6801e4d2a13dead40de5cc2c4f0f9b))


### Performance Improvements

* avoid copying DDP frames on the render task ([fdb6020](https://github.com/simplyRoba/pixoo-firmware/commit/fdb60202e98e8f7e2daaf84c94f2b3e856ecf069))
* limit shadow clock rendering to 25 fps ([9d4aef9](https://github.com/simplyRoba/pixoo-firmware/commit/9d4aef9524cd288aef102c6e3881316107b0f6b2))


### Code Refactoring

* isolate rendering state and snapshot external inputs ([8566833](https://github.com/simplyRoba/pixoo-firmware/commit/85668331ac3e5bb0a528d30d29a0c0094d6e0cb0))
* prepare application ports for queued rendering ([f2fa96f](https://github.com/simplyRoba/pixoo-firmware/commit/f2fa96f06b754a49a3dcebdfc38308fd994b85ba))


### Documentation

* align architecture with asynchronous rendering ([5125741](https://github.com/simplyRoba/pixoo-firmware/commit/51257415e900db162668ae3a685cdd3cec05c36c))


### Dependencies

* bump esphome from 2026.9.1 to 2026.10.2 ([7f3f9a0](https://github.com/simplyRoba/pixoo-firmware/commit/7f3f9a0a71050db6abfeebf89c4fa0a16101cb70))

## [2.1.0](https://github.com/simplyRoba/pixoo-firmware/compare/v2.0.0...v2.1.0) (2026-10-23)


### Features

* add flickering shadow clock ignition ([9aa117f](https://github.com/simplyRoba/pixoo-firmware/commit/9aa117feb0881b4e20466b246a16415e3c621d6c))
* add timer and stopwatch minimize and show actions ([37ad50e](https://github.com/simplyRoba/pixoo-firmware/commit/37ad50e32197bb7324f1027790d7f8edfbc05e7a))
* add translucent notification backdrops ([c8a7b02](https://github.com/simplyRoba/pixoo-firmware/commit/c8a7b02e7db748230afe899805d39bee98890762))


### Bug Fixes

* smooth now-playing text background ([7c285ad](https://github.com/simplyRoba/pixoo-firmware/commit/7c285adef042812aa888799fceedc6f8b2a22880))


### Performance Improvements

* reduce shadow clock shading and transfer overhead ([d6c5fa2](https://github.com/simplyRoba/pixoo-firmware/commit/d6c5fa279a66c610cf50072bca147333835fc155))
* reduce translucent notification compositing work ([47327bd](https://github.com/simplyRoba/pixoo-firmware/commit/47327bdd76f9d53ea0f72a4fb37c33680ca1f965))
* skip redundant shadow clock blocker calculations ([9dcfce0](https://github.com/simplyRoba/pixoo-firmware/commit/9dcfce05a72fa204c47ea788cff6f9782949b195))


### Code Refactoring

* test now-playing presentation without rendering ([d3a874a](https://github.com/simplyRoba/pixoo-firmware/commit/d3a874a66f96e5e527aa1d072284d8b947e52aab))

## [2.0.0](https://github.com/simplyRoba/pixoo-firmware/compare/v1.2.0...v2.0.0) (2026-10-20)


### ⚠ BREAKING CHANGES

* Stopwatch and timer are no longer dashboard selections. timer_start requires duration_seconds; stop closes the timing view instead of pausing. timer_set, timer_reset, and stopwatch_reset are removed.

### Features

* add light-blocking shadow clock markers ([3d4a072](https://github.com/simplyRoba/pixoo-firmware/commit/3d4a0722ba268e52be6a5f3276aaca43c10d0c5d))
* add shadow clock dashboard ([ff044ee](https://github.com/simplyRoba/pixoo-firmware/commit/ff044ee977e8b53f73beeb0e20fac3ec5019281f))
* replace timing dashboards with transient sessions ([c91160b](https://github.com/simplyRoba/pixoo-firmware/commit/c91160bc92d9a6eecdb2d1bf01d0a00340579642))


### Bug Fixes

* align shadow clock hands at a shared centre ([47d6c0a](https://github.com/simplyRoba/pixoo-firmware/commit/47d6c0a0ec7e9b0af61e83d4df5b82e8ecd31d4a))
* extend digital clock fade around the full perimeter ([07a85d7](https://github.com/simplyRoba/pixoo-firmware/commit/07a85d765db95c2be2c0849a25d8f48f8b595190))
* group dashboard dropdown options ([1046701](https://github.com/simplyRoba/pixoo-firmware/commit/10467015fb37f46ddcb5cc1b96cd7bf2ba34f4d9))
* release shadow renderer resources while inactive ([c81ba7a](https://github.com/simplyRoba/pixoo-firmware/commit/c81ba7a2da10409646cd3d0eee05d6edf267423b))
* soften shadow clock seconds occlusion ([9b0b7d7](https://github.com/simplyRoba/pixoo-firmware/commit/9b0b7d723eabeb2eadcc46247a0f1863f2475002))


### Performance Improvements

* bound shadow renderer instruction RAM use ([a6fcf64](https://github.com/simplyRoba/pixoo-firmware/commit/a6fcf642aebffc9badca9b7b28bcc510c854655c))
* reduce shadow renderer internal RAM use ([f84abe2](https://github.com/simplyRoba/pixoo-firmware/commit/f84abe2593f5a85031e83f558ac8c2e3d8643821))


### Dependencies

* update Arduino-ESP32 to 3.3.11 ([c0a0fdf](https://github.com/simplyRoba/pixoo-firmware/commit/c0a0fdfdba99b5ce68ad3925ae402bf3332f7173))

## [1.2.0](https://github.com/simplyRoba/pixoo-firmware/compare/v1.1.0...v1.2.0) (2026-10-17)


### Features

* redesign digital clock with perimeter wake ([ea669dd](https://github.com/simplyRoba/pixoo-firmware/commit/ea669dd07cf6abee11d1326a8614339c8eee9d51))

## [1.1.0](https://github.com/simplyRoba/pixoo-firmware/compare/v1.0.0...v1.1.0) (2026-10-08)


### Features

* add experimental DDP dashboard ([4d806c9](https://github.com/simplyRoba/pixoo-firmware/commit/4d806c9070e554592264393e1d52f962fadf4d97))

## 1.0.0 (2026-10-08)


### Features

- Four clock faces: split-flap, analog, binary, and animated digital.
- Open-Meteo weather forecast and animated landscape dashboards, including day/night scenery and moon phases.
- Home Assistant now-playing dashboard with title, artist, cover artwork, playback progress, scrolling text, and animated transitions.
- Baseline/progressive JPEG and static PNG artwork, with fallback artwork when images are missing or fail.
- Text dashboard with wrapping, paging, and scrolling.
- Microphone-driven equalizer bars and waveform.
- Animated Game of Life.
- Stopwatch and countdown timer with start, stop/resume, reset, and completion alarm.
- Location-based solar brightness scheduling with configurable day/night levels.
- Queued notifications with optional titles, severity, duration, and buzzer sounds.
- Twelve animated reactions.
- Temporary panel wake for notifications and reactions, restoring the previous state afterward.
- Home Assistant controls for power, brightness, dashboard, text, timezone, weather location, refresh interval, and sounds.
- Network-synchronized time and 44 selectable timezones.
- Front-button power, brightness, solar scheduling, and preference-reset controls.
- Wi-Fi captive-portal setup, encrypted ESPHome native API, and password-protected OTA updates.
- Firmware-update display and reboot API action.
- Wi-Fi, memory, boot/reset, and rendering diagnostics.
- Firmware release-version metadata in logs and the native API.
