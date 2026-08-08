# Firmware architecture

This document describes the structure, runtime flow, ownership rules, and extension
points of the replacement firmware. Feature-specific layouts, colors, animation
constants, and tuning values remain defined by the code that implements each
feature.

## 1. Scope

The firmware is a modular monolith built around a framework-independent C++
application core. ESPHome supplies provisioning, Wi-Fi, the native API, OTA,
entities, fonts, time, logging, and code generation; project code owns device
policy, rendering, and the panel adapter.

The firmware replaces the ESP32 application. It does not replace the panel MCU
firmware, implement the Divoom app or cloud protocols, or provide a generic board
abstraction. Physical interfaces, pin assignments, electrical constraints, and
the panel wire protocol are documented in [hardware.md](hardware.md).

## 2. Architecture

### 2.1 Runtime shape

ESPHome YAML is the compile-time composition root. Its schemas validate the
configuration, code generation constructs application-lifetime objects, and typed
references connect those objects before startup. There is no runtime dependency
injection container, service locator, or plugin loader.

`pixoo::FirmwareApp` is the single owner of lifecycle, user-intent, and overlay
policy state. Renderer buffers, dashboard animation, weather snapshots, and DSP
state remain with their respective components. `FirmwareApp` decides:

- whether the display is off, waiting for panel initialization, showing the boot
  presentation, or running;
- when the selected dashboard and any overlay must render;
- how power, brightness, buttons, dashboard selection, notifications, reactions,
  sounds, and factory reset interact;
- when microphone capture is required; and
- when a rendered frame is presented to the panel.

`FirmwareApp` has no ESPHome, ESP-IDF, FreeRTOS, HTTP, or GPIO types in its public
interface. `FirmwareAppComponent` is the ESPHome adapter around it: it converts
entity callbacks and API actions into typed application commands and calls
`FirmwareApp::Tick(millis())` from a 5 ms ESPHome polling interval.

```text
ESPHome entities, API actions, OTA callbacks, GPIO button callbacks
        │
        ▼
FirmwareAppComponent
        │ typed commands and lifecycle ticks
        ▼
FirmwareApp
        ├── PanelPort ───────► Pixoo64Panel ─────► panel transport and power output
        ├── RenderPort ──────► ContentController ─► dashboards and overlays
        │                                           └► WeatherSource adapter
        ├── SoundPlayer ─────► RTTTL adapter
        ├── MicrophonePort ──► I2S microphone adapter and equalizer DSP
        ├── LightStateSink ──► ESPHome light entity
        ├── SystemPort ──────► preference reset and safe reboot
        └── FrameMetricsPort ► ESPHome diagnostic sensors
```

Calls enter through the component and leave through ports:

```text
entities/actions/buttons → FirmwareApp → ports → adapters
```

Adapters do not call sibling adapters to implement product policy. An adapter may
use framework collaborators internally—for example, the weather adapter uses
ESPHome's HTTP client—but coordination between panel, renderer, microphone,
sound, and light state remains in `FirmwareApp`.

### 2.2 Ports and adapters

Coarse interfaces are used at boundaries that have a hardware, framework, or
test substitute. Most are consumed by `FirmwareApp`; `WeatherSource` is a
separate content-layer boundary consumed by the weather renderer.

| Port | Consumer | Production adapter | Responsibility |
|---|---|---|---|
| `PanelPort` | `FirmwareApp` | `Pixoo64Panel` | Panel power, initialization, brightness, synchronous frame presentation, and panel transport resources. |
| `RenderPort` | `FirmwareApp` | `ContentController` | Dashboard lookup, boot/update frames, dashboard rendering, notification composition, and reaction composition. |
| `SoundPlayer` | `FirmwareApp` | `RtttlSoundPlayer` | Play or stop a sound from the closed `pixoo::Sound` vocabulary. |
| `MicrophonePort` | `FirmwareApp` | `MicrophoneAdapter` | Enable capture, process microphone samples, and publish normalized equalizer levels. |
| `LightStateSink` | `FirmwareApp` | `FirmwareAppComponent` | Reflect application-owned light state in the ESPHome light entity. |
| `SystemPort` | `FirmwareApp` | `FirmwareAppComponent` | Clear persisted preferences and request a safe reboot. |
| `FrameMetricsPort` | `FirmwareApp` | `FirmwareAppComponent` | Measure complete regular-frame work without putting a platform clock in the application core. |
| `WeatherSource` | weather rendering layer | `OpenMeteoSource` | Non-blocking weather refresh and coherent weather snapshots. |

