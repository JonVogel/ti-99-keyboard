/*
 * TI-99/4A USB + Bluetooth Keyboard Adapter
 * For ESP32-S3-DevKitC-1
 *
 * Bridges a USB or Bluetooth keyboard to the TI-99/4A keyboard connector,
 * allowing use of a modern keyboard with the TI home computer.
 *
 * Input modes (select one or both via #define below):
 *   INPUT_USB  - USB keyboard via the USB-OTG port
 *   INPUT_BLE  - Bluetooth Low Energy (BLE) HID keyboard
 *
 * Hardware connections:
 *   ESP32-S3 N16R8 GPIO -> TXS0108E level shifter -> TI-99/4A keyboard connector
 *
 *   Two TXS0108E boards sit along the left edge of the ESP32-S3 N16R8
 *   dev board. TXS#1 uses GPIO 4-18, TXS#2 uses GPIO 9-14. GPIO 46
 *   (LOG pin) sits in the gap between them and is unused.
 *
 *   Each TXS0108E OE pin must be jumpered to ESP32 3V3 to enable.
 *   Do NOT wire OE to RST.
 *
 *   TI keyboard connector wiring (15-pin header):
 *
 *     TI pin  Signal   TXS    Ch    GPIO  Direction
 *     ------  -------  -----  ----  ----  ----------------
 *       1     INT5     TXS#1  B8    4     ESP32 -> TI (row)
 *       2     INT6     TXS#1  B7    5     ESP32 -> TI (row)
 *       3     INT8     TXS#1  B6    6     ESP32 -> TI (row)
 *       4     INT4     TXS#1  B5    7     ESP32 -> TI (row)
 *       5     INT3     TXS#1  B4    15    ESP32 -> TI (row)
 *       6     P5       ---    ---   ---   (nc, software alpha lock)
 *       7     INT7     TXS#1  B3    16    ESP32 -> TI (row)
 *       8     1Y1      TXS#1  B2    17    TI -> ESP32 (col 0)
 *       9     1Y0      TXS#1  B1    18    TI -> ESP32 (col 4)
 *      10     INT9     TXS#2  B8    9     ESP32 -> TI (row)
 *      11     INT10    TXS#2  B7    10    ESP32 -> TI (row)
 *      12     2Y0      TXS#2  B6    11    TI -> ESP32 (col 5)
 *      13     2Y1      TXS#2  B5    12    TI -> ESP32 (col 1)
 *      14     2Y2      TXS#2  B4    13    TI -> ESP32 (col 2)
 *      15     2Y3      TXS#2  B3    14    TI -> ESP32 (col 3)
 *
 * Board settings (Arduino IDE):
 *   Board: "ESP32S3 Dev Module"
 *   USB Mode: "USB-OTG (TinyUSB)" (if using USB input)
 *   USB CDC On Boot: "Disabled" (use UART port for serial monitor)
 *
 * Libraries required:
 *   - EspUsbHost by tanakamasayuki (for USB input)
 *   - BLE uses the built-in ESP32 BLE library (Bluedroid stack)
 */

// ---------------------------------------------------------------------------
// Input Mode Selection — uncomment one or both
// ---------------------------------------------------------------------------
#define INPUT_USB
#define INPUT_BLE

#ifdef INPUT_USB
#include <EspUsbHost.h>
#endif

#ifdef INPUT_BLE
#include <BleHidHost.h>
#endif

// ---------------------------------------------------------------------------
// Status LED (onboard WS2812 RGB LED on GPIO 48)
// ---------------------------------------------------------------------------
#define PIN_LED        48
#define LED_BRIGHTNESS 20

enum LedState
{
  LED_STARTUP,
  LED_SCANNING,
  LED_CONNECTED,
  LED_KEYPRESS,
  LED_ERROR,
  LED_NO_INPUT
};

static LedState currentLedState = LED_STARTUP;
static unsigned long ledStateTime = 0;
static unsigned long keypressTime = 0;

void setLed(uint8_t r, uint8_t g, uint8_t b)
{
  rgbLedWrite(PIN_LED, r, g, b);
}

void setLedState(LedState state)
{
  if (state != currentLedState)
  {
    currentLedState = state;
    ledStateTime = millis();
  }
}

