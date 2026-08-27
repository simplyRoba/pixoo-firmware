# Pixoo64 hardware

Hardware reference for the Divoom Pixoo64: chips, interfaces, pin maps, and the
ESP32↔panel protocols. Facts only.

## 1. Overview

The Pixoo64 is a Wi-Fi 64×64 RGB LED matrix display, USB-C powered. It is built
from two PCBs:

- **Mainboard** (`Pixoo64-Wifi Mainboard`, REV1.1, date code `20210318`): carries
  the ESP32-WROVER-IE module, 8MB flash, USB-C, and the microSD slot. Runs the
  networking/app firmware.
- **LED panel** (`Pixoo64 Wifi LEDBoard`, REV1.0, date code `20210525`): carries
  the 64×64 LED matrix,
  a dedicated ARM MCU (`AT32F413CBT7`) that owns the LED refresh, the LED driver
  ICs, a MEMS microphone, a buzzer, and two front buttons.

Unless separately sourced, measurements and security-state observations in this reference apply to the examined `Pixoo64-Wifi Mainboard` REV1.1 and `Pixoo64 Wifi LEDBoard` REV1.0 sample.

The two boards are joined by:

- a **14-pin FFC ribbon** carrying all logic signals (display SPI, control UART,
  microphone I2S, buttons, buzzer, panel 3V3, ground), and
- a separate **6-pin `CN1` harness** carrying the high-current LED power rail and
  ground.

The mainboard is the master: it delivers the panel's 3V3 logic rail and gates the
LED power rail, drives the display over SPI, and reads the microphone and buttons.
The panel MCU owns the continuous LED matrix refresh.

## 2. Mainboard

### 2.1 ESP32-WROVER-IE module

- Espressif `ESP32-WROVER-IE` module, external antenna (u.FL coax lead).
- Silicon: `ESP32-D0WD-V3`, chip revision 3, classic dual-core ESP32 (not
  S2/S3/C3), classic ESP32 ROM v1 (`ets Jul 29 2019`). No native USB peripheral.
- External PSRAM: 8MB (8192 KB) quad SPI RAM. The classic ESP32 maps at most 4MB
  of external RAM into its address space at once; the remaining capacity requires
  himem bank switching.

### 2.2 Internal flash

- 8MB SPI flash. JEDEC manufacturer `0xc8`, device `0x4017`.
- Flash mode DIO, 3.3V supply (GPIO12/MTDI strap reads low at boot).

### 2.3 Observed eFuse / security state

On the examined mainboard:

- Flash Encryption is disabled (`FLASH_CRYPT_CNT = 0`).
- Secure Boot is disabled (`ABS_DONE_0 = False`, `ABS_DONE_1 = False`).
- UART ROM download mode is allowed (`UART_DOWNLOAD_DIS = False`).
- JTAG is not eFuse-disabled (`JTAG_DISABLE = False`).

This chip accepts a plaintext full-flash read-back and unsigned firmware.

### 2.4 USB-C port

Power input only. In normal operation it does not enumerate as a USB device
(no serial/CDC/mass-storage device appears on a host, even with a data cable).
There is no USB-UART bridge chip on the board.

### 2.5 microSD slot

On the mainboard, driven by the ESP32 SDMMC host peripheral (not SPI mode). See
§11 for the pin map and configuration.

### 2.6 Power tree

USB-C 5V feeds a switching converter (shielded inductor marked `701`) and
`AO3401` P-channel MOSFET load switches (`Q4`/`Q5`, marked `3401A`) that gate the
LED power rail onto `CN1`. The gate is ESP32 **GPIO22** (§10). The panel's 3V3
logic rail is delivered separately over ribbon pin 14.

### 2.7 Test pads and debug access

- Back-side pad cluster: `GND`, `TX`, `RX`, `IO0`, `IO2`. `SW1` footprint present
  but unpopulated.
- `IO0` is the BOOT strap: held low across a cold power cycle, the ESP32 enters
  ROM serial download mode over the UART pads.

## 3. LED panel

### 3.1 Panel MCU

Front-side top-edge board marking: `94V-0 E350388 XZG-I 2240`.

`AT32F413CBT7` ARM MCU. It receives display frames and control commands from the
ESP32 (SPI in, UART reply out) and owns the continuous LED matrix scan/refresh.
Programming/debug is exposed via panel-side SWD pads (§3.5).