`ContentController` implements both `RenderPort` and
`EqualizerLevelsSink`. The microphone adapter publishes levels to the controller,
which forwards them to every configured equalizer dashboard. The microphone is
therefore bound to the renderer catalog rather than to one selected face.

`FirmwareAppComponent` deliberately receives ESPHome light and select entities as
concrete inbound dependencies. Toward the application core it implements only the
small output ports shown above.

### 2.3 Ownership and lifetime

- ESPHome code generation owns the concrete components for the lifetime of the
  device application.
- `FirmwareAppComponent` constructs one `FirmwareApp` after its required adapters
  are available.
- `FirmwareApp` owns application state and the bounded 16-entry overlay FIFO.
- `ContentController` owns the dashboard catalog and reusable framebuffer storage.
- Dashboard pointers and injected port pointers are non-owning and remain fixed
  after setup.
- `FrameView` is a borrowed view into renderer-owned storage. `PanelPort::Present`
  consumes it synchronously and must not retain it.
- Hardware adapters own their peripheral handles, retained transfer storage,
  workers, queues, and teardown state.
- Cross-task state uses an explicit queue, lock, atomic handoff, or ownership swap;
  `volatile` is not used as synchronization.

The microphone callback uses a single-producer/single-consumer atomic handoff for
completed sample windows. Background workers follow a cooperative stop sequence:
request stop, wake the worker, wait for acknowledgement, then release captured
state.

### 2.4 Dependency direction

1. `pixoo_protocol` depends only on the C++ standard library.
2. `pixoo_content` contains standalone content-domain models and depends only on
   the C++ standard library.
3. `pixoo_app` depends on `pixoo_protocol` for frame-output policy and otherwise
   communicates through its own value types and ports.
4. ESPHome adapters depend inward on application ports, content models, and
   protocol types.
5. Only YAML and Python code generation name and connect concrete adapters.
6. Production board values originate in
   `esphome/hardware/pixoo64_rev1.yaml`; component schemas also encode required
   invariants such as the supported power-output binding. Protocol and application
   code contain no board pin assignments.
7. Product state has one owner in `FirmwareApp`; ESPHome entity state is an
   external representation and input, not a second policy engine.

ESPHome `packages:` organize YAML but do not create runtime boundaries. The files
are merged before validation; cross-component wiring belongs in the device
composition.

## 3. Repository layers

```text
pixoo_protocol/
  include/ src/        panel command constants, frame encoder, framebuffer,
                       and receive-side UART parser
  test/                protocol and framebuffer unit tests

pixoo_app/
  include/ src/        FirmwareApp, application ports and value types,
                       timezone catalog, sound vocabulary, frame metrics,
                       and FrameOutput policy
  test/                lifecycle, scheduling, button, overlay, sound,
                       output, and metrics tests using fake ports

pixoo_content/
  include/ src/        framework-independent weather, equalizer, clock,
                       and Game of Life models
  test/                content-model, animation, DSP, and clock tests

esphome/
  components/pixoo64/          ESPHome application, panel, weather, sound,
                               microphone, and timezone adapters
  components/pixoo64_content/  renderer, dashboards, drawing utilities,
                               notifications, and reactions
  hardware/pixoo64_rev1.yaml   production board profile
  pixoo64.yaml                 deployable composition root
  tests/config_validation/     schema and wiring tests
  tests/render_test/           deterministic host rendering and snapshots
```