void updateLed()
{
  unsigned long now = millis();
  int pulse = (sin(now / 300.0) + 1.0) * LED_BRIGHTNESS / 2;

  if (now - keypressTime < 100)
  {
    setLed(0, LED_BRIGHTNESS * 3, 0);
    return;
  }

  switch (currentLedState)
  {
    case LED_STARTUP:
      setLed(0, 0, pulse);
      break;
    case LED_SCANNING:
      setLed(pulse, 0, pulse);
      break;
    case LED_CONNECTED:
      setLed(0, LED_BRIGHTNESS / 4, 0);
      break;
    case LED_KEYPRESS:
      setLed(0, LED_BRIGHTNESS * 3, 0);
      break;
    case LED_ERROR:
      if ((now / 250) % 2)
      {
        setLed(LED_BRIGHTNESS, 0, 0);
      }
      else
      {
        setLed(0, 0, 0);
      }
      break;
    case LED_NO_INPUT:
      setLed(LED_BRIGHTNESS / 4, LED_BRIGHTNESS / 6, 0);
      break;
  }
}

// ---------------------------------------------------------------------------
// GPIO Pin Assignments
// ---------------------------------------------------------------------------
// Both TXS0108E boards sit along the left edge of the ESP32-S3 N16R8 dev
// board. TXS#1 uses GPIO 4-18 (8 channels), TXS#2 uses GPIO 9-14
// (6 channels). GPIO 46 (LOG pin) sits in the gap and is unused.
//
// ESP32 -> TXS#1:  GPIO4=A8, 5=A7, 6=A6, 7=A5, 15=A4, 16=A3, 17=A2, 18=A1
// ESP32 -> TXS#2:  GPIO9=A8, 10=A7, 11=A6, 12=A5, 13=A4, 14=A3
//
// TXS#1 B-side -> TI: B8=pin1, B7=pin2, B6=pin3, B5=pin4, B4=pin5,
//                      B3=pin7, B2=pin8, B1=pin9   (pin6=P5 nc)
// TXS#2 B-side -> TI: B8=pin10, B7=pin11, B6=pin12, B5=pin13,
//                      B4=pin14, B3=pin15

// Row outputs — ESP32 -> TI via TXS#1 and TXS#2
#define PIN_ROW_INT5   4   // TXS#1 A8/B8 -> TI pin 1
#define PIN_ROW_INT6   5   // TXS#1 A7/B7 -> TI pin 2
#define PIN_ROW_INT8   6   // TXS#1 A6/B6 -> TI pin 3
#define PIN_ROW_INT4   7   // TXS#1 A5/B5 -> TI pin 4
#define PIN_ROW_INT3  15   // TXS#1 A4/B4 -> TI pin 5
#define PIN_ROW_INT7  16   // TXS#1 A3/B3 -> TI pin 7
#define PIN_ROW_INT9   9   // TXS#2 A8/B8 -> TI pin 10
#define PIN_ROW_INT10 10   // TXS#2 A7/B7 -> TI pin 11

// Column inputs — TI -> ESP32 via TXS#1 and TXS#2
#define PIN_COL_1Y1   17   // TXS#1 A2/B2 -> TI pin 8  (col 0)
#define PIN_COL_1Y0   18   // TXS#1 A1/B1 -> TI pin 9  (col 4)
#define PIN_COL_2Y0   11   // TXS#2 A6/B6 -> TI pin 12 (col 5)
#define PIN_COL_2Y1   12   // TXS#2 A5/B5 -> TI pin 13 (col 1)
#define PIN_COL_2Y2   13   // TXS#2 A4/B4 -> TI pin 14 (col 2)
#define PIN_COL_2Y3   14   // TXS#2 A3/B3 -> TI pin 15 (col 3)

static const int colPins[6] =
{
  PIN_COL_1Y1, PIN_COL_2Y1, PIN_COL_2Y2,
  PIN_COL_2Y3, PIN_COL_1Y0, PIN_COL_2Y0
};

static const int rowPins[8] =
{
  PIN_ROW_INT3, PIN_ROW_INT4, PIN_ROW_INT5, PIN_ROW_INT6,
  PIN_ROW_INT10, PIN_ROW_INT8, PIN_ROW_INT9, PIN_ROW_INT7
};

// ---------------------------------------------------------------------------
// TI-99/4A Keyboard Matrix
// ---------------------------------------------------------------------------
// Column index: 0=1Y1, 1=2Y1, 2=2Y2, 3=2Y3, 4=1Y0, 5=2Y0
// Row index:    0=INT3, 1=INT4, 2=INT5, 3=INT6, 4=INT10, 5=INT8, 6=INT9, 7=INT7
//
// Col 0 (1Y1): =  ;  P  0  Z  A  Q  1
// Col 1 (2Y1): .  L  O  9  X  S  W  2
// Col 2 (2Y2): ,  K  I  8  C  D  E  3
// Col 3 (2Y3): M  J  U  7  V  F  R  4
// Col 4 (1Y0): N  H  Y  6  B  G  T  5
// Col 5 (2Y0): (=) SPC ENT  -  -  SHIFT CTRL FCTN

