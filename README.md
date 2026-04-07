# TI-99/4A Modern Keyboard Adapter

An ESP32-S3 based adapter that lets you use a modern USB or Bluetooth keyboard
with a Texas Instruments TI-99/4A home computer.

The adapter plugs into the TI's internal keyboard connector and emulates the
original 6×8 key matrix, translating HID reports from a USB or BLE keyboard
into key presses the TMS9901 sees as if they came from the original keyboard.

## Features

- **USB keyboard input** via the ESP32-S3's native USB-OTG port
- **Bluetooth LE keyboard input** using the standard ESP32 Bluedroid stack
  - Auto-pairing on first use ("Just Works" bonding)
  - Auto-reconnect after sleep or power cycle
  - **F12** on the keyboard enters pairing mode for a new keyboard
  - **BOOT button** on the ESP32 is the escape hatch when no working
    keyboard is available
- **Status LED** (onboard WS2812) shows connection state
- **Dual mode**: Both USB and BLE can be enabled simultaneously
- Maps modern keys (arrows, F-keys, Esc, Tab, etc.) to TI-99/4A
  FCTN combinations

## Hardware

| Part | Purpose |
|------|---------|
| ESP32-S3-DevKitC-1 | MCU with USB host and BLE |
| 2× TXS0108E | 3.3V ↔ 5V level shifters (8 channels each) |
| Buck converter (12V→5V) | Power from TI's 12V rail |
| 1×15 dupont cable | Connects to TI keyboard header |

The adapter is intended for permanent installation inside a TI-99/4A with the
original keyboard removed and a 3D-printed cover over the keyboard opening.

## TI-99/4A Keyboard Connector Pinout

The TI's internal keyboard connector is a 1×15 single-row 0.1" header.
Six column lines (active-low, from the TI's 74LS156) and eight row lines
(connecting back to the TMS9901 INT pins) form the matrix:

| Pin signal | ESP32 GPIO | Direction |
|------------|------------|-----------|
| 1Y1 (col 0) | 4 | TI → ESP32 |
| 2Y1 (col 1) | 5 | TI → ESP32 |
| 2Y2 (col 2) | 6 | TI → ESP32 |
| 2Y3 (col 3) | 7 | TI → ESP32 |
| 1Y0 (col 4) | 15 | TI → ESP32 |
| 2Y0 (col 5) | 16 | TI → ESP32 |
| INT3 (row 0) | 17 | ESP32 → TI |
| INT4 (row 1) | 18 | ESP32 → TI |
| INT5 (row 2) | 8 | ESP32 → TI |
| INT6 (row 3) | 9 | ESP32 → TI |
| INT10 (row 4) | 10 | ESP32 → TI |
| INT8 (row 5) | 11 | ESP32 → TI |
| INT9 (row 6) | 12 | ESP32 → TI |
| INT7 (row 7) | 13 | ESP32 → TI |
| Alpha Lock | 14 | ESP32 → TI |

The ESP32 monitors which column the TMS9901 is currently scanning, then
drives the appropriate row line(s) low to simulate key presses based on
the current HID input state. Row outputs are open-drain via the TXS0108E
so the original keyboard could coexist if desired.

## Software Build

### Arduino IDE Setup

1. Install **ESP32 board support** (Boards Manager → "esp32 by Espressif")
2. Install **EspUsbHost** library (Library Manager → search "EspUsbHost")
3. BLE uses the built-in ESP32 BLE library — no extra install needed

### Board Settings

- Board: **ESP32S3 Dev Module**
- USB Mode: **USB-OTG (TinyUSB)** (only required for USB input)
- USB CDC On Boot: **Disabled** (use the UART port for the serial monitor)

### Selecting Input Mode

At the top of `ti-99-keyboard.ino`, comment out either define to disable
that input source:

```cpp
#define INPUT_USB   // USB keyboard via USB-OTG port
#define INPUT_BLE   // Bluetooth LE HID keyboard
```

## Usage

### First-time BLE pairing

1. Power on the ESP32 — the LED pulses purple (scanning)
2. Put your keyboard in pairing mode
3. The ESP32 finds the keyboard, pairs, and bonds automatically
4. LED turns dim green when ready

### Re-pairing a different keyboard

Press **F12** on the currently connected keyboard. The current keyboard
disconnects, the ESP32 enters pairing mode, the LED pulses blue for 30
seconds, and any HID keyboard in pairing mode during that window will be
adopted as the new peer.

If the current keyboard is broken, lost, or unavailable, press the **BOOT
button** on the ESP32 dev board instead. It does the same thing as F12 and
exists as a hardware escape hatch for the "no working keyboard" case.

### LED Status

| Color | Meaning |
|-------|---------|
| Pulsing blue | Startup or pairing mode |
| Pulsing purple | Scanning for keyboard |
| Steady dim green | Keyboard connected, idle |
| Bright green flash | Key pressed |
| Red blink | Error |

## Key Mapping

Most keys map directly. Special mappings via the host's Alt key
(treated as TI **FCTN**) or hardcoded combos:

| Modern key | TI-99/4A |
|------------|----------|
| Alt | FCTN |
| Ctrl | CTRL |
| Shift | SHIFT |
| Caps Lock | Alpha Lock (software-emulated; see below) |
| Esc | FCTN+9 (BACK) |
| Backspace | FCTN+S |
| Tab | FCTN+7 |
| Delete | FCTN+1 |
| Arrows | FCTN+E/X/S/D |
| F1 | FCTN+1 |
| F2 | FCTN+2 |
| F3 | FCTN+3 |
| F4 | FCTN+4 (CLEAR) |
| F5 | FCTN+5 (BEGIN) |
| F6 | FCTN+6 (PROC'D) |
| F7 | FCTN+7 (AID) |
| F8 | FCTN+8 (REDO) |
| F9 | FCTN+9 (BACK) |
| F10 | FCTN+0 |
| F11 | (unmapped, reserved) |
| F12 | Enter BLE pairing mode (not forwarded to TI) |
| / | FCTN+I |
| - | FCTN+U |

The F-key shortcuts let you press the standard TI FCTN+digit combos with a
single key on the modern keyboard, instead of holding Alt and a digit at
the same time.

### Alpha Lock and joystick compatibility

The original TI-99/4A has a notorious hardware bug: the Alpha Lock key
shares an electrical signal with joystick UP, so any game played with
Alpha Lock engaged sees a permanent UP input. The classic fix was to
physically pop the Alpha Lock key out of its socket before playing.

This adapter sidesteps the bug entirely by implementing Alpha Lock in
software. When Caps Lock is on, the adapter automatically injects SHIFT
into letter keypresses (producing capital letters via the normal KSCAN
path), and **never drives the dedicated Alpha Lock line on the keyboard
connector**. Joysticks therefore work correctly regardless of Caps Lock
state. Numbers and punctuation are unaffected by Caps Lock, matching the
original Alpha Lock semantics.

## Status

This is a work in progress. As of the initial commit:

- BLE keyboard input: working and tested with ProtoArc L75
- USB keyboard input: code complete, awaiting hardware testing
- TI-99/4A matrix output: code complete, awaiting hardware integration

## License

MIT — see [LICENSE](LICENSE).