### 3.1 `pixoo_protocol`

This layer owns only the panel data contract:

- command IDs and fixed frame geometry in `pixoo_cmd.h`;
- panel-frame encoding in `pixoo_frame.*`;
- the 64×64 RGB framebuffer in `pixoo_framebuffer.*`; and
- the receive-side panel UART parser in `pixoo_uart.*`.

It does not own brightness policy, resend cadence, rendering, GPIOs, or transport
handles.

### 3.2 `pixoo_app`

This is the policy layer. `firmware_app.h` defines the ports, application
configuration, lifecycle phases, public commands, and `FirmwareApp` state.
`app_state.h` contains shared values such as `LightState`, `Notification`,
`Reaction`, and `Overlay`.

`FrameOutput` scales frame bytes for brightness and decides whether an unchanged
frame must be resent using the interval supplied by its caller. It belongs in this
layer rather than `pixoo_protocol`: those transformations and comparisons are
presentation behavior, not requirements of the wire format. The production panel
adapter owns the concrete maximum resend interval.

The application layer also owns the tested timezone catalog, sound vocabulary,
and frame-metrics aggregation.

### 3.3 `pixoo_content`

This layer contains models that can be compiled and tested without ESPHome:

- weather data, WMO mapping, astronomy, refresh policy, condition transitions,
  and weather animation mechanics;
- microphone spectrum analysis and equalizer level processing;
- split-flap, analog, and binary clock animation state; and
- the bounded 64×64 Game of Life board.

These models do not draw through ESPHome. The concrete dashboard classes under
`esphome/components/pixoo64_content/` combine them with fonts, display drawing,
clock sources, and configured adapters.

### 3.4 ESPHome integration

`esphome/pixoo64.yaml` selects the ESP32 target, framework, board profile, fonts,
services, entities, adapters, and dashboard catalog. The custom component schemas
validate references and values before C++ generation.

`FirmwareAppComponent` adapts ESPHome lifecycle and commands. `Pixoo64Panel`
implements the panel boundary. `ContentController` is the rendering boundary and
owns the configured dashboards. The remaining classes adapt Open-Meteo, RTTTL,
I2S microphone capture, and timezone selection.

## 4. Lifecycle and scheduling

`FirmwareApp::Phase` has four states:

| Phase | Meaning |
|---|---|
| `kOff` | Logical display power is off; regular rendering and microphone capture are stopped. |
| `kWaitingInit` | The panel rail is on and the application is waiting for the configured settle deadline before initialization. |
| `kBootAnimation` | The first successful initialization is showing the timed boot presentation. |
| `kRunning` | The selected dashboard and any queued overlay are scheduled normally. |

When startup restores the light as on, the initial transition uses the configured
cold initialization delay. If startup restores it as off, the first later off→on
transition uses the shorter repower delay, as do subsequent repowers. The panel
adapter reports time spent in its synchronous power ramp so the application can
account for that before the remaining settle wait. If `PanelPort::Initialize()` fails after the deadline, the
application remains in `kWaitingInit` and retries on each 5 ms service poll; there
is currently no backoff or separate failure phase.

The 5 ms component poll is only a service opportunity. Actual rendering follows
separate deadlines:

- each dashboard supplies a validated positive frame interval through
  `DashboardSelection`;
- boot frames use `boot_frame_interval_ms`;
- notifications and reactions use `overlay_frame_interval_ms`; and
- production values for boot and overlays are 33 ms.

Deadlines use wrap-safe unsigned time arithmetic. A delayed poll renders at the
current tick and advances the deadline; it does not render a burst of missed
frames. This keeps work bounded when networking or another ESPHome component
delays the main loop.

A dashboard selection resolves through `RenderPort`'s trusted catalog. The result
contains its canonical ID, frame interval, and microphone requirement. The
application enables capture only while the selected base dashboard requires it.