// ---------------------------------------------------------------------------
// Key State (shared between USB and BLE input)
// ---------------------------------------------------------------------------
static volatile uint8_t keyState[6] = {0, 0, 0, 0, 0, 0};
static volatile bool alphaLockActive = false;

// ---------------------------------------------------------------------------
// HID Scancode Definitions
// ---------------------------------------------------------------------------
#define HID_KEY_A          0x04
#define HID_KEY_B          0x05
#define HID_KEY_C          0x06
#define HID_KEY_D          0x07
#define HID_KEY_E          0x08
#define HID_KEY_F          0x09
#define HID_KEY_G          0x0A
#define HID_KEY_H          0x0B
#define HID_KEY_I          0x0C
#define HID_KEY_J          0x0D
#define HID_KEY_K          0x0E
#define HID_KEY_L          0x0F
#define HID_KEY_M          0x10
#define HID_KEY_N          0x11
#define HID_KEY_O          0x12
#define HID_KEY_P          0x13
#define HID_KEY_Q          0x14
#define HID_KEY_R          0x15
#define HID_KEY_S          0x16
#define HID_KEY_T          0x17
#define HID_KEY_U          0x18
#define HID_KEY_V          0x19
#define HID_KEY_W          0x1A
#define HID_KEY_X          0x1B
#define HID_KEY_Y          0x1C
#define HID_KEY_Z          0x1D
#define HID_KEY_1          0x1E
#define HID_KEY_2          0x1F
#define HID_KEY_3          0x20
#define HID_KEY_4          0x21
#define HID_KEY_5          0x22
#define HID_KEY_6          0x23
#define HID_KEY_7          0x24
#define HID_KEY_8          0x25
#define HID_KEY_9          0x26
#define HID_KEY_0          0x27
#define HID_KEY_ENTER      0x28
#define HID_KEY_ESCAPE     0x29
#define HID_KEY_BACKSPACE  0x2A
#define HID_KEY_TAB        0x2B
#define HID_KEY_SPACE      0x2C
#define HID_KEY_MINUS      0x2D
#define HID_KEY_EQUAL      0x2E
#define HID_KEY_SEMICOLON  0x33
#define HID_KEY_COMMA      0x36
#define HID_KEY_PERIOD     0x37
#define HID_KEY_SLASH      0x38
#define HID_KEY_CAPSLOCK   0x39
#define HID_KEY_F1         0x3A
#define HID_KEY_F2         0x3B
#define HID_KEY_F3         0x3C
#define HID_KEY_F4         0x3D
#define HID_KEY_F5         0x3E
#define HID_KEY_F6         0x3F
#define HID_KEY_F7         0x40
#define HID_KEY_F8         0x41
#define HID_KEY_F9         0x42
#define HID_KEY_F10        0x43
#define HID_KEY_F11        0x44
#define HID_KEY_F12        0x45
#define HID_KEY_RIGHT      0x4F
#define HID_KEY_LEFT       0x50
#define HID_KEY_DOWN       0x51
#define HID_KEY_UP         0x52
#define HID_KEY_DELETE     0x4C

#define HID_MOD_LCTRL   0x01
#define HID_MOD_LSHIFT  0x02
#define HID_MOD_LALT    0x04
#define HID_MOD_RCTRL   0x10
#define HID_MOD_RSHIFT  0x20
#define HID_MOD_RALT    0x40

// Row bitmasks
#define ROW0  0x01  // INT3
#define ROW1  0x02  // INT4
#define ROW2  0x04  // INT5
#define ROW3  0x08  // INT6
#define ROW4  0x10  // INT10
#define ROW5  0x20  // INT8
#define ROW6  0x40  // INT9
#define ROW7  0x80  // INT7

// TI modifier key positions (column 5 = 2Y0)
#define TI_SHIFT_COL  5
#define TI_SHIFT_ROW  ROW5
#define TI_CTRL_COL   5
#define TI_CTRL_ROW   ROW6
#define TI_FCTN_COL   5
#define TI_FCTN_ROW   ROW7

// ---------------------------------------------------------------------------
// HID Scancode to TI Matrix Lookup Table
// ---------------------------------------------------------------------------
typedef struct
{
  uint8_t col;
  uint8_t row;
} TiKeyMapping;