### 3.2 LED driver ICs

- **24× `SM16380SC`** constant-current LED (column) drivers — 8 channels each =
  192 column current sinks.
- **8× `SM5166PF`** row/scan MOSFET switch arrays — 8 rows each = 64 rows.
- Local bulk decoupling near the drivers (e.g. `C33`, 100µF tantalum, marking
  `107A`).

### 3.3 Display topology

Row-scanned RGB matrix (not WS28xx-style daisy-chained smart LEDs):

- 64 columns × 3 (RGB) = 192 column lines = 24 drivers × 8 channels.
- 64 rows = 8 switch arrays × 8 rows.

The 24 `SM16380SC` sink the 192 RGB column currents in parallel; the 8 `SM5166PF`
select one row at a time; the panel `AT32F413CBT7` cycles rows to refresh from its
own framebuffer. Brightness is set by scaling pixel values, not by a refresh
command — there is no per-row/refresh traffic on the ESP32 side.

### 3.4 On-panel peripherals

- `U37` — ported metal-can MEMS microphone (front, center-bottom). Read by the
  ESP32 over I2S (§7).
- `BUZ1` — buzzer. Driven by the ESP32 over a PWM line on ribbon pin 1 (§8).
- `S1` / `S2` — two front tactile buttons. `S1` (upper, power icon) and `S2`
  (lower, sun/brightness icon). Read by the ESP32 as GPIO inputs (§9).

### 3.5 Panel debug and connector

- Panel-side SWD pads silk-labeled `GND1`, `3V3`, `SWC`, `SWD` for the ARM MCU.
- `J2` is the panel-side FFC connector for the ribbon.
- Panel trace routing splits by destination: ribbon pins 1–8 route left (to the
  ARM MCU and the two buttons); pins 9–14 route right (to the mic, buzzer, and
  power/ground).

## 4. Interconnect

### 4.1 FFC ribbon (14-pin)

Ribbon pin ↔ ESP32 GPIO from continuity measurement. Pins 13 and 6 are ground.

| Pin | ESP32 GPIO | Role |
|---:|---|---|
| 1 | GPIO23 | Buzzer PWM output (ESP32 → `BUZ1`), LEDC |
| 2 | GPIO32 | UART RX (panel → ESP32), control frames |
| 3 | GPIO33 | SPI MOSI (display data) |
| 4 | GPIO25 | SPI SCLK (display clock) |
| 5 | GPIO26 | SPI CS (chip-select, active-low) |
| 6 | GND | Ground |
| 7 | GPIO35 (input-only) | Power button (`S1`) input |
| 8 | GPIO34 (input-only) | Brightness button (`S2`) input |
| 9 | — | No continuity to ESP32 or rails (panel-internal net or no-connect) |
| 10 | GPIO19 | I2S WS (word select) |
| 11 | GPIO18 | I2S BCLK (bit clock) |
| 12 | GPIO5 | I2S data-in (microphone SD/DIN) |
| 13 | GND | Ground |
| 14 | 3V3 | Panel logic power, mainboard → panel |

Panel 3V3 (pin 14) is sourced entirely from the mainboard; the panel has no local
regulator on this rail. With the ribbon detached, panel-side pin 14 reads 0V and
mainboard-side pin 14 reads 3.3V. A bench setup driving the panel from a separate
controller must feed pin 14 with 3V3 (and ground on 13/6) or the panel logic stays
dead.

### 4.2 CN1 LED-power harness (6-pin)

High-current LED power rail, separate from the ribbon. Numbering: with USB-C at
the bottom and `CN1` at the top, pin 1 is top and pin 6 is bottom. Populated pins:
1, 2, 5, 6. Pins 1 and 2 carry ~5V (the gated LED rail); pins 5 and 6 are ground.
The rail is gated by ESP32 GPIO22 (§10): GPIO22 high → ~5V, low → ~0.1V. This
harness carries power only, not display data.

### 4.3 ESP32 GPIO assignment