## 5. Rendering and presentation

### 5.1 Dashboard catalog

Each configured dashboard has a stable ID and implements the shared `Dashboard`
interface. ESPHome's typed schema admits the closed dashboard families `text`,
`weather`, `equalizer`, `game_of_life`, and `clock`. Final validation checks that
the dashboard-select options match the renderer catalog and that its initial
option matches the renderer default.

`ContentController` stores non-owning pointers to generated dashboard objects.
It resolves IDs, calls visibility hooks, advances animation from the application
tick, and renders into reusable storage. The application does not switch on
dashboard type.

A dashboard that needs wall-clock time receives an ESPHome real-time clock. Its
animation still advances from the `now_ms` tick supplied by `FirmwareApp`; this
keeps animation deterministic and separates elapsed time from civil time.

Clock, equalizer, and weather dashboards split common policy from presentation:

- `ClockDashboard` delegates animation and drawing to a `WatchFace`.
- `EqualizerDashboard` owns smoothing and peak behavior and delegates to an
  `EqualizerFace`.
- `WeatherDashboard` obtains a coherent source snapshot, builds its view model,
  and delegates to a `WeatherFace`.

Faces are selected through ordinary dashboard IDs at composition time. There is
no second runtime face selector.

### 5.2 Drawing storage

`ContentController` owns the active framebuffer, a clean base snapshot, and a
reaction-background buffer. Shared `BlendCanvas` and `aa_draw` utilities provide
coverage-based shapes, gradients, glows, and alpha composition without allocating
per frame.

Rendering returns a `FrameView` over this retained storage. Presentation is
synchronous: the panel adapter finishes consuming the view before the next render
may reuse it.

### 5.3 Notifications and reactions

One base dashboard is active at a time. Notifications and reactions are transient
overlays managed by a bounded FIFO in `FirmwareApp`.

- A notification keeps the base dashboard live. Its banner is composited over the
  latest base frame, and its minimum visible time may increase to accommodate text
  animation.
- A reaction freezes one clean base frame, derives a blurred and darkened
  background once, and restores that background before each animated reaction
  frame.
- The first queued overlay snapshots light state and may wake an off display.
  When the last overlay expires or the queue is cleared, the application restores
  the prior state.
- Queue timing, wake/restore behavior, and notification sound coordination belong
  to `FirmwareApp`; pixel composition belongs to `ContentController`.

### 5.4 Weather and microphone work

`OpenMeteoSource` performs HTTP work outside the rendering path and exposes a
coherent weather snapshot through `WeatherSource`. Requests are made only while
the weather dashboard is relevant, Wi-Fi is connected, and cached data needs a
refresh.

The microphone adapter captures I2S samples into retained windows. Completed
windows pass through the framework-independent spectrum and equalizer processors,
then normalized levels are sent to every configured equalizer face. Rendering
never waits for a new audio window.

## 6. Panel boundary

`Pixoo64Panel` implements the hardware-facing `PanelPort`. Physical interfaces,
wire framing, pins, rates, and electrical constraints belong to
[hardware.md](hardware.md). At the software boundary the adapter:

- translates power, initialization, brightness, and presentation requests into
  the panel operations;
- consumes each borrowed `FrameView` synchronously;
- retains transfer and previous-frame storage instead of allocating during
  presentation;
- exposes synchronous power-on time to the application lifecycle;
- owns hardware-resource setup and idempotent teardown state; and
- rejects later operations once shutdown or teardown begins.

`FirmwareApp` owns when power and initialization occur. `FrameOutput` owns
brightness scaling and unchanged-frame comparison; `Pixoo64Panel` supplies the
production maximum resend interval. Soft-start values originate in the board
profile; cold-start and repower settle delays are `FirmwareAppConfig` policy.

## 7. Configuration ownership