#define MAP_SIZE 0x53
static const TiKeyMapping hidToTi[MAP_SIZE] =
{
  {0xFF, 0}, {0xFF, 0}, {0xFF, 0}, {0xFF, 0},       // 0x00-0x03: reserved
  {0, ROW5},  {4, ROW4},  {2, ROW4},  {2, ROW5},    // A B C D
  {2, ROW6},  {3, ROW5},  {4, ROW5},  {4, ROW1},    // E F G H
  {2, ROW2},  {3, ROW1},  {2, ROW1},  {1, ROW1},    // I J K L
  {3, ROW0},  {4, ROW0},  {1, ROW2},  {0, ROW2},    // M N O P
  {0, ROW6},  {3, ROW6},  {1, ROW5},  {4, ROW6},    // Q R S T
  {3, ROW2},  {3, ROW4},  {1, ROW6},  {1, ROW4},    // U V W X
  {4, ROW2},  {0, ROW4},                              // Y Z
  {0, ROW7},  {1, ROW7},  {2, ROW7},  {3, ROW7},    // 1 2 3 4
  {4, ROW7},  {4, ROW3},  {3, ROW3},  {2, ROW3},    // 5 6 7 8
  {1, ROW3},  {0, ROW3},                              // 9 0
  {5, ROW2},                                           // Enter
  {0xFF, 0}, {0xFF, 0}, {0xFF, 0},                    // Esc, BS, Tab (special)
  {5, ROW1},                                           // Space
  {0xFF, 0},                                           // Minus (special)
  {0, ROW0},                                           // Equal
  {0xFF, 0}, {0xFF, 0}, {0xFF, 0}, {0xFF, 0},         // brackets, etc.
  {0, ROW1},                                           // Semicolon
  {0xFF, 0}, {0xFF, 0},                                // apostrophe, grave
  {2, ROW0},                                           // Comma
  {1, ROW0},                                           // Period
  {0xFF, 0},                                           // Slash (special)
  {0xFF, 0},                                           // Caps Lock (special)
  {0xFF, 0}, {0xFF, 0}, {0xFF, 0}, {0xFF, 0}, {0xFF, 0},  // F1-F5
  {0xFF, 0}, {0xFF, 0}, {0xFF, 0}, {0xFF, 0}, {0xFF, 0},  // F6-F10
  {0xFF, 0}, {0xFF, 0}, {0xFF, 0}, {0xFF, 0}, {0xFF, 0},  // F11-misc
  {0xFF, 0}, {0xFF, 0}, {0xFF, 0}, {0xFF, 0}, {0xFF, 0}, {0xFF, 0},
  {0xFF, 0}, {0xFF, 0}, {0xFF, 0}, {0xFF, 0},             // arrows (special)
};

// ---------------------------------------------------------------------------
// Special Key Combos (require FCTN or SHIFT + another key)
// ---------------------------------------------------------------------------
typedef struct
{
  uint8_t hidKey;
  uint8_t tiCol;
  uint8_t tiRow;
  bool    needFctn;
  bool    needShift;
} SpecialKeyMapping;

static const SpecialKeyMapping specialKeys[] =
{
  {HID_KEY_UP,        2, ROW6, true, false},  // FCTN+E
  {HID_KEY_DOWN,      1, ROW4, true, false},  // FCTN+X
  {HID_KEY_LEFT,      1, ROW5, true, false},  // FCTN+S
  {HID_KEY_RIGHT,     2, ROW5, true, false},  // FCTN+D
  {HID_KEY_ESCAPE,    1, ROW3, true, false},  // FCTN+9 (BACK)
  {HID_KEY_BACKSPACE, 1, ROW5, true, false},  // FCTN+S (delete)
  {HID_KEY_TAB,       3, ROW3, true, false},  // FCTN+7
  {HID_KEY_DELETE,    0, ROW7, true, false},  // FCTN+1
  {HID_KEY_SLASH,     2, ROW2, true, false},  // FCTN+I
  {HID_KEY_MINUS,     3, ROW2, true, false},  // FCTN+U
  {HID_KEY_F1,        0, ROW7, true, false},  // FCTN+1
  {HID_KEY_F2,        1, ROW7, true, false},  // FCTN+2
  {HID_KEY_F3,        2, ROW7, true, false},  // FCTN+3
  {HID_KEY_F4,        3, ROW7, true, false},  // FCTN+4
  {HID_KEY_F5,        4, ROW7, true, false},  // FCTN+5
  {HID_KEY_F6,        4, ROW3, true, false},  // FCTN+6
  {HID_KEY_F7,        3, ROW3, true, false},  // FCTN+7
  {HID_KEY_F8,        2, ROW3, true, false},  // FCTN+8
  {HID_KEY_F9,        1, ROW3, true, false},  // FCTN+9
  {HID_KEY_F10,       0, ROW3, true, false},  // FCTN+0
};
#define NUM_SPECIAL_KEYS (sizeof(specialKeys) / sizeof(specialKeys[0]))

// ---------------------------------------------------------------------------
// Shared HID Report Processing
// ---------------------------------------------------------------------------
static uint8_t prevKeys[6] = {0};