| GPIO | Function |
|---|---|
| 2 | SDMMC D0 (also strap) |
| 4 | SDMMC D1 |
| 5 | I2S microphone data-in (ribbon pin 12) |
| 12 | SDMMC D2 (also flash-voltage strap) |
| 13 | SDMMC D3 |
| 14 | SDMMC CLK |
| 15 | SDMMC CMD |
| 18 | I2S BCLK (ribbon pin 11) |
| 19 | I2S WS (ribbon pin 10) |
| 21 | Microphone enable (see §7) |
| 22 | LED-power rail gate (mainboard, not on ribbon) (§10) |
| 23 | Buzzer PWM (ribbon pin 1) |
| 25 | SPI SCLK (ribbon pin 4) |
| 26 | SPI CS (ribbon pin 5) |
| 32 | UART RX from panel (ribbon pin 2) |
| 33 | SPI MOSI (ribbon pin 3) |
| 34 | Brightness button input (ribbon pin 8, input-only) |
| 35 | Power button input (ribbon pin 7, input-only) |

## 5. ESP32 → panel SPI display link

The display is driven from the ESP32 over the SPI2/HSPI master.

**Bus configuration:**

- MOSI = GPIO33 (pin 3), SCLK = GPIO25 (pin 4), CS = GPIO26 (pin 5), MISO unused.
- Clock 15 MHz. SPI mode 0 (CPOL=0, CPHA=0): clock idles low, data latched on the
  rising edge. MSB-first. CS active-low, one CS-low window per full frame
  (header→tail). No CS pre/post delay; no command/address/dummy bits.
- The 12293-byte RGB command plus its 240-byte continuation occupies 12533 bytes
  and approximately 6.68 ms at 15 MHz.

**Frame format:**

```
0xAA  <len LE16>  <cmd>  <payload[len]>  0xBB
```

Header `0xAA`, 2-byte little-endian length, 1-byte command, payload, tail `0xBB`.
Large frames may be split into the command frame plus one `0x21` continuation frame
within the same CS window (padding toward the DMA chunk boundary).

**Command set:**

| cmd | meaning | payload |
|---|---|---|
| `0x00` | Full-frame RGB push | `0x3000` = 12288 bytes = 64×64×3 |
| `0x01` | Brightness | 1 byte (0–100) |
| `0x10` | Init / mode select | 1 byte (`0x00`) |
| `0x21` | Continuation / pad | remainder of a split frame |
| `0x22` | White balance | 3 bytes RGB |

- Full RGB frame on the wire: `AA 00 30 00 <12288 bytes> BB` = 12293 bytes total.
- Observed startup handshake: `0x10` (init) then `0x22` (white balance), before
  framebuffer transfers. White balance is applied by the panel and is not encoded
  into the pixel stream.

**Pixel encoding (mode 64):**

- Each payload channel is one byte. The wire format itself does not define gamma
  or scaling; the sender chooses the byte values before encoding.
- Channel order **R, G, B**.
- Row-major, origin top-left: payload byte 0 = pixel (0,0). No scatter/remap.

**Brightness:** `0x01` carries a 0–100 value.

**Refresh behavior:** animation is not a distinct command; every image is sent as
an ordinary `0x00` framebuffer. The 12533-byte padded transaction gives a
back-to-back wire ceiling near 150 frames per second. The panel MCU refreshes the
LEDs independently from the most recently received frame.

## 6. UART control channel (panel → ESP32)

- Ribbon pin 2 = GPIO32, 115200 baud, same `0xAA <len> <cmd> <payload> 0xBB` frame
  format as the SPI link.
- The wire is unidirectional from the panel to ESP32 GPIO32; the ESP32 does not
  transmit on it.
- At boot, after the ESP32 sends the SPI init (`0x10`), the panel replies here with
  cmd `0x10` and a 4-byte payload carrying the panel ("ledboard") firmware/build
  version, e.g. `aa 04 00 10 42 23 05 06 bb`. The ESP32 reads and stores it.
- Full RGB frames are never carried here (a 12293-byte frame would take ~1067 ms at
  115200 baud); the UART carries only small control frames.
- Init ordering is ESP-first: the ESP32 sends SPI init before the panel's UART
  reply. Frame output is not hard-gated on receiving the reply.

## 7. I2S microphone bus

The ESP32 reads the panel MEMS microphone (`U37`) over I2S as master, RX only.

- BCLK = GPIO18 (pin 11), WS = GPIO19 (pin 10), data-in = GPIO5 (pin 12).
- Config: MASTER | RX, 32000 Hz, 32-bit → BCLK 2.048 MHz, WS 32 kHz (64:1). No
  I2S audio output to the panel on this path.
