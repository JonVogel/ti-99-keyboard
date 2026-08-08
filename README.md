# TI-99/4A Modern Keyboard Adapter

An ESP32-S3 based adapter that lets you use a modern Bluetooth (or USB) keyboard
with a Texas Instruments TI-99/4A home computer.

The adapter installs inside the TI in place of the original keyboard connector
and emulates the original 6×8 key matrix, translating HID reports from a BLE/USB
keyboard into key presses the console's keyboard scan sees as if they came from
the original keyboard. The original keyboard can optionally run in parallel.

## Features

- **Bluetooth LE keyboard input** using the **NimBLE-Arduino** stack
  - Auto-pairing / "Just Works" bonding on first use
  - Auto-reconnect by saved address after sleep or power cycle
  - **F12** on the keyboard enters pairing mode for a new keyboard
- **Status LED** (onboard WS2812) shows connection state
- Full punctuation and symbol mapping, including the FCTN-generated characters
  (`? _ [ ] { } ' " \ | ~` `` ` `` `: < >`)
- System keys that work on real hardware: **QUIT** (Alt+=) and **CLEAR / BREAK**
  (Alt+4) to stop a running program
- Software **Alpha Lock** that fixes the original joystick-UP bug (see below)
- Optional **parallel original keyboard** — both can drive the matrix at once
- **USB keyboard input** via the native USB-OTG host port (using `EspUsbHost`),
  usable alongside or instead of BLE

## Hardware — Bill of Materials

The adapter comes in **two editions** that are electrically identical and run
the same firmware; they differ only in how the 16 level-shifter channels are
built:

- **DIY edition** (board rev ≤6): all through-hole, level shifting on four
  plug-in BOB-12009 modules — buildable with just a soldering iron.
- **Assembled edition** (board rev 7+): the shifters are 48 SMT parts
  populated by JLCPCB during fabrication — you hand-solder only connectors.

### Common parts (both editions)

| Part | Purpose |
|------|---------|
| Hosyond ESP32-S3 N16R8 dev board (USB-C, WROOM-1, 16MB flash, 8MB PSRAM) — sold in 3-packs on Amazon | MCU with USB host and BLE |
| 2× 1×22 female socket strips, 2.54mm | Sockets the ESP32 dev board (never solder the module directly) |
| 2× 1×15 male pin headers, 2.54mm (snap from 1×40 breakaway strips — e.g. Adam Tech `PH1-40-UA`, DigiKey ~$0.48/strip) | J10 (to the TI) and J20 (optional original keyboard) |
| 1×2 + 1×1 male header pins (from the same breakaway strips) | J9 bench power header, J15 spare-channel pad |
| TI PSU daisy-chain header — Molex KK-396 4-pin, `09-65-2048` (DigiKey **WM18825-ND**; alt `26-60-4040` / **WM4622-ND**) | Passes the TI's power through the board; the TI mainboard's existing female plug mates here (J14) |
| PSU-side mating plug — Molex `09-50-3041` housing (DigiKey **WM2102-ND**) + 4× `08-50-0106` crimp terminals (DigiKey **WM2300-ND**) + ~30cm of 22 AWG stranded wire | Cable from the J13 solder pads to the TI power supply's header |
| 15-pin female-to-female keyboard ribbon (DIY from bulk Dupont parts, ~$1.5–3.5/cable, no crimping — see note below; or ready-made aftermarket TI ribbon, ~$8) | Connects the adapter's male header to the TI's male keyboard header |

No voltage regulator is needed — the board runs on regulated +5V taken
straight from the TI PSU via the power daisy-chain. Crimp tool for the KK
terminals: IWISS SN-48B (~$20), or fold-and-solder the terminal barrels.

### DIY edition — additional parts (board rev ≤6)

| Part | Purpose |
|------|---------|
| PCB fabbed from `pcb/gerbers/ti99-kb-adapter_r6.zip` (2-layer, 1.6mm, HASL — ~$1/board at JLCPCB) | The carrier board, all through-hole |
| 4× BOB-12009 BSS138 level-shifter breakouts (SparkFun ~$3.50 ea, or AliExpress clones) | The 16 shifter channels (4 per module) |
| 8× 1×6 female socket strips, 2.54mm + male header pins on each BOB | Sockets the four BOB modules |

**Solder generously on the BOB headers** — under-heated joints there were the
single most common build fault. Test every finished board with
`test-matrix-loopback/` (onboard LED: green = good, red = bad).

### Assembled edition — what to order (board rev 7+)

| Item | Details |
|------|---------|
| JLCPCB order with **PCB Assembly (Economic, Top side)** using the three files in `pcb/gerbers/`: `ti99-kb-adapter_r7.zip` + `ti99-kb-adapter_BOM.csv` + `ti99-kb-adapter_CPL.csv` | Boards arrive with all 48 shifter parts (16× BSS138 `C78284`, 32× 10kΩ `C17414`) machine-soldered — ~$3/board + shipping in qty 10 |

Then add the common parts above (connectors + socketed ESP32) — no SMT work,
no BOB modules, no shifter soldering.

The adapter is intended for permanent installation inside a TI-99/4A with the
original keyboard removed (or run in parallel) and a 3D-printed cover over the
keyboard opening.

> **Sourcing the keyboard ribbon (cheap DIY):** the cable is a straight 15-way
> female-to-female run — both the adapter and the TI motherboard expose 1×15 male
> headers. Instead of a ready-made aftermarket TI ribbon (arcadeshopper, ~$8),
> build it from bulk commodity Dupont parts for ~$1.5–3.5/cable with **no
> crimping and no shells to strip**:
> - **15× female-to-female _no-shell_ pre-crimped 2.54mm leads** per cable —
>   [AliExpress](https://www.aliexpress.us/item/3256804990855815.html)
>   (search: *"Dupont 2.54mm female-to-female no-shell pre-crimped jumper wire 40P"*).
> - **2× 1×15 single-row Dupont female housings** per cable —
>   [AliExpress](https://www.aliexpress.us/item/3256806021686798.html)
>   (search: *"Dupont 2.54mm 1x15P housing female single row"*).
>
> Slide 15 bare pins into each housing — that's the whole assembly. A genuine
> Molex 1×15 housing (`50-57-9315`, DigiKey ~$1) also fits, but only accepts Molex
> crimp terminals — commodity Dupont pins won't lock in it, so stay all-Dupont.
> AliExpress links rot; the search terms are the durable fallback. Orientation
> gotcha is noted under the pinout below (red stripe = pin 15).

> **Note:** an earlier revision used TXS0108E auto-direction level shifters. They
> do **not** work for a keyboard matrix — the auto-direction detector can't
> handle open-drain / resistive loads and channels latch or oscillate. The design
> uses **BSS138 discrete MOSFET shifters**, which are bidirectional by physics and
> handle open-drain cleanly. Do not substitute TXS0108E.

## TI-99/4A Keyboard Connector Pinout

The TI's internal keyboard connector is a 1×15 single-row 0.1" header. The console
drives **six column lines** low one at a time (via a 74LS156, pulled up with 1kΩ)
and reads **eight row lines** back (into the TMS9901, pulled up with 10kΩ). The
adapter reads the columns (inputs) and drives the rows (open-drain outputs).

GPIOs below are the **v5 (straight-ribbon) mapping**: the flat ribbon lands
adapter connector (J10) pin *p* on TI pin 16−*p*, and the firmware pin map
mirrors to match. (Rev-3 boards used a 180°-twisted ribbon and a different
GPIO map — see the `v3` git tag.)

| TI pin | Signal | Matrix | ESP32 GPIO | Direction |
|:------:|--------|--------|:----------:|-----------|
| 1  | INT5  | row 2 | 13 | ESP32 → TI (drive) |
| 2  | INT6  | row 3 | 12 | ESP32 → TI (drive) |
| 3  | INT8  | row 5 | 11 | ESP32 → TI (drive) |
| 4  | INT4  | row 1 | 10 | ESP32 → TI (drive) |
| 5  | INT3  | row 0 | 9  | ESP32 → TI (drive) |
| 6  | P5    | Alpha Lock | 3 | wired, never driven (software alpha lock) |
| 7  | INT7  | row 7 | 8  | ESP32 → TI (drive) |
| 8  | 1Y1   | col 0 | 18 | TI → ESP32 (read) |
| 9  | 1Y0   | col 4 | 17 | TI → ESP32 (read) |
| 10 | INT9  | row 6 | 16 | ESP32 → TI (drive) |
| 11 | INT10 | row 4 | 15 | ESP32 → TI (drive) |
| 12 | 2Y0   | col 5 | 7  | TI → ESP32 (read) |
| 13 | 2Y1   | col 1 | 6  | TI → ESP32 (read) |
| 14 | 2Y2   | col 2 | 5  | TI → ESP32 (read) |
| 15 | 2Y3   | col 3 | 4  | TI → ESP32 (read) |

A 16th level-shifter channel (GPIO14 → **J15 "SPARE" solder pad**) is wired as a
repair path: if any matrix GPIO or shifter channel dies, jumper J15 to the dead
line's pad and remap that line's `#define` to GPIO14 — no board respin needed.

> **Ribbon orientation gotcha:** aftermarket TI keyboard ribbons mark **pin 15**
> with the red stripe, not pin 1. Verify against the silkscreen before plugging in.

### How the matrix output works

A hardware timer samples all six column inputs at ~200 kHz. Each column is
debounced asymmetrically — it becomes *active* on the first LOW sample (so the row
is driven before the ROM reads it) and only releases after two consecutive HIGH
samples (so ribbon coupling can't glitch a held key). The rows driven are the OR
of the pressed keys in every currently-active column, so an ordinary one-column
scan drives exactly that column, while the console's deliberately-overlapping QUIT
and CLEAR scans (which strobe two columns at once) are presented correctly. Rows
are open-drain, so the original keyboard can coexist on the same lines.

The dedicated Alpha Lock line (TI pin 6) is intentionally **never driven** — see
below.

## Software Build

The BLE library `BleHidHost/` lives in-tree and is picked up automatically; the
only external dependency to install is **NimBLE-Arduino**.

### Toolchain

Builds with `arduino-cli` via the included `build.bat` (Windows). FQBN:

```
esp32:esp32:esp32s3:PartitionScheme=no_ota,FlashSize=16M,PSRAM=opi
```

- `build.bat compile` — compile only
- `build.bat` — compile + upload (default COM port)
- `build.bat upload COM7` — upload to a specific port
- `build.bat monitor COM7` — serial monitor at 115200 baud

### Arduino IDE alternative

1. Install **ESP32 board support** (Boards Manager → "esp32 by Espressif")
2. Install **NimBLE-Arduino** (Library Manager → search "NimBLE-Arduino")
3. Point the sketchbook libraries at the in-tree `BleHidHost/` folder (or copy it
   into `~/Documents/Arduino/libraries/`)

### Board settings

- Board: **ESP32S3 Dev Module**
- Flash Size: **16MB**, PSRAM: **OPI PSRAM**, Partition Scheme: **no_ota**
- USB CDC On Boot: **Disabled** (frees the USB-OTG port for host mode; use the
  UART port for the serial monitor)

### Selecting input mode

At the top of `ti-99-keyboard.ino`:

```cpp
// #define INPUT_USB   // USB keyboard via USB-OTG port (currently disabled)
#define INPUT_BLE      // Bluetooth LE HID keyboard
```

## Usage

### First-time BLE pairing

1. Power on the ESP32 — the LED pulses purple (scanning)
2. Put your keyboard in pairing mode
3. The ESP32 finds the keyboard, pairs, bonds, and saves its address to NVS
4. LED turns dim green when ready; it reconnects automatically on later boots

### Re-pairing a different keyboard

Press **F12** on the currently connected keyboard. It disconnects, the ESP32
opens a 30-second pairing window (LED pulses blue), and any HID keyboard put in
pairing mode during that window is adopted as the new peer.

### LED status

| Color | Meaning |
|-------|---------|
| Pulsing blue | Startup or pairing mode |
| Pulsing purple | Scanning for keyboard |
| Steady dim green | Keyboard connected, idle |
| Bright green flash | Key pressed |
| Red blink | Error |

## Key Mapping

Modifiers pass straight through; the host **Alt** key acts as TI **FCTN**.

| Modern key | TI-99/4A |
|------------|----------|
| Alt | FCTN |
| Ctrl | CTRL |
| Shift | SHIFT |
| Caps Lock | Alpha Lock (software-emulated; see below) |

Punctuation and symbols (unshifted / shifted):

| Key | Unshifted | Shifted |
|-----|-----------|---------|
| `-` | `-` | `_` (FCTN+U) |
| `=` | `=` | `+` |
| `/` | `/` | `?` (FCTN+I) |
| `;` | `;` | `:` |
| `,` | `,` | `<` |
| `.` | `.` | `>` |
| `'` | `'` (FCTN+O) | `"` (FCTN+P) |
| `[` | `[` (FCTN+R) | `{` (FCTN+F) |
| `]` | `]` (FCTN+T) | `}` (FCTN+G) |
| `\` | `\` (FCTN+Z) | `\|` (FCTN+A) |
| `` ` `` | `` ` `` (FCTN+C) | `~` (FCTN+W) |

Editing / function keys:

| Modern key | TI-99/4A |
|------------|----------|
| Esc | FCTN+9 (BACK) |
| Backspace | FCTN+S |
| Tab | FCTN+7 |
| Delete | FCTN+1 |
| Arrows | FCTN+E / S / D / X (up / left / right / down) |
| F1–F10 | FCTN+1…0 (DEL, INS, ERASE, CLEAR, BEGIN, PROC'D, AID, REDO, BACK, …) |
| F11 | Toggle type-ahead buffer on/off |
| F12 | Enter BLE pairing mode (not forwarded to the TI) |

The F-key shortcuts let you press the standard TI FCTN+digit combos with a single
key. Two system combos worth calling out, both verified on real hardware:

- **Alt+=** → FCTN+= = **QUIT** (soft-resets to the master title screen)
- **Alt+4** (or **F4**) → FCTN+4 = **CLEAR** (breaks a running BASIC program)

### Alpha Lock and joystick compatibility

The original TI-99/4A has a notorious hardware bug: the Alpha Lock key shares an
electrical signal with joystick UP, so any game played with Alpha Lock engaged
sees a permanent UP input. The classic fix was to physically pop the Alpha Lock
key out of its socket before playing.

This adapter sidesteps the bug entirely by implementing Alpha Lock in software.
When Caps Lock is on, the adapter injects SHIFT into letter keypresses (producing
capital letters via the normal keyboard-scan path) and **never drives the
dedicated Alpha Lock line** (TI pin 6). Joysticks therefore work correctly
regardless of Caps Lock state, and numbers/punctuation are unaffected by Caps
Lock — matching the original Alpha Lock semantics.

## Status

- **BLE keyboard input:** working; tested with the RoyalAxe / ProtoArc L75.
- **TI-99/4A matrix output:** working on real hardware — typing, SHIFT/CTRL/FCTN,
  full punctuation, QUIT, and CLEAR/BREAK, with the original keyboard usable in
  parallel.
- **USB keyboard input:** working — plug a USB keyboard into the ESP32's native
  USB-OTG port (`EspUsbHost`); runs alongside BLE.

## License

MIT — see [LICENSE](LICENSE).