// Debug helper: convert a HID scancode + effective shift state into a
// human-readable character (or label) for the matrix debug print.
static const char *hidKeyToDebugChar(uint8_t k, bool shift)
{
  static char buf[2] = {0, 0};

  // Letters
  if (k >= HID_KEY_A && k <= HID_KEY_Z)
  {
    buf[0] = shift ? ('A' + (k - HID_KEY_A)) : ('a' + (k - HID_KEY_A));
    return buf;
  }

  // Top-row numbers and their shifted symbols (US layout)
  static const char numUnshift[] = "1234567890";
  static const char numShift[]   = "!@#$%^&*()";
  if (k >= HID_KEY_1 && k <= HID_KEY_0)
  {
    buf[0] = shift ? numShift[k - HID_KEY_1] : numUnshift[k - HID_KEY_1];
    return buf;
  }

  // Punctuation and common control keys
  switch (k)
  {
    case 0x28: return "<ENTER>";
    case 0x29: return "<ESC>";
    case 0x2A: return "<BKSP>";
    case 0x2B: return "<TAB>";
    case 0x2C: return " ";
    case 0x2D: buf[0] = shift ? '_' : '-'; return buf;
    case 0x2E: buf[0] = shift ? '+' : '='; return buf;
    case 0x2F: buf[0] = shift ? '{' : '['; return buf;
    case 0x30: buf[0] = shift ? '}' : ']'; return buf;
    case 0x31: buf[0] = shift ? '|' : '\\'; return buf;
    case 0x33: buf[0] = shift ? ':' : ';'; return buf;
    case 0x34: buf[0] = shift ? '"' : '\''; return buf;
    case 0x35: buf[0] = shift ? '~' : '`'; return buf;
    case 0x36: buf[0] = shift ? '<' : ','; return buf;
    case 0x37: buf[0] = shift ? '>' : '.'; return buf;
    case 0x38: buf[0] = shift ? '?' : '/'; return buf;
    case 0x39: return "<CAPS>";
    case 0x4F: return "<RIGHT>";
    case 0x50: return "<LEFT>";
    case 0x51: return "<DOWN>";
    case 0x52: return "<UP>";
  }

  // Function keys F1..F12. F1..F10 become FCTN+1..FCTN+0 on the TI;
  // F12 triggers BLE pairing mode and is not forwarded to the TI.
  if (k >= HID_KEY_F1 && k <= HID_KEY_F12)
  {
    static char fbuf[4];
    int n = (k - HID_KEY_F1) + 1;  // F1..F12
    if (n < 10)
    {
      fbuf[0] = 'F';
      fbuf[1] = '0' + n;
      fbuf[2] = 0;
    }
    else
    {
      fbuf[0] = 'F';
      fbuf[1] = '1';
      fbuf[2] = '0' + (n - 10);
      fbuf[3] = 0;
    }
    return fbuf;
  }

  return "?";
}

