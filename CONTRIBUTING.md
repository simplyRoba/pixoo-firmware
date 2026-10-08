# Contributing

## Setup

The documented commands use a POSIX shell and are supported on macOS and Ubuntu.
Windows is untested. Use Python 3.12 to match CI. Native tests and reaction-art
generation require a C++ compiler and Cairo. The host render target also requires
a C compiler, Make, and CMake >= 3.15 on `PATH`:

- macOS: install the Xcode command-line tools and `brew install cairo cmake`;
- Ubuntu: install `build-essential`, `cmake`, `libcairo2-dev`, `pkg-config`, and
  the Python venv package for the selected interpreter.

Create the development environment from the repository root:

```bash
python3 -m venv .venv
.venv/bin/pip install -r requirements.txt
cp esphome/secrets.example.yaml esphome/secrets.yaml
```

`requirements.txt` pins the project's direct Python tools. CI tests both that
full environment and the smaller `ci-requirements.txt` subset. Keep shared pins
consistent between the files and with ESPHome's exact dependency constraints.
Other transitive packages and host libraries are resolved for the current
platform. Recreate `.venv` after moving or renaming the checkout because its
scripts contain absolute paths.

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
.venv/bin/python tools/gen-showcase-cover.py --check
.venv/bin/esphome compile esphome/pixoo64.yaml
.venv/bin/esphome compile esphome/tests/render_test/render_test.yaml
esphome/tests/render_test/.esphome/build/pixoo64-render-test/.pioenvs/pixoo64-render-test/program
.venv/bin/python tools/render-readme-showcase.py --check
```

The native suites test the framework-independent protocol, application-policy,
and content layers. Configuration tests validate the production composition and
expected failures for invalid wiring or schema combinations. The host render
target exercises the real renderer, fonts, deterministic animation states, and
now-playing adapter and image-decoder fixtures, including full progressive JPEG
output and resource-limit failures. Host results and ESP32 compilation do not
establish on-device progressive decode latency or memory availability; those
require hardware verification.

The host render target runs `tools/build-libjpeg-host.py` as a PlatformIO
prebuild hook. It downloads the pinned libjpeg-turbo 3.2.0 release archive,
verifies its SHA-256, and uses native CMake with `Unix Makefiles` to build the
static libjpeg API library with `WITH_JPEG8=ON` and SIMD disabled. TurboJPEG,
command-line tools, and upstream tests are disabled. The archive, extracted
source, build, and installation are cached under
`esphome/tests/render_test/.esphome/build/pixoo64-render-test/.pioenvs/pixoo64-render-test/libjpeg-turbo/`;
the first build requires network access. This generated tree is not a checked-in
source input. Upstream license notices retained for distribution are documented
in [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md#libjpeg-turbo-320).

## Generated sources

`tools/gen-reaction-art.py` owns
`esphome/components/pixoo64_content/reaction/reaction_art.h`; its exact OpenMoji
inputs and license are under `resources/openmoji-17.0.0/`.
`tools/gen-split-flap-digits.py` owns
`esphome/components/pixoo64_content/dashboard/clock/split_flap_digits.h`.
`tools/gen-showcase-cover.py` owns
`esphome/tests/render_test/components/pixoo64_render_test/showcase_cover.h`.
`tools/render-readme-showcase.py` owns `docs/images/readme-showcase.png` from
its explicit ordered render-snapshot inputs.

Run a generator without `--check` only when intentionally changing its source
inputs or algorithm. Commit the generator, its public inputs, and generated output
together.

## Tools

Generators owning checked-in sources:

- `tools/gen-reaction-art.py` — rasterize the OpenMoji SVG sources into the
  palette-compressed reaction artwork.
- `tools/gen-split-flap-digits.py` — rasterize the split-flap digit strokes into
  the anti-aliased coverage table.
- `tools/gen-showcase-cover.py` — render the original high-resolution cover art
  and downsample it into the RGB565 now-playing fixture.

Render-test review:

- `tools/render-test-view.sh` — rebuild the render-test frames and assemble the
  review composites.
- `tools/render-readme-showcase.py` — compose the tracked README image from its
  five explicit 64×64 render snapshots; use `--check` to verify decoded pixels.
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

## Releases

Releases contain source archives only. Users compile firmware with their own
secrets; compiled firmware images are not published. Installation procedures are
in the [manual](docs/manual.md).

Use Conventional Commits for commit subjects and PR titles. Squash-merge PRs with
the same Conventional Commit title. `feat:` bumps the minor version;
`fix:`, `perf:`, `refactor:`, `docs:`, and `deps:` bump the patch version.
Use `!` or a `BREAKING CHANGE:` footer for a major bump. `chore:`, `test:`,
`ci:`, `build:`, and `style:` are excluded from release notes and do not trigger
releases on their own unless they contain a breaking change.

Release Please maintains one release PR on `main`. That PR updates `version.txt`,
`CHANGELOG.md`, `.release-please-manifest.json`, and the annotated
`esphome.project.version` in `esphome/pixoo64.yaml`. Merge it after CI passes to
create the `v<version>` tag and GitHub release. Project metadata identifies the
firmware release in ESPHome logs and the native API; it does not change the
independently pinned ESPHome or Arduino versions.

The shared release workflow requires a GitHub App installed for this repository
with Contents and Pull requests write permissions. Configure repository Actions
secrets `RELEASE_BOT_CLIENT_ID` and `RELEASE_BOT_PRIVATE_KEY`. App-created release
PRs trigger CI. Dependabot groups the monthly Python toolchain updates with
`deps:` subjects; GitHub Actions updates use `ci:`.

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
