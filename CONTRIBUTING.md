# Contributing

## Setup

The documented commands use a POSIX shell and are supported on macOS and Ubuntu.
Windows is untested. Use Python 3.12 to match CI. Native tests and reaction-art
generation also require a C++ compiler and Cairo:

- macOS: install the Xcode command-line tools and `brew install cairo`;
- Ubuntu: install `build-essential`, `libcairo2-dev`, `pkg-config`, and the Python
  venv package for the selected interpreter.

Create the development environment from the repository root:

```bash
python3 -m venv .venv
.venv/bin/pip install -r requirements.txt
cp esphome/secrets.example.yaml esphome/secrets.yaml
```

`requirements.txt` pins the project's direct Python tools; their transitive
packages and host libraries are resolved for the current platform. CI uses the
smaller `ci-requirements.txt` subset needed by automated checks. Recreate `.venv`
after moving or renaming the checkout because its scripts contain absolute paths.

`esphome/secrets.yaml` is local and ignored by Git. Replace every example value
before using a build on hardware. Tests may use the committed example values.

## Production build

Validate and compile the deployable ESP32 configuration:

```bash
.venv/bin/esphome config esphome/pixoo64.yaml
.venv/bin/esphome compile esphome/pixoo64.yaml
```

ESPHome prints the factory-image path after a successful compile. Installation,
backup, and recovery procedures are owned by the [manual](docs/manual.md); do not
infer flash commands or offsets from build output alone.

## Required checks

```bash
.venv/bin/pio test -d pixoo_protocol -e native
.venv/bin/pio test -d pixoo_app -e native
.venv/bin/pio test -d pixoo_content -e native
.venv/bin/python -m unittest discover -s tools/tests
.venv/bin/python -m unittest discover -s esphome/tests/config_validation
.venv/bin/python tools/gen-reaction-art.py --check
.venv/bin/python tools/gen-split-flap-digits.py --check
.venv/bin/esphome compile esphome/pixoo64.yaml
.venv/bin/esphome compile esphome/tests/render_test/render_test.yaml
esphome/tests/render_test/.esphome/build/pixoo64-render-test/.pioenvs/pixoo64-render-test/program
```

The native suites test the framework-independent protocol, application-policy,
and content layers. Configuration tests validate the production composition and
expected failures for invalid wiring or schema combinations. The host render
target exercises the real renderer, fonts, deterministic animation states, and
now-playing adapter and image-decoder fixtures.

## Generated sources

`tools/gen-reaction-art.py` owns
`esphome/components/pixoo64_content/reaction/reaction_art.h`; its exact OpenMoji
inputs and license are under `resources/openmoji-17.0.0/`.
`tools/gen-split-flap-digits.py` owns
`esphome/components/pixoo64_content/dashboard/clock/split_flap_digits.h`.

Run a generator without `--check` only when intentionally changing its source
inputs or algorithm. Commit the generator, its public inputs, and generated output
together.

## Tools

Generators owning checked-in sources:

- `tools/gen-reaction-art.py` — rasterize the OpenMoji SVG sources into the
  palette-compressed reaction artwork.
- `tools/gen-split-flap-digits.py` — rasterize the split-flap digit strokes into
  the anti-aliased coverage table.

Render-test review:

- `tools/render-test-view.sh` — rebuild the render-test frames and assemble the
  review composites.
- `tools/render-test-contact-sheet.py` — assemble rendered frames into one sheet.
- `tools/render-test-icon-gallery.py` — assemble the weather-icon states.
- `tools/render-weather-gif.py` — animate a weather dashboard state.

Device and capture work:

- `tools/esptool-readonly.sh` — ESP32 ROM-mode chip ID, security state, flash ID,
  and full-flash dump; refuses to dump unless security parses as disabled.
- `tools/uart-capture.py` — passive UART boot-log capture.
- `tools/notify-pixoo.py` — call the device `notify`, `reaction`, and
  `clear_overlay_queue` API actions over the network.
- `tools/decode-panel-spi.py` — decode a logic-analyzer capture into panel
  protocol frames; optionally dump a full-frame RGB payload.
- `tools/read-sr-capture.py` — report the structure of a sigrok/PulseView `.sr`
  capture.
- `tools/render-panel-frame.py` — render a 12288-byte RGB payload to a PNG.

## Render snapshots

The host render binary byte-compares its output with PNG references under
`esphome/tests/render_test/frames/`. Run
`tools/render-test-view.sh --update` only for an intentional visual change, then
review every changed frame. Do not accept platform-specific references for the
same renderer state.

## Hardware evidence

Scope hardware claims to the board revision and method that establish them; mark
other revisions as unknown. Keep credentials, flash backups, stock firmware,
disassembly, raw device captures, and extracted device data out of this
repository.

For panel-link measurements, the SPI clock is 15 MHz, so a logic analyzer must
sample well above it; approximately 50 MHz or more is required for reliable SPI
decoding. A 24 MHz capture aliases the clock. Establish continuity with the board
unpowered. Follow the [manual's safety rules](docs/manual.md#requirements-and-safety)
before connecting any instrument.

Public tools may decode or render locally held captures, but proprietary or
credential-bearing source artifacts must not be committed.

## Repository rules

- Preserve the dependency and ownership rules in the
  [firmware architecture](docs/firmware.md).
- Keep build, test, and generator inputs required by public contributors checked
  in under redistributable terms.
- Keep documentation topics in their owning file:
  - `README.md` — overview, compatibility, features, and navigation;
  - `docs/manual.md` — operator safety, installation, use, update, recovery, and
    troubleshooting;
  - `docs/hardware.md` — physical, electrical, and panel-protocol facts;
  - `docs/firmware.md` — software architecture and implementation design;
  - `CONTRIBUTING.md` — development setup, commands, tests, and contribution
    workflow.
- Link to an owning section instead of duplicating its instructions or facts.
- Keep maintained documentation factual; omit edit history, plans, and process
  narration.
- Preserve third-party attribution and license boundaries.