void processHidReport(const uint8_t *report, size_t len)
{
  if (len < 8)
  {
    return;
  }

  uint8_t modifiers = report[0];
  const uint8_t *keys = &report[2];

#ifdef INPUT_BLE
  // F12 → enter BLE pairing mode (edge-triggered, not forwarded to TI).
  // Flag is consumed on the main loop; calling BLE stack functions from a
  // notify callback context is unsafe.
  bool f12Now = false;
  bool f12Prev = false;
  for (int i = 0; i < 6; i++)
  {
    if (keys[i] == HID_KEY_F12) f12Now = true;
    if (prevKeys[i] == HID_KEY_F12) f12Prev = true;
  }
  if (f12Now && !f12Prev)
  {
    BleHidHost::requestPairingMode();
  }
#endif

  // Clear all key state and rebuild from current report
  memset((void *)keyState, 0, sizeof(keyState));

  // Modifier keys
  if (modifiers & (HID_MOD_LSHIFT | HID_MOD_RSHIFT))
  {
    keyState[TI_SHIFT_COL] |= TI_SHIFT_ROW;
  }
  if (modifiers & (HID_MOD_LCTRL | HID_MOD_RCTRL))
  {
    keyState[TI_CTRL_COL] |= TI_CTRL_ROW;
  }
  if (modifiers & (HID_MOD_LALT | HID_MOD_RALT))
  {
    keyState[TI_FCTN_COL] |= TI_FCTN_ROW;
  }

  // Process each pressed key
  for (int i = 0; i < 6; i++)
  {
    uint8_t k = keys[i];
    if (k == 0)
    {
      continue;
    }

    // Check special keys first
    bool handled = false;
    for (int s = 0; s < NUM_SPECIAL_KEYS; s++)
    {
      if (specialKeys[s].hidKey == k)
      {
        keyState[specialKeys[s].tiCol] |= specialKeys[s].tiRow;
        if (specialKeys[s].needFctn)
        {
          keyState[TI_FCTN_COL] |= TI_FCTN_ROW;
        }
        if (specialKeys[s].needShift)
        {
          keyState[TI_SHIFT_COL] |= TI_SHIFT_ROW;
        }
        handled = true;
        break;
      }
    }
    if (handled)
    {
      continue;
    }

    // Caps Lock toggle (edge-triggered)
    if (k == HID_KEY_CAPSLOCK)
    {
      bool wasPressed = false;
      for (int j = 0; j < 6; j++)
      {
        if (prevKeys[j] == HID_KEY_CAPSLOCK)
        {
          wasPressed = true;
          break;
        }
      }
      if (!wasPressed)
      {
        alphaLockActive = !alphaLockActive;
      }
      continue;
    }

    // Standard key lookup
    if (k < MAP_SIZE && hidToTi[k].col != 0xFF)
    {
      keyState[hidToTi[k].col] |= hidToTi[k].row;

      // Software Alpha Lock: force SHIFT on letters when caps lock is on.
      // We never drive the dedicated Alpha Lock line — that line shares
      // the joystick UP signal on the original TI, and asserting it
      // permanently breaks joystick reads. By emulating Alpha Lock as
      // "letters get shifted," we get capital letters without ever
      // touching PIN_ALPHA_LOCK, so joysticks keep working.
      if (alphaLockActive && k >= HID_KEY_A && k <= HID_KEY_Z)
      {
        keyState[TI_SHIFT_COL] |= TI_SHIFT_ROW;
      }
    }
  }

  // LED feedback
  bool anyKeyPressed = false;
  for (int i = 0; i < 6; i++)
  {
    if (keyState[i] != 0)
    {
      anyKeyPressed = true;
      break;
    }
  }
  if (anyKeyPressed)
  {
    keypressTime = millis();
  }

  // DEBUG: print TI matrix state when it changes
  static uint8_t lastPrintedState[6] = {0, 0, 0, 0, 0, 0};
  static bool lastPrintedAlpha = false;
  if (memcmp((const void *)keyState, lastPrintedState, 6) != 0 ||
      alphaLockActive != lastPrintedAlpha)
  {
    // Find the primary (first non-zero) HID key in the current report
    uint8_t primaryKey = 0;
    for (int i = 0; i < 6; i++)
    {
      if (keys[i] != 0 && keys[i] != HID_KEY_CAPSLOCK)
      {
        primaryKey = keys[i];
        break;
      }
    }

    // Compute effective shift: physical shift OR alpha-lock-on-letter
    bool shiftHeld = (modifiers & (HID_MOD_LSHIFT | HID_MOD_RSHIFT)) != 0;
    bool effectiveShift = shiftHeld;
    if (alphaLockActive && primaryKey >= HID_KEY_A && primaryKey <= HID_KEY_Z)
    {
      effectiveShift = true;
    }

    const char *charStr = (primaryKey == 0)
                            ? "(release)"
                            : hidKeyToDebugChar(primaryKey, effectiveShift);

    // Pad the key label to a fixed width so the matrix bytes always
    // start in the same column. Longest expected label is "(release)"
    // at 9 characters; everything else is shorter and gets right-padded.
    Serial.printf("TI key %-9s  C0=%02X C1=%02X C2=%02X C3=%02X C4=%02X C5=%02X  alpha=%d\n",
                  charStr,
                  keyState[0], keyState[1], keyState[2],
                  keyState[3], keyState[4], keyState[5],
                  alphaLockActive ? 1 : 0);
    memcpy(lastPrintedState, (const void *)keyState, 6);
    lastPrintedAlpha = alphaLockActive;
  }

  memcpy(prevKeys, keys, 6);
}

// ---------------------------------------------------------------------------
// USB Host Input
// ---------------------------------------------------------------------------
#ifdef INPUT_USB

class TiUsbHost : public EspUsbHost
{
public:
  void onKeyboardKey(uint8_t ascii, uint8_t keycode, uint8_t modifier) override
  {
  }

  void onReceive(const usb_transfer_t *transfer) override
  {
    processHidReport(transfer->data_buffer, transfer->actual_num_bytes);
  }
};

static TiUsbHost usbHost;

#endif // INPUT_USB

// ---------------------------------------------------------------------------
// BLE HID Host Input (Bluedroid)
// ---------------------------------------------------------------------------
#ifdef INPUT_BLE

// HID reports arrive via BleHidHost's callback (notify context). Forward
// straight into the shared processHidReport, which builds the TI matrix.
static void bleOnHidReport(const uint8_t *data, size_t len)
{
  processHidReport(data, len);
}