| Kind | Examples | Owner | Rebuild required? |
|---|---|---|---|
| Hardware profile | GPIOs, polarity, transport and audio rates, power ramp | `esphome/hardware/pixoo64_rev1.yaml` and component schema | Yes |
| Build composition | Board/framework, fonts, adapters, dashboard catalog, frame intervals | `esphome/pixoo64.yaml` | Yes |
| Deployment secrets | API encryption key, OTA password, fallback-AP password | local `esphome/secrets.yaml` | Yes |
| Provisioned state | Wi-Fi credentials | ESPHome/NVS | No |
| Runtime settings | Light state, dashboard, text, timezone, weather location and refresh interval, sound enable | persisted ESPHome entities | No |
| Product policy | Lifecycle delays, button behavior, overlay queue and restore rules | `pixoo_app` | Yes |
| Runtime state | Current phase, deadlines, framebuffers, weather cache, DSP windows | application and adapter memory | No |

The board profile is the production source of pins, button polarity, transport
rates, microphone stream values, and panel-ramp values. Python schemas validate
those values and enforce cross-component invariants before injecting them into
C++. Adapters repeat checks where a framework mismatch could otherwise drive an
unsupported hardware configuration.

API actions, entity callbacks, and OTA callbacks are inbound adapter events. They
are converted to typed `FirmwareApp` commands rather than implementing product
policy in YAML. Deployment and operation are documented in the
[manual](manual.md); development-time configuration is documented in
[CONTRIBUTING.md](../CONTRIBUTING.md).

## 8. Extension rules

- Keep cross-feature policy in `FirmwareApp`; do not coordinate sibling adapters
  directly.
- Add a port only for a real external boundary or useful host-test substitute.
- Keep board pins and rates in the validated hardware profile.
- Add a dashboard through the typed schema and stable renderer catalog. Update the
  dashboard-select catalog in the same composition change.
- Express dashboard capabilities, such as microphone use, as metadata rather than
  string checks.
- Advance animation from the supplied tick time. Use a wall clock only for civil
  time data.
- Keep backend-specific names behind closed value types; public callers do not
  pass arbitrary RTTTL or provider-specific strings.
- Reuse retained frame and working storage in render and presentation paths.
- Keep framework-independent policy and models covered by native tests; use host
  rendering only for integration behavior that depends on fonts and drawing.

There is intentionally no external or Home Assistant raw-frame streaming API.
All visual content exposed by the deployed firmware is rendered on the device.

Contributor-facing rules for YAML logic, required checks, generators, and visual
snapshot updates are in [CONTRIBUTING.md](../CONTRIBUTING.md).

## 9. Test boundaries

The architecture keeps most behavior below framework boundaries so each layer can
be tested at its natural level:

| Boundary | Verification |
|---|---|
| `pixoo_protocol` | Native tests for frame encoding, framebuffer geometry, and UART parsing. |
| `pixoo_app` | Native tests for lifecycle and product policy using fake ports. |
| `pixoo_content` | Native tests for weather, DSP, clock, and Game of Life models. |
| ESPHome composition | Configuration tests for schemas, references, and expected invalid wiring. |
| Renderer integration | Deterministic host snapshots using the production content pipeline and substituted external sources. |
| Hardware adapters | ESP32 target compilation plus behavior on the documented board. |

The commands and snapshot-update procedure are owned by
[CONTRIBUTING.md](../CONTRIBUTING.md#required-checks).

## 10. Runtime metrics

`ContentController` measures render-only work. `FirmwareAppComponent`, through
`FrameMetricsPort`, measures complete regular-frame work from immediately before
rendering until synchronous presentation returns. Both aggregate in RAM and
publish windowed diagnostics rather than updating ESPHome entities per frame.

Boot, off, initialization, and waiting ticks are excluded. Base refreshes beneath
a live notification contribute to renderer timing but not complete presented-frame
timing when they do not call the panel.

Operator-facing logs, privacy behavior, and current limitations are documented in
the [manual](manual.md).