- **Microphone enable = GPIO21** (mainboard pin, not on the ribbon). Drive high to
  enable the mic; low leaves the data line idle-high (I2S reads return
  `0xFFFFFFFF`). Working read config: left channel, 32 kHz, 32-bit, APLL on.

## 8. Buzzer

The buzzer is driven entirely by the ESP32; no buzzer data crosses the SPI/UART
panel link.

- GPIO23 = ribbon pin 1 → `BUZ1`.
- The buzzer is driven by a ~50%-duty square wave at the note frequency.

## 9. Front buttons

Both front buttons are plain ESP32 GPIO inputs, read directly (not requested from
the panel MCU). Both GPIOs are input-only.

- Pin 7 = GPIO35 = power button (`S1`, upper, power icon).
- Pin 8 = GPIO34 = brightness button (`S2`, lower, sun icon).

## 10. LED-power rail gating (GPIO22)

The `CN1` LED power rail is gated by ESP32 **GPIO22** (a mainboard pin, not on the
ribbon), through the switching converter + `AO3401` P-FET load switches:

- GPIO22 high → `CN1` ~5V (LEDs can light); low → ~0.1V (LEDs dark, panel logic
  still alive on ribbon pin 14).
- The panel logic rail (3V3, pin 14) is independent of this gate — panel logic
  stays up while the LED rail is off.

**Inrush / brownout:** an abrupt rail turn-on draws converter-start and panel
capacitor inrush that can sag 3V3 and reset the ESP32. The GPIO22 gate supports a
controlled ramp instead of an abrupt transition.

**Measured current at 5V input:** rail cut = 0.12A; rail on + panel blanked =
0.31A; rail on + dashboard = 0.39A.

## 11. microSD interface

The slot is wired to the ESP32's dedicated SDMMC peripheral in 4-bit mode, not to
an SPI bus.

- CLK = GPIO14, CMD = GPIO15, D0 = GPIO2, D1 = GPIO4, D2 = GPIO12, D3 = GPIO13
  (fixed classic-ESP32 IO-MUX pins for SDMMC slot 1).
- No hardware card-detect or write-protect GPIO is connected.
- GPIO2 (D0) and GPIO12 (D2) are also ESP32 straps; the board boots reliably with
  the slot occupied or empty.
- The SD pin group (2, 4, 12, 13, 14, 15) does not overlap any panel/peripheral
  pin (SPI 33/25/26, UART 32, I2S 18/19/5, buttons 35/34, buzzer 23, LED-rail 22).

## 12. Debug access

Physical debug and instrumentation points on the documented boards. All logic is
3.3V.

| Access point | Location | Gives |
|---|---|---|
| `TX` / `RX` / `GND` pads | mainboard back-side cluster | ESP32 UART console: boot log (115200 baud) and runtime output |
| `IO0` pad (+ `GND`) | same cluster | held low across a cold power cycle → ESP32 ROM serial download mode |
| USB-C | mainboard edge | power only (no data enumeration) |
| FFC ribbon (14-pin) | mainboard↔panel | tap point for all panel logic signals (SPI/UART/I2S/buttons/buzzer) |
| `CN1` (6-pin) | mainboard↔panel | LED power rail + ground |
| SWD pads (`GND1`/`3V3`/`SWC`/`SWD`) | LED panel | `AT32F413CBT7` debug/programming |

The UART pads use 3.3V logic. On the examined unit, 230400 baud is reliable for
bulk ROM-download transfers; 460800 and 921600 baud corrupt packets. Backup,
first-flash, and recovery procedures are in the [manual](manual.md).

Hardware-measurement requirements and public tool workflow are in
[CONTRIBUTING.md](../CONTRIBUTING.md#hardware-evidence).

## 13. Open hardware items

- **Ribbon pin 9:** no continuity to the ESP32, GND, or 3V3. Likely a
  panel-internal net or no-connect. Panel-side destination not traced.
- **ESP32 JTAG:** not attempted. The classic ESP32 exposes JTAG on GPIO12 (TDI),
  GPIO13 (TCK), GPIO14 (TMS), GPIO15 (TDO) — the same pins used here by the SDMMC
  slot (§11). JTAG is not eFuse-disabled (`JTAG_DISABLE = False`, §2.3), but no
  mainboard pads for GPIO12–15 have been located and no adapter has been connected.
  Using it would also need the flash-voltage strap on GPIO12 handled (3.3V flash).