// Track BleHidHost state transitions so the LED and TI matrix stay in sync.
// We poll once per loop() rather than embed hooks in the BLE callbacks,
// since those run in BLE stack contexts.
static void bleUpdateLedAndState()
{
  static bool wasConnected = false;
  static bool wasPairing = false;

  bool connected = BleHidHost::isReady();
  bool pairing   = BleHidHost::inPairingMode();

  // Edge: just disconnected — clear TI matrix so no ghost key stays held
  if (wasConnected && !connected)
  {
    memset((void *)keyState, 0, sizeof(keyState));
  }

  // Drive LED state from BLE state
  if (connected)
  {
    setLedState(LED_CONNECTED);
  }
  else if (pairing)
  {
    setLedState(LED_STARTUP);
  }
  else
  {
    setLedState(LED_SCANNING);
  }

  wasConnected = connected;
  wasPairing   = pairing;
}

static void bleInit()
{
  BleHidHost::setReportCallback(bleOnHidReport);
  BleHidHost::begin("TI99-KB", "ti99kb");
}

static void bleTask()
{
  BleHidHost::task();
  bleUpdateLedAndState();
}

#endif // INPUT_BLE

// ---------------------------------------------------------------------------
// TI-99/4A Matrix Output
// ---------------------------------------------------------------------------
static inline void updateRowOutputs()
{
  int activeCol = -1;
  for (int c = 0; c < 6; c++)
  {
    if (digitalRead(colPins[c]) == LOW)
    {
      activeCol = c;
      break;
    }
  }

  if (activeCol >= 0)
  {
    uint8_t rows = keyState[activeCol];
    for (int r = 0; r < 8; r++)
    {
      if (rows & (1 << r))
      {
        pinMode(rowPins[r], OUTPUT);
        digitalWrite(rowPins[r], LOW);
      }
      else
      {
        pinMode(rowPins[r], INPUT);
      }
    }
  }
  else
  {
    for (int r = 0; r < 8; r++)
    {
      pinMode(rowPins[r], INPUT);
    }
  }

  // PIN_ALPHA_LOCK is intentionally never driven. See processHidReport
  // for the software Alpha Lock implementation. Driving the original
  // Alpha Lock line breaks joystick UP on unmodified TI-99/4A consoles.
}

// ---------------------------------------------------------------------------
// Serial Debug Mode
// ---------------------------------------------------------------------------
// Type a 15-digit binary number (e.g. "110000001000000") into the serial
// monitor to directly drive all 15 TI connector pins. Bit order matches
// the TI keyboard connector pin numbering:
//
//   Bit 1  (leftmost)  = TI pin 1  (INT5,  row, GPIO 4)
//   Bit 2              = TI pin 2  (INT6,  row, GPIO 5)
//   Bit 3              = TI pin 3  (INT8,  row, GPIO 6)
//   Bit 4              = TI pin 4  (INT4,  row, GPIO 7)
//   Bit 5              = TI pin 5  (INT3,  row, GPIO 15)
//   Bit 6              = TI pin 6  (P5,    nc — ignored)
//   Bit 7              = TI pin 7  (INT7,  row, GPIO 16)
//   Bit 8              = TI pin 8  (1Y1,   col, GPIO 17)
//   Bit 9              = TI pin 9  (1Y0,   col, GPIO 18)
//   Bit 10             = TI pin 10 (INT9,  row, GPIO 9)
//   Bit 11             = TI pin 11 (INT10, row, GPIO 10)
//   Bit 12             = TI pin 12 (2Y0,   col, GPIO 11)
//   Bit 13             = TI pin 13 (2Y1,   col, GPIO 12)
//   Bit 14             = TI pin 14 (2Y2,   col, GPIO 13)
//   Bit 15 (rightmost) = TI pin 15 (2Y3,   col, GPIO 14)
//
// A '1' drives the pin LOW (active), a '0' releases it (high-Z / input).
// Type "off" or "reset" to release all pins and return to normal mode.

static const int debugPinMap[15] =
{
  PIN_ROW_INT5,   // TI pin 1
  PIN_ROW_INT6,   // TI pin 2
  PIN_ROW_INT8,   // TI pin 3
  PIN_ROW_INT4,   // TI pin 4
  PIN_ROW_INT3,   // TI pin 5
  -1,             // TI pin 6  (P5, not connected)
  PIN_ROW_INT7,   // TI pin 7
  PIN_COL_1Y1,    // TI pin 8
  PIN_COL_1Y0,    // TI pin 9
  PIN_ROW_INT9,   // TI pin 10
  PIN_ROW_INT10,  // TI pin 11
  PIN_COL_2Y0,    // TI pin 12
  PIN_COL_2Y1,    // TI pin 13
  PIN_COL_2Y2,    // TI pin 14
  PIN_COL_2Y3,    // TI pin 15
};

static bool debugMode = false;
static bool cycleMode = false;
static unsigned long cycleLastTime = 0;
static int cyclePinIndex = 0;

static const char* debugPinNames[15] =
{
  "INT5 ", "INT6 ", "INT8 ", "INT4 ", "INT3 ",
  "P5   ", "INT7 ", "1Y1  ", "1Y0  ",
  "INT9 ", "INT10", "2Y0  ", "2Y1  ", "2Y2  ", "2Y3  "
};

void processCycleMode()
{
  if (!cycleMode)
  {
    return;
  }

  unsigned long now = millis();
  if (now - cycleLastTime < 1000)
  {
    return;
  }
  cycleLastTime = now;

  // Release previous pin
  int prevIndex = (cyclePinIndex == 0) ? 14 : cyclePinIndex - 1;
  // Find previous valid pin
  for (int i = 0; i < 14; i++)
  {
    if (debugPinMap[prevIndex] >= 0)
    {
      pinMode(debugPinMap[prevIndex], INPUT);
      break;
    }
    prevIndex = (prevIndex == 0) ? 14 : prevIndex - 1;
  }

  // Skip pin 6 (P5, not connected)
  if (debugPinMap[cyclePinIndex] < 0)
  {
    cyclePinIndex = (cyclePinIndex + 1) % 15;
  }

  // Drive current pin LOW
  int pin = debugPinMap[cyclePinIndex];
  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);
  Serial.printf("CYCLE: TI pin %2d (%s) -> GPIO %d LOW\n",
                cyclePinIndex + 1, debugPinNames[cyclePinIndex], pin);

  cyclePinIndex = (cyclePinIndex + 1) % 15;
}

void processSerialDebug()
{
  // Run cycle mode each loop iteration
  processCycleMode();

  if (!Serial.available())
  {
    return;
  }

  String input = Serial.readStringUntil('\n');
  input.trim();

  if (input.length() == 0)
  {
    return;
  }

  // Release all pins and exit debug/cycle mode
  if (input.equalsIgnoreCase("off") || input.equalsIgnoreCase("reset"))
  {
    for (int i = 0; i < 15; i++)
    {
      if (debugPinMap[i] >= 0)
      {
        pinMode(debugPinMap[i], INPUT);
      }
    }
    debugMode = false;
    cycleMode = false;
    Serial.println("DEBUG: All pins released. Normal mode.");
    return;
  }

  // Cycle test mode
  if (input.equalsIgnoreCase("cycle"))
  {
    debugMode = true;
    cycleMode = true;
    cyclePinIndex = 0;
    cycleLastTime = 0;
    Serial.println("CYCLE: Toggling each TI pin for 1 second. Type 'off' to stop.");
    return;
  }

  // Expect a 15-digit binary string
  if (input.length() != 15)
  {
    Serial.println("DEBUG: Enter 15 binary digits (e.g. 110000001000000)");
    Serial.println("       or 'cycle' to cycle through all pins.");
    Serial.println("       or 'off' to release all pins.");
    return;
  }

  cycleMode = false;
  debugMode = true;
  Serial.printf("DEBUG: Setting pins: %s\n", input.c_str());
  Serial.printf("       TI pins:  ");

  for (int i = 0; i < 15; i++)
  {
    int pin = debugPinMap[i];
    if (pin < 0)
    {
      Serial.printf("-- ");
      continue;
    }

    if (input.charAt(i) == '1')
    {
      pinMode(pin, OUTPUT);
      digitalWrite(pin, LOW);
      Serial.printf("LO ");
    }
    else
    {
      pinMode(pin, INPUT);
      Serial.printf("HI ");
    }
  }
  Serial.println();
}

// ---------------------------------------------------------------------------
// Setup and Main Loop
// ---------------------------------------------------------------------------
void setup()
{
  Serial.begin(115200);
  setLed(0, 0, LED_BRIGHTNESS);
  setLedState(LED_STARTUP);

  Serial.println("TI-99/4A Keyboard Adapter");
  Serial.println("=========================");

  for (int c = 0; c < 6; c++)
  {
    pinMode(colPins[c], INPUT_PULLUP);
  }

  for (int r = 0; r < 8; r++)
  {
    pinMode(rowPins[r], INPUT);
  }

#ifdef INPUT_USB
  Serial.println("USB: Initializing...");
  usbHost.begin();
  Serial.println("USB: Ready.");
#endif

#ifdef INPUT_BLE
  Serial.println("BLE: Initializing...");
  bleInit();
#endif
}

void loop()
{
  processSerialDebug();

  if (!debugMode)
  {
#ifdef INPUT_USB
    usbHost.task();
#endif

#ifdef INPUT_BLE
    bleTask();
#endif

    updateRowOutputs();
  }

  updateLed();
}
