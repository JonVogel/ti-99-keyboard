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
#define INPUT_USB  // USB keyboard via USB-OTG host port (EspUsbHost)
#define INPUT_BLE

// ---------------------------------------------------------------------------
// Type-ahead Buffer
// ---------------------------------------------------------------------------
// The original TI-99/4A has no keyboard buffer — keys typed faster than the
// console's matrix scan get dropped. The IBM PC's BIOS solved this with a
// 16-key type-ahead buffer that captured keystrokes at interrupt time and
// fed them to the console at scan rate. The same trick is implemented here:
// HID key-down events get queued (with their modifiers snapshotted at press
// time), then a replay state machine holds each one on the TI matrix long
// enough for the ROM to definitely see it before serving the next.
//
// The buffer breaks games that depend on "is this key DOWN right now?"
// (Parsec hold-to-fire, TI Invaders movement, etc.) — held keys turn into
// 40ms pulses, multi-key chords get serialized. So the buffer is OFF by
// default and the user toggles it on/off at runtime with F11. Compile-flag
// gates the whole feature in case the runtime path proves problematic on
// real hardware.
#define ENABLE_TYPE_AHEAD_BUFFER

// Tunables. HOLD_MS must cover at least 2 TI keyboard scan cycles so the
// ROM is guaranteed to sample the matrix while the key is asserted.
// GAP_MS gives the ROM a chance to see the release between consecutive
// presses of the same key (otherwise back-to-back AAA looks like one A).
#define TYPE_AHEAD_DEPTH    16
#define TYPE_AHEAD_HOLD_MS  40
#define TYPE_AHEAD_GAP_MS   10

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

// True while a USB keyboard is enumerated on the host port. EspUsbHost's
// usbTransferSize goes >0 on enumerate and back to 0 on unplug, so we mirror
// it each loop. Drives the status LED orange so it's obvious USB is the live
// input. Stays false when INPUT_USB is compiled out.
static bool usbInUse = false;

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

  // USB keyboard connected → steady orange, overriding the BLE-driven states
  // below. (Keypress flash above still wins momentarily for feedback.)
  if (usbInUse)
  {
    setLed(LED_BRIGHTNESS, LED_BRIGHTNESS / 4, 0);
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

// ---------------------------------------------------------------------------
// v4 straight-cable GPIO remap  (rev-3 twisted-ribbon baseline is tagged v3)
// ---------------------------------------------------------------------------
// The 1x15 keyboard ribbon reverses pin order end-to-end because of the
// connector orientation (J10 pin p <-> TI-motherboard pin 16-p), so rev 1-3
// only worked with the ribbon TWISTED 180 degrees. v4 targets a plain
// STRAIGHT (untwisted) ribbon.
//
// The 14 driven/read matrix lines are remapped here in firmware: each signal
// now uses the GPIO whose J10 pin mirrors (16-p) to the correct TI pin. It's
// a pure relabel -- the matrix tables further down are unchanged.
//
// Alpha-lock (TI pin 6, J10 pin 10) is no longer a bare pass-through: the v4
// board gives it a level-shifter channel and GPIO9, so EVERY TI line is now
// reachable from firmware -- future re-pinning never needs a new layout. The
// firmware still never drives it (software alpha lock; driving the line
// breaks joystick UP), but the copper is there.
//
// v4 board channel fabric (GPIO -> BOB channel -> J10 pin), per Jon's map:
//   BOB1: GPIO4-7   -> CH4..CH1 -> J10 1-4
//   BOB2: GPIO15-18 -> CH4..CH1 -> J10 5-8
//   BOB3: GPIO8-11  -> CH4..CH1 -> J10 9-12   (GPIO9 = alpha, J10 10)
//   BOB4: GPIO12-14 -> CH4..CH2 -> J10 13-15  (CH1 spare)
//
// WARNING: do NOT flash this build onto a rev-3 board with a twisted ribbon;
// the double reversal scrambles the matrix.

// Row outputs — ESP32 -> TI  (GPIO -> J10 pin -> straight ribbon -> TI pin)
#define PIN_ROW_INT5  14   // GPIO14 -> J10 p15 -> TI pin 1
#define PIN_ROW_INT6  13   // GPIO13 -> J10 p14 -> TI pin 2
#define PIN_ROW_INT8  12   // GPIO12 -> J10 p13 -> TI pin 3
#define PIN_ROW_INT4  11   // GPIO11 -> J10 p12 -> TI pin 4
#define PIN_ROW_INT3  10   // GPIO10 -> J10 p11 -> TI pin 5
#define PIN_ROW_INT7   8   // GPIO8  -> J10 p9  -> TI pin 7
#define PIN_ROW_INT9  16   // GPIO16 -> J10 p6  -> TI pin 10
#define PIN_ROW_INT10 15   // GPIO15 -> J10 p5  -> TI pin 11

// Column inputs — TI -> ESP32
#define PIN_COL_1Y1   18   // GPIO18 -> J10 p8  -> TI pin 8  (col 0)
#define PIN_COL_1Y0   17   // GPIO17 -> J10 p7  -> TI pin 9  (col 4)
#define PIN_COL_2Y0    7   // GPIO7  -> J10 p4  -> TI pin 12 (col 5)
#define PIN_COL_2Y1    6   // GPIO6  -> J10 p3  -> TI pin 13 (col 1)
#define PIN_COL_2Y2    5   // GPIO5  -> J10 p2  -> TI pin 14 (col 2)
#define PIN_COL_2Y3    4   // GPIO4  -> J10 p1  -> TI pin 15 (col 3)

// Alpha Lock — wired in v4 (GPIO9 -> J10 p10 -> TI pin 6 / P5) but NEVER
// driven: kept INPUT for the life of the sketch. Software alpha lock injects
// SHIFT instead; driving this line breaks joystick UP on unmodified consoles.
#define PIN_ALPHA_LOCK 9

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
#define HID_KEY_MINUS        0x2D
#define HID_KEY_EQUAL        0x2E
#define HID_KEY_LEFTBRACKET  0x2F
#define HID_KEY_RIGHTBRACKET 0x30
#define HID_KEY_BACKSLASH    0x31
#define HID_KEY_SEMICOLON    0x33
#define HID_KEY_APOSTROPHE   0x34
#define HID_KEY_GRAVE        0x35
#define HID_KEY_COMMA        0x36
#define HID_KEY_PERIOD       0x37
#define HID_KEY_SLASH        0x38
#define HID_KEY_CAPSLOCK     0x39
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
  {0xFF, 0},                                           // Minus    (punctKeys)
  {0xFF, 0},                                           // Equal    (punctKeys)
  {0xFF, 0}, {0xFF, 0}, {0xFF, 0}, {0xFF, 0},         // [ ] \ (punctKeys), #
  {0, ROW1},                                           // Semicolon
  {0xFF, 0}, {0xFF, 0},                                // ' ` (punctKeys)
  {2, ROW0},                                           // Comma
  {1, ROW0},                                           // Period
  {0xFF, 0},                                           // Slash    (punctKeys)
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
  {HID_KEY_BACKSPACE, 1, ROW5, true, false},  // FCTN+S (LEFT)
  {HID_KEY_TAB,       3, ROW3, true, false},  // FCTN+7
  {HID_KEY_DELETE,    0, ROW7, true, false},  // FCTN+1 (DEL)
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
// Shift-dependent punctuation
// ---------------------------------------------------------------------------
// US-keyboard punctuation keys whose TI-99/4A production differs between the
// unshifted and shifted forms — and where the shifted form often needs FCTN
// (not SHIFT) and a completely different matrix cell. The generic "pass the
// physical SHIFT through to a base key" rule can't express this, so these
// keys are resolved here from the live modifier state instead.
//
// Each entry gives the TI matrix cell (col + row bitmask) and which TI
// modifier to add, separately for the unshifted (u*) and shifted (s*) forms.
//
// Matrix positions verified against Thierry Nouspikel's TI-99/4A keyboard
// matrix (unige.ch/.../ti99/keyboard.htm), mapped to this sketch's proven
// col/row numbering via the working letter/number cells. Notably `/` lives
// at {col0,ROW0} and `=` at {col5,ROW0} — NOT the other way around.
//
// Reference — TI produces:
//   -  SHIFT+/      _  FCTN+U        =  (base {5,R0})   +  SHIFT+=
//   /  (base {0,R0}) ?  FCTN+I       ;  (base {0,R1})   :  SHIFT+;
//   ,  (base {2,R0}) <  SHIFT+,      .  (base {1,R0})   >  SHIFT+.
//   '  FCTN+O        "  FCTN+P       `  FCTN+C          ~  FCTN+W
//   [  FCTN+R        {  FCTN+F       ]  FCTN+T          }  FCTN+G
//   \  FCTN+Z        |  FCTN+A
#define M_NONE  0
#define M_SHIFT 1
#define M_FCTN  2

typedef struct
{
  uint8_t hidKey;
  uint8_t uCol, uRow, uMod;   // unshifted form
  uint8_t sCol, sRow, sMod;   // shifted form
} PunctKeyMapping;

static const PunctKeyMapping punctKeys[] =
{
  // HID key               unshifted → TI          shifted → TI
  {HID_KEY_MINUS,        0, ROW0, M_SHIFT,      3, ROW2, M_FCTN },  // -  / _
  {HID_KEY_EQUAL,        5, ROW0, M_NONE,       5, ROW0, M_SHIFT},  // =  / +
  {HID_KEY_SEMICOLON,    0, ROW1, M_NONE,       0, ROW1, M_SHIFT},  // ;  / :
  {HID_KEY_COMMA,        2, ROW0, M_NONE,       2, ROW0, M_SHIFT},  // ,  / <
  {HID_KEY_PERIOD,       1, ROW0, M_NONE,       1, ROW0, M_SHIFT},  // .  / >
  {HID_KEY_SLASH,        0, ROW0, M_NONE,       2, ROW2, M_FCTN },  // /  / ?
  {HID_KEY_APOSTROPHE,   1, ROW2, M_FCTN,       0, ROW2, M_FCTN },  // '  / "
  {HID_KEY_LEFTBRACKET,  3, ROW6, M_FCTN,       3, ROW5, M_FCTN },  // [  / {
  {HID_KEY_RIGHTBRACKET, 4, ROW6, M_FCTN,       4, ROW5, M_FCTN },  // ]  / }
  {HID_KEY_BACKSLASH,    0, ROW4, M_FCTN,       0, ROW5, M_FCTN },  // \  / |
  {HID_KEY_GRAVE,        2, ROW4, M_FCTN,       1, ROW6, M_FCTN },  // `  / ~
};
#define NUM_PUNCT_KEYS (sizeof(punctKeys) / sizeof(punctKeys[0]))

// ---------------------------------------------------------------------------
// Single-key matrix builder
// ---------------------------------------------------------------------------
// Given one HID key + its captured modifiers + alpha-lock state, write the
// resulting 6-byte TI column matrix into out[]. Used by both the immediate
// pass-through path (which calls it once per key in the report) and the
// type-ahead replay state machine (which calls it on the head of the
// queue). Returns true if the key produced any matrix activity.
static bool buildTiMatrixForKey(uint8_t hidKey,
                                uint8_t modifiers,
                                bool alphaLockOn,
                                uint8_t out[6])
{
  memset(out, 0, 6);

  bool shiftHeld = (modifiers & (HID_MOD_LSHIFT | HID_MOD_RSHIFT)) != 0;

  // Shift-dependent punctuation resolves to its own TI cell + TI modifier
  // and returns immediately — it must NOT fall through to the generic SHIFT
  // passthrough below, since a shifted form like "?" is FCTN+I (no SHIFT).
  if (hidKey != 0)
  {
    for (int p = 0; p < NUM_PUNCT_KEYS; p++)
    {
      if (punctKeys[p].hidKey == hidKey)
      {
        uint8_t col = shiftHeld ? punctKeys[p].sCol : punctKeys[p].uCol;
        uint8_t row = shiftHeld ? punctKeys[p].sRow : punctKeys[p].uRow;
        uint8_t mod = shiftHeld ? punctKeys[p].sMod : punctKeys[p].uMod;
        out[col] |= row;
        if (mod == M_FCTN)
        {
          out[TI_FCTN_COL] |= TI_FCTN_ROW;
        }
        else if (mod == M_SHIFT)
        {
          out[TI_SHIFT_COL] |= TI_SHIFT_ROW;
        }
        // Physical CTRL/ALT still stack on top of the punctuation mapping,
        // so combos like ALT+= reach FCTN+= (QUIT) and CTRL+= reaches
        // CTRL+=. Physical SHIFT is NOT re-applied here — it was already
        // consumed above to select the unshifted vs shifted form.
        if (modifiers & (HID_MOD_LCTRL | HID_MOD_RCTRL))
        {
          out[TI_CTRL_COL] |= TI_CTRL_ROW;
        }
        if (modifiers & (HID_MOD_LALT | HID_MOD_RALT))
        {
          out[TI_FCTN_COL] |= TI_FCTN_ROW;
        }
        return true;
      }
    }
  }

  // Modifier passthrough
  if (modifiers & (HID_MOD_LSHIFT | HID_MOD_RSHIFT))
  {
    out[TI_SHIFT_COL] |= TI_SHIFT_ROW;
  }
  if (modifiers & (HID_MOD_LCTRL | HID_MOD_RCTRL))
  {
    out[TI_CTRL_COL] |= TI_CTRL_ROW;
  }
  if (modifiers & (HID_MOD_LALT | HID_MOD_RALT))
  {
    out[TI_FCTN_COL] |= TI_FCTN_ROW;
  }

  if (hidKey == 0)
  {
    return out[0] || out[1] || out[2] || out[3] || out[4] || out[5];
  }

  // Special key (FCTN/SHIFT + something) lookup first
  for (int s = 0; s < NUM_SPECIAL_KEYS; s++)
  {
    if (specialKeys[s].hidKey == hidKey)
    {
      out[specialKeys[s].tiCol] |= specialKeys[s].tiRow;
      if (specialKeys[s].needFctn)
      {
        out[TI_FCTN_COL] |= TI_FCTN_ROW;
      }
      if (specialKeys[s].needShift)
      {
        out[TI_SHIFT_COL] |= TI_SHIFT_ROW;
      }
      return true;
    }
  }

  // Standard key lookup
  if (hidKey < MAP_SIZE && hidToTi[hidKey].col != 0xFF)
  {
    out[hidToTi[hidKey].col] |= hidToTi[hidKey].row;

    // Software Alpha Lock: force SHIFT on letters when caps lock is on.
    if (alphaLockOn && hidKey >= HID_KEY_A && hidKey <= HID_KEY_Z)
    {
      out[TI_SHIFT_COL] |= TI_SHIFT_ROW;
    }
    return true;
  }

  return out[0] || out[1] || out[2] || out[3] || out[4] || out[5];
}

// ---------------------------------------------------------------------------
// Type-ahead Buffer (PC-style 16-deep ring, F11 to toggle at runtime)
// ---------------------------------------------------------------------------
#ifdef ENABLE_TYPE_AHEAD_BUFFER

struct TypeAheadEvent
{
  uint8_t hidKey;     // HID scancode
  uint8_t modifiers;  // snapshot of modifier byte at press time
  bool    alphaLock;  // snapshot of alpha-lock state at press time
};

static TypeAheadEvent taBuf[TYPE_AHEAD_DEPTH];
static volatile int   taHead = 0;   // write index
static volatile int   taTail = 0;   // read index
static bool           taEnabled = false;
static unsigned long  taPhaseEndsAt = 0;
static bool           taHolding = false;  // true = key on matrix, false = inter-key gap

static inline int taCount()
{
  int n = taHead - taTail;
  if (n < 0) n += TYPE_AHEAD_DEPTH;
  return n;
}

static inline bool taPush(uint8_t hidKey, uint8_t modifiers, bool alphaLock)
{
  int next = (taHead + 1) % TYPE_AHEAD_DEPTH;
  if (next == taTail)
  {
    return false;  // full — drop the keystroke (PC BIOS beeps; we just drop)
  }
  taBuf[taHead].hidKey    = hidKey;
  taBuf[taHead].modifiers = modifiers;
  taBuf[taHead].alphaLock = alphaLock;
  taHead = next;
  return true;
}

static void taFlush()
{
  taHead = taTail = 0;
  taHolding = false;
  taPhaseEndsAt = 0;
  memset((void *)keyState, 0, sizeof(keyState));
}

#endif // ENABLE_TYPE_AHEAD_BUFFER

// ---------------------------------------------------------------------------
// Shared HID Report Processing
// ---------------------------------------------------------------------------
static uint8_t prevKeys[6] = {0};

// ---------------------------------------------------------------------------
// TI matrix reverse map (authoritative) — for step-1 (HID→TI) validation
// ---------------------------------------------------------------------------
// [col][rowBitIndex] -> TI key name, where rowBitIndex r means ROWr = 1<<r.
// This is the definitive TI-99/4A matrix (per Thierry Nouspikel), expressed
// in this sketch's proven col/row numbering. It lets the debug print decode a
// built keyState[] back into a readable "MODS + keys" label, so the HID→TI
// translation can be validated straight from the serial monitor without the
// TI attached or its scan timing in play. col5 rows 5/6/7 are the modifiers
// and are printed as prefixes, not base keys.
static const char *const tiCellName[6][8] =
{
  /* col0 */ { "/",  ";",     "P",     "0",  "Z",     "A",    "Q",    "1" },
  /* col1 */ { ".",  "L",     "O",     "9",  "X",     "S",    "W",    "2" },
  /* col2 */ { ",",  "K",     "I",     "8",  "C",     "D",    "E",    "3" },
  /* col3 */ { "M",  "J",     "U",     "7",  "V",     "F",    "R",    "4" },
  /* col4 */ { "N",  "H",     "Y",     "6",  "B",     "G",    "T",    "5" },
  /* col5 */ { "=",  "SPACE", "ENTER", "-",  "-",     "SHIFT","CTRL", "FCTN" },
};

// Decode a built TI matrix into a readable label, e.g. "FCTN I", "SHIFT 1",
// "A", or "(release)". Modifiers (col5 rows 5/6/7) are listed first.
static void decodeTiMatrix(const volatile uint8_t ks[6], char *buf, size_t buflen)
{
  buf[0] = '\0';
  bool any = false;

  if (ks[5] & TI_FCTN_ROW)  { strncat(buf, "FCTN ",  buflen - strlen(buf) - 1); any = true; }
  if (ks[5] & TI_CTRL_ROW)  { strncat(buf, "CTRL ",  buflen - strlen(buf) - 1); any = true; }
  if (ks[5] & TI_SHIFT_ROW) { strncat(buf, "SHIFT ", buflen - strlen(buf) - 1); any = true; }

  for (int c = 0; c < 6; c++)
  {
    for (int r = 0; r < 8; r++)
    {
      if (c == 5 && (r == 5 || r == 6 || r == 7))
      {
        continue;  // modifier bits, already handled
      }
      if (ks[c] & (1u << r))
      {
        strncat(buf, tiCellName[c][r], buflen - strlen(buf) - 1);
        strncat(buf, " ", buflen - strlen(buf) - 1);
        any = true;
      }
    }
  }

  if (!any)
  {
    strncpy(buf, "(release)", buflen);
    buf[buflen - 1] = '\0';
  }
}

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

// Helper: was this HID code present in the previous report?
static inline bool keyWasPressed(uint8_t k)
{
  for (int j = 0; j < 6; j++)
  {
    if (prevKeys[j] == k) return true;
  }
  return false;
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
  for (int i = 0; i < 6; i++)
  {
    if (keys[i] == HID_KEY_F12) { f12Now = true; break; }
  }
  if (f12Now && !keyWasPressed(HID_KEY_F12))
  {
    BleHidHost::requestPairingMode();
  }
#endif

#ifdef ENABLE_TYPE_AHEAD_BUFFER
  // F11 → toggle type-ahead buffer at runtime. Edge-triggered, never
  // forwarded to TI. Flushes the queue + matrix on either transition so
  // a leftover queued key can't bleed into game mode.
  bool f11Now = false;
  for (int i = 0; i < 6; i++)
  {
    if (keys[i] == HID_KEY_F11) { f11Now = true; break; }
  }
  if (f11Now && !keyWasPressed(HID_KEY_F11))
  {
    taEnabled = !taEnabled;
    taFlush();
    Serial.printf("Type-ahead buffer: %s\n", taEnabled ? "ON" : "OFF");
  }
#endif

  // Caps Lock toggle (edge-triggered, never reaches the TI matrix)
  bool capsNow = false;
  for (int i = 0; i < 6; i++)
  {
    if (keys[i] == HID_KEY_CAPSLOCK) { capsNow = true; break; }
  }
  if (capsNow && !keyWasPressed(HID_KEY_CAPSLOCK))
  {
    alphaLockActive = !alphaLockActive;
  }

#ifdef ENABLE_TYPE_AHEAD_BUFFER
  if (taEnabled)
  {
    // Buffered path: every new key-down edge becomes a queued event with
    // its modifiers + alpha-lock state captured at press time. Releases
    // and meta-keys (F11/F12/Caps) are NOT enqueued — they were handled
    // above. The replay state machine (processTypeAheadBuffer) drives
    // keyState[] from the queue.
    for (int i = 0; i < 6; i++)
    {
      uint8_t k = keys[i];
      if (k == 0) continue;
      if (k == HID_KEY_F11 || k == HID_KEY_F12 || k == HID_KEY_CAPSLOCK) continue;
      if (keyWasPressed(k)) continue;  // already in flight from a prior report
      taPush(k, modifiers, alphaLockActive);
    }
    memcpy(prevKeys, keys, 6);
    return;
  }
#endif

  // Pass-through path (no buffer): rebuild keyState from the current
  // report on every report. This is the original game-friendly behavior
  // — "is this key DOWN right now?" is faithfully reflected in the matrix.
  memset((void *)keyState, 0, sizeof(keyState));

  for (int i = 0; i < 6; i++)
  {
    uint8_t k = keys[i];
    if (k == 0 || k == HID_KEY_CAPSLOCK || k == HID_KEY_F11 || k == HID_KEY_F12)
    {
      continue;
    }

    uint8_t perKey[6];
    if (buildTiMatrixForKey(k, modifiers, alphaLockActive, perKey))
    {
      for (int c = 0; c < 6; c++)
      {
        keyState[c] |= perKey[c];
      }
    }
  }

  // Even if no real keys are down, modifiers alone should still be
  // reflected (so the user can hold SHIFT before pressing a letter).
  uint8_t modOnly[6];
  buildTiMatrixForKey(0, modifiers, alphaLockActive, modOnly);
  for (int c = 0; c < 6; c++)
  {
    keyState[c] |= modOnly[c];
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

    // Step-1 validation readout: what you typed (HID) -> what the TI will
    // see (decoded from the built matrix). Read straight from the serial
    // monitor with or without the TI attached.
    const char *hidStr = (primaryKey != 0)
                           ? hidKeyToDebugChar(primaryKey, effectiveShift)
                           : "(mods)";
    char tiLabel[40];
    decodeTiMatrix(keyState, tiLabel, sizeof(tiLabel));

    Serial.printf("HID %-9s -> TI %-18s  C0=%02X C1=%02X C2=%02X C3=%02X C4=%02X C5=%02X  alpha=%d\n",
                  hidStr, tiLabel,
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

  // Reconnect after the keyboard sleeps is handled inside BleHidHost by a
  // persistent background scan that re-binds a saved peer by address the
  // instant it re-advertises. The old watchdog that forced pairing mode
  // every 5 s when disconnected was removed: it was only ever needed
  // because scanning used to run only during pairing windows, and it made
  // the host chase random nearby HID-looking devices (e.g. a stray "Q35…"
  // name) instead of quietly waiting for the saved keyboard. New keyboards
  // still pair with F12.
}

#endif // INPUT_BLE

// ---------------------------------------------------------------------------
// Type-ahead Buffer Replay
// ---------------------------------------------------------------------------
#ifdef ENABLE_TYPE_AHEAD_BUFFER

// Drive keyState[] from the head of the type-ahead queue. Two phases per
// queued event: HOLD (key + modifiers asserted on the matrix for HOLD_MS)
// then GAP (matrix cleared for GAP_MS so the ROM sees a clean release).
// When the queue is empty and we're not mid-hold, keyState stays zero so
// any held physical key from a stale prevKeys snapshot doesn't leak through.
static void processTypeAheadBuffer()
{
  if (!taEnabled) return;

  unsigned long now = millis();

  // Idle: nothing to do.
  if (!taHolding && taPhaseEndsAt == 0 && taCount() == 0)
  {
    return;
  }

  // GAP phase finished — advance the queue and start the next HOLD, or go
  // idle if the queue is empty.
  if (!taHolding && taPhaseEndsAt != 0 && (long)(now - taPhaseEndsAt) >= 0)
  {
    taPhaseEndsAt = 0;
    if (taCount() == 0)
    {
      memset((void *)keyState, 0, sizeof(keyState));
      return;
    }
  }

  // HOLD phase finished — clear the matrix and start the GAP.
  if (taHolding && (long)(now - taPhaseEndsAt) >= 0)
  {
    memset((void *)keyState, 0, sizeof(keyState));
    taTail = (taTail + 1) % TYPE_AHEAD_DEPTH;
    taHolding = false;
    taPhaseEndsAt = now + TYPE_AHEAD_GAP_MS;
    return;
  }

  // Start a new HOLD phase from the head of the queue.
  if (!taHolding && taCount() > 0 && taPhaseEndsAt == 0)
  {
    const TypeAheadEvent& ev = taBuf[taTail];
    uint8_t built[6];
    buildTiMatrixForKey(ev.hidKey, ev.modifiers, ev.alphaLock, built);
    memcpy((void *)keyState, built, 6);
    taHolding = true;
    taPhaseEndsAt = now + TYPE_AHEAD_HOLD_MS;
    keypressTime = now;  // light up keypress LED feedback
  }
}

#endif // ENABLE_TYPE_AHEAD_BUFFER

// ---------------------------------------------------------------------------
// TI-99/4A Matrix Output
// ---------------------------------------------------------------------------

// COL_SETTLE_US debounces strobe detection. The TI strobes one column LOW
// at a time, but adjacent strobe edges capacitively couple transient drops
// onto neighboring (idle) columns — measured at ~10us duration on the
// rev 2 PCB with unshielded jumpers. The original TI ROM avoids confusion
// by sampling rows ~20-50us AFTER asserting the column (waiting for the
// line to settle); we mimic that discipline here so a transient spike
// doesn't get latched as a real strobe.
//
// Settle window must be > observed transient duration (so the second read
// catches the line back at HIGH after the blip has decayed) but well under
// the TI's row-sample timing (~20-50us after strobe) so our row outputs
// are valid when the ROM samples. 30us splits that window with margin on
// both sides. Real TI strobes hold LOW for hundreds of us so they survive
// both reads cleanly.
#define COL_SETTLE_US 30

static inline bool colIsAsserted(int c)
{
  if (digitalRead(colPins[c]) != LOW)
  {
    return false;
  }
  delayMicroseconds(COL_SETTLE_US);
  return digitalRead(colPins[c]) == LOW;
}

static inline void updateRowOutputs()
{
  int activeCol = -1;
  for (int c = 0; c < 6; c++)
  {
    if (colIsAsserted(c))
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

  // PIN_ALPHA_LOCK is wired (v4: GPIO9 -> J10 p10 -> TI pin 6) but
  // intentionally never driven -- parked as INPUT for the life of the
  // sketch. See processHidReport for the software Alpha Lock
  // implementation. Driving the original Alpha Lock line breaks
  // joystick UP on unmodified TI-99/4A consoles.
  pinMode(PIN_ALPHA_LOCK, INPUT);
}

// ---------------------------------------------------------------------------
// Timer-Sampled, Debounced Row Outputs
// ---------------------------------------------------------------------------
// A hardware timer samples all six column inputs at a fixed high rate and
// debounces them: a column must read LOW for MATRIX_DEBOUNCE consecutive
// samples to count as a genuine strobe. This robustly rejects the ribbon's
// capacitive coupling transients (a blip lasts only a sample or two, never
// enough to confirm) while a real ~66us strobe is confirmed within a few
// samples — comfortably before the ROM samples the rows (~20-50us in).
//
// The rows driven are the OR of keyState[] over every CONFIRMED-low column.
// During a normal scan only one column is confirmed at a time, so this is just
// that column's rows (no ghosting). During the QUIT/break ROM routine, which
// deliberately holds two columns low at once, both confirm and their keys
// combine correctly (an idle column contributes 0). This replaced a reactive
// column-edge ISR that couldn't tell a coupling blip from a real overlapping
// strobe without either dropping QUIT or duplicating typed characters.
//
// Asymmetric debounce: a column becomes ACTIVE on the first LOW sample (drive
// the row with almost no latency, so we're valid when the ROM samples), but
// only DROPS after MATRIX_HIGH_RELEASE consecutive HIGH samples. The release
// hysteresis is what matters: a single-sample positive coupling blip on the
// column being held would otherwise reset it mid-strobe and the TI would read
// release+repress -> duplicate characters. Idle-column transients are harmless
// regardless since they contribute keyState==0 to the OR.
#define MATRIX_SAMPLE_US    5
#define MATRIX_LOW_CONFIRM  1
#define MATRIX_HIGH_RELEASE 2

static volatile uint8_t currentRowsDriven = 0;
static uint32_t         rowBit[8]         = {0};  // GPIO bit per row pin
static uint32_t         colBitMask[6]     = {0};  // GPIO bit per column pin
static volatile uint8_t colLowCount[6]    = {0};  // consecutive LOW samples
static volatile uint8_t colHighCount[6]   = {0};  // consecutive HIGH samples
static volatile bool    colActive[6]      = {false};
static hw_timer_t      *matrixTimer       = nullptr;

// Configure all row pins as open-drain outputs, released (high-Z), and cache
// each one's GPIO bit for fast register-level driving. Open-drain means a row
// is driven LOW by clearing its OUT bit and released (high-Z) by setting it.
static void configureRowsOpenDrain()
{
  for (int r = 0; r < 8; r++)
  {
    rowBit[r] = (1u << rowPins[r]);
    pinMode(rowPins[r], OUTPUT_OPEN_DRAIN);
    digitalWrite(rowPins[r], HIGH);  // released: high-Z, TI pull-up holds HIGH
  }
  currentRowsDriven = 0;
}

// Apply a desired 8-bit row mask via two direct GPIO register writes. A set
// bit = drive that row LOW (clear its OUT bit); a clear bit = release to
// high-Z (set its OUT bit). All row GPIOs are < 32, so one OUT_W1TS/W1TC bank
// covers them. Skips the writes when nothing changed (the common case).
static inline void IRAM_ATTR applyRows(uint8_t desired)
{
  if (desired == currentRowsDriven)
  {
    return;
  }
  uint32_t clr = 0, set = 0;
  for (int r = 0; r < 8; r++)
  {
    if (desired & (1u << r))
    {
      clr |= rowBit[r];   // drive LOW
    }
    else
    {
      set |= rowBit[r];   // release HIGH
    }
  }
  REG_WRITE(GPIO_OUT_W1TC_REG, clr);
  REG_WRITE(GPIO_OUT_W1TS_REG, set);
  currentRowsDriven = desired;
}

// Timer ISR: sample all columns, update debounce counters, drive the OR of the
// keyState of every confirmed-low column.
static void IRAM_ATTR onMatrixTimer()
{
  uint32_t in = REG_READ(GPIO_IN_REG);
  uint8_t desired = 0;
  for (int c = 0; c < 6; c++)
  {
    if (in & colBitMask[c])                  // HIGH
    {
      colLowCount[c] = 0;
      if (colHighCount[c] < MATRIX_HIGH_RELEASE)
      {
        colHighCount[c]++;
      }
      if (colHighCount[c] >= MATRIX_HIGH_RELEASE)
      {
        colActive[c] = false;                // released after N consecutive highs
      }
    }
    else                                     // LOW
    {
      colHighCount[c] = 0;
      if (colLowCount[c] < MATRIX_LOW_CONFIRM)
      {
        colLowCount[c]++;
      }
      if (colLowCount[c] >= MATRIX_LOW_CONFIRM)
      {
        colActive[c] = true;                 // asserted on the first low sample
      }
    }
    if (colActive[c])
    {
      desired |= keyState[c];
    }
  }
  applyRows(desired);
}

// Start (or resume) the column sampler. Named attachMatrixIsrs for continuity
// with the callers that arm/disarm the matrix output.
static void attachMatrixIsrs()
{
  configureRowsOpenDrain();
  for (int c = 0; c < 6; c++)
  {
    colBitMask[c]   = (1u << colPins[c]);
    colLowCount[c]  = 0;
    colHighCount[c] = 0;
    colActive[c]    = false;
  }
  if (matrixTimer == nullptr)
  {
    matrixTimer = timerBegin(1000000);                   // 1 MHz tick
    timerAttachInterrupt(matrixTimer, &onMatrixTimer);
    timerAlarm(matrixTimer, MATRIX_SAMPLE_US, true, 0);  // period, autoreload
  }
  else
  {
    timerStart(matrixTimer);
  }
}

// Stop the sampler and tri-state all row lines (used by observe / serial-debug
// modes so they can drive the pins directly).
static void detachMatrixIsrs()
{
  if (matrixTimer != nullptr)
  {
    timerStop(matrixTimer);
  }
  for (int c = 0; c < 6; c++)
  {
    colLowCount[c]  = 0;
    colHighCount[c] = 0;
    colActive[c]    = false;
  }
  currentRowsDriven = 0;
  for (int r = 0; r < 8; r++)
  {
    pinMode(rowPins[r], INPUT);
  }
}

// ---------------------------------------------------------------------------
// Strobe Observation Mode (passive scope-in-firmware)
// ---------------------------------------------------------------------------
// Captures every column-line edge from a real TI with microsecond
// timestamps and dumps them over serial. Used to derive: scan period,
// strobe duration, scan order, transient frequency/duration. Doesn't drive
// the rows while active so our own output doesn't perturb what we're
// observing.
//
// Usage:
//   observe   - start capturing (rows held INPUT, all column edges logged)
//   off       - stop and return to normal matrix output
//
// Output format (one line per edge):
//   STROBE  t=<us-from-start>  col=<idx> (<TI signal>)  <FALL|RISE>
//                                                       [dur=<us>]   ; on RISE
//
// Implementation: ISR records {micros, col, level} into a lock-free ring
// buffer. Main loop drains and printf's. Never call Serial from an ISR.

#define STROBE_LOG_DEPTH 256   // ring entries; keep power-of-2 for cheap mod

struct StrobeEvent
{
  uint32_t timeUs;
  uint8_t  col;
  uint8_t  level;   // 0 = FALL, 1 = RISE
};

static volatile StrobeEvent strobeLog[STROBE_LOG_DEPTH];
static volatile uint32_t    strobeLogHead = 0;   // ISR writes
static volatile uint32_t    strobeLogTail = 0;   // main loop reads
static volatile uint32_t    strobeLogDropped = 0;

// Per-column ISR fire counter — atomic-ish (single-writer, single-reader).
static volatile uint32_t colIsrCount[6] = {0};
// Per-column last known level. ISR updates from GPIO read on each edge.
// A FALL is recorded when this transitions HIGH→LOW; a RISE is recorded
// when this transitions LOW→HIGH. More robust than relying on the
// post-edge digitalRead matching the actual edge polarity.
static volatile uint8_t colLastLevel[6] = {1, 1, 1, 1, 1, 1};

static bool   strobeObserveMode = false;
static uint32_t strobeObserveStartUs = 0;
static uint32_t strobeHeartbeatLastMs = 0;
// Track last FALL time per column so RISE prints "dur=NNus" inline.
static uint32_t colLastFallUs[6] = {0};

static const char *colName(int c)
{
  static const char *names[6] = { "1Y1", "2Y1", "2Y2", "2Y3", "1Y0", "2Y0" };
  return (c >= 0 && c < 6) ? names[c] : "?";
}

static void IRAM_ATTR strobeEdgeIsr(void *arg)
{
  int c = (int)(intptr_t)arg;
  colIsrCount[c]++;

  // Read post-edge level. For very fast pulses the line may already have
  // returned by the time the ISR runs (a few us latency on ESP32-S3),
  // so we cross-check against the last known level: if the read says HIGH
  // and we previously saw HIGH, the edge was a brief LOW pulse — we still
  // want to log both edges. Same for the symmetric case.
  uint8_t lvl = (uint8_t)(digitalRead(colPins[c]) == HIGH ? 1 : 0);

  if (lvl == colLastLevel[c])
  {
    // The line transitioned and came back before our ISR could catch it.
    // Log both edges so the analysis sees a non-zero pulse. Use micros()
    // for both to keep them adjacent in the timeline.
    uint32_t t = micros();
    for (int k = 0; k < 2; k++)
    {
      uint32_t next = (strobeLogHead + 1) & (STROBE_LOG_DEPTH - 1);
      if (next == strobeLogTail)
      {
        strobeLogDropped++;
        return;
      }
      strobeLog[strobeLogHead].timeUs = t;
      strobeLog[strobeLogHead].col    = (uint8_t)c;
      // First entry = opposite of current level (the missed edge),
      // second entry = current level (the recovery edge).
      strobeLog[strobeLogHead].level  = (k == 0) ? (uint8_t)(1 - lvl) : lvl;
      strobeLogHead = next;
    }
    return;
  }

  // Normal case: a single edge was caught.
  colLastLevel[c] = lvl;

  uint32_t next = (strobeLogHead + 1) & (STROBE_LOG_DEPTH - 1);
  if (next == strobeLogTail)
  {
    strobeLogDropped++;
    return;
  }
  strobeLog[strobeLogHead].timeUs = micros();
  strobeLog[strobeLogHead].col    = (uint8_t)c;
  strobeLog[strobeLogHead].level  = lvl;
  strobeLogHead = next;
}

static void strobeObserveStart()
{
  // Stop the column sampler and release any rows we may have been driving so
  // we don't perturb the TI while observing.
  if (matrixTimer != nullptr)
  {
    timerStop(matrixTimer);
  }
  for (int r = 0; r < 8; r++)
  {
    pinMode(rowPins[r], INPUT);
  }

  strobeLogHead    = 0;
  strobeLogTail    = 0;
  strobeLogDropped = 0;
  strobeObserveStartUs = micros();
  strobeHeartbeatLastMs = millis();
  for (int c = 0; c < 6; c++)
  {
    colLastFallUs[c] = 0;
    colIsrCount[c]   = 0;
    // Initialize last-level from a fresh read so we don't start with a
    // mismatched assumption.
    colLastLevel[c]  = (uint8_t)(digitalRead(colPins[c]) == HIGH ? 1 : 0);
  }

  // Detach first in case there's a stale interrupt from a previous run.
  for (int c = 0; c < 6; c++)
  {
    detachInterrupt(digitalPinToInterrupt(colPins[c]));
  }
  for (int c = 0; c < 6; c++)
  {
    attachInterruptArg(digitalPinToInterrupt(colPins[c]),
                       strobeEdgeIsr,
                       (void *)(intptr_t)c,
                       CHANGE);
  }

  strobeObserveMode = true;
  Serial.println("OBSERVE: capturing column edges. Type 'off' to stop.");
  Serial.println("OBSERVE: initial column levels:");
  for (int c = 0; c < 6; c++)
  {
    Serial.printf("  col=%d (%s) GPIO=%d level=%s\n",
                  c, colName(c), colPins[c],
                  colLastLevel[c] ? "HIGH" : "LOW");
  }
}

static void strobeObserveStop()
{
  strobeObserveMode = false;
  for (int c = 0; c < 6; c++)
  {
    detachInterrupt(digitalPinToInterrupt(colPins[c]));
  }
  attachMatrixIsrs();
  Serial.printf("OBSERVE: stopped. (Ring buffer drops: %u)\n",
                (unsigned)strobeLogDropped);
}

// Drain the ring buffer to serial. Called from main loop. Prints up to
// `maxPerCall` events per invocation so we never block loop() for long.
static void strobeObserveDrain()
{
  if (!strobeObserveMode)
  {
    return;
  }

  // Heartbeat: once per second, print per-column ISR fire counts and
  // current GPIO levels. Confirms the ISR pipeline is alive even if
  // there are no logged events (which would mean the line is moving
  // too fast for the ISR to catch, or it's not moving at all).
  uint32_t nowMs = millis();
  if (nowMs - strobeHeartbeatLastMs >= 1000)
  {
    strobeHeartbeatLastMs = nowMs;
    Serial.printf("OBSERVE-HB: ISRs c0=%u c1=%u c2=%u c3=%u c4=%u c5=%u  "
                  "lvls=%d%d%d%d%d%d  drops=%u\n",
                  (unsigned)colIsrCount[0], (unsigned)colIsrCount[1],
                  (unsigned)colIsrCount[2], (unsigned)colIsrCount[3],
                  (unsigned)colIsrCount[4], (unsigned)colIsrCount[5],
                  digitalRead(colPins[0]), digitalRead(colPins[1]),
                  digitalRead(colPins[2]), digitalRead(colPins[3]),
                  digitalRead(colPins[4]), digitalRead(colPins[5]),
                  (unsigned)strobeLogDropped);
  }

  const int maxPerCall = 32;
  for (int i = 0; i < maxPerCall; i++)
  {
    if (strobeLogTail == strobeLogHead)
    {
      return;
    }
    StrobeEvent ev;
    ev.timeUs = strobeLog[strobeLogTail].timeUs;
    ev.col    = strobeLog[strobeLogTail].col;
    ev.level  = strobeLog[strobeLogTail].level;
    strobeLogTail = (strobeLogTail + 1) & (STROBE_LOG_DEPTH - 1);

    uint32_t rel = ev.timeUs - strobeObserveStartUs;

    if (ev.level == 0)   // FALL
    {
      colLastFallUs[ev.col] = ev.timeUs;
      Serial.printf("STROBE  t=%9u  col=%u (%s)  FALL\n",
                    (unsigned)rel, ev.col, colName(ev.col));
    }
    else                  // RISE
    {
      uint32_t dur = colLastFallUs[ev.col]
                     ? (ev.timeUs - colLastFallUs[ev.col])
                     : 0;
      if (dur)
      {
        Serial.printf("STROBE  t=%9u  col=%u (%s)  RISE  dur=%uus\n",
                      (unsigned)rel, ev.col, colName(ev.col), (unsigned)dur);
      }
      else
      {
        Serial.printf("STROBE  t=%9u  col=%u (%s)  RISE\n",
                      (unsigned)rel, ev.col, colName(ev.col));
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Serial Debug Mode
// ---------------------------------------------------------------------------
// Type a 15-digit binary number (e.g. "110000001000000") into the serial
// monitor to directly drive all 15 TI connector pins. Bit order matches
// the TI keyboard connector pin numbering:
//
// NOTE: "TI pin n" here is the TI MOTHERBOARD pin the signal reaches through
// the v4 straight ribbon (J10 pin 16-n), not the J10 connector position.
//
//   Bit 1  (leftmost)  = TI pin 1  (INT5,  row, GPIO 14)
//   Bit 2              = TI pin 2  (INT6,  row, GPIO 13)
//   Bit 3              = TI pin 3  (INT8,  row, GPIO 12)
//   Bit 4              = TI pin 4  (INT4,  row, GPIO 11)
//   Bit 5              = TI pin 5  (INT3,  row, GPIO 10)
//   Bit 6              = TI pin 6  (P5,    alpha lock, GPIO 9)
//   Bit 7              = TI pin 7  (INT7,  row, GPIO 8)
//   Bit 8              = TI pin 8  (1Y1,   col, GPIO 18)
//   Bit 9              = TI pin 9  (1Y0,   col, GPIO 17)
//   Bit 10             = TI pin 10 (INT9,  row, GPIO 16)
//   Bit 11             = TI pin 11 (INT10, row, GPIO 15)
//   Bit 12             = TI pin 12 (2Y0,   col, GPIO 7)
//   Bit 13             = TI pin 13 (2Y1,   col, GPIO 6)
//   Bit 14             = TI pin 14 (2Y2,   col, GPIO 5)
//   Bit 15 (rightmost) = TI pin 15 (2Y3,   col, GPIO 4)
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
  PIN_ALPHA_LOCK, // TI pin 6  (P5 / alpha lock -- wired in v4, bench use only)
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

  // Skip any unmapped entry (none in v4 -- alpha lock is wired; kept as a
  // guard in case a pin is ever unmapped again)
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

  // Release all pins and exit debug/cycle/observe mode
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
    if (strobeObserveMode)
    {
      strobeObserveStop();
    }
    else
    {
      attachMatrixIsrs();
    }
    Serial.println("DEBUG: All pins released. Normal mode.");
    return;
  }

  // Cycle test mode
  if (input.equalsIgnoreCase("cycle"))
  {
    detachMatrixIsrs();
    debugMode = true;
    cycleMode = true;
    cyclePinIndex = 0;
    cycleLastTime = 0;
    Serial.println("CYCLE: Toggling each TI pin for 1 second. Type 'off' to stop.");
    return;
  }

  // Strobe observation mode (passive scope-in-firmware)
  if (input.equalsIgnoreCase("observe"))
  {
    debugMode = true;   // pause the normal updateRowOutputs() loop
    cycleMode = false;
    strobeObserveStart();
    return;
  }

  // Expect a 15-digit binary string
  if (input.length() != 15)
  {
    Serial.println("DEBUG: Enter 15 binary digits (e.g. 110000001000000)");
    Serial.println("       or 'cycle'   to cycle through all pins");
    Serial.println("       or 'observe' to log column edges from a real TI");
    Serial.println("       or 'off'     to release all pins / stop observing");
    return;
  }

  detachMatrixIsrs();
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

  attachMatrixIsrs();
}

void loop()
{
  processSerialDebug();

  // Drain the strobe-observation ring buffer to serial. Active only when
  // 'observe' mode is on; otherwise this is a one-flag-check no-op.
  strobeObserveDrain();

  if (!debugMode)
  {
#ifdef INPUT_USB
    usbHost.task();
    usbInUse = (usbHost.usbTransferSize > 0);
#endif

#ifdef INPUT_BLE
    bleTask();
#endif

#ifdef ENABLE_TYPE_AHEAD_BUFFER
    processTypeAheadBuffer();
#endif

    // Row outputs are driven by the timer-sampled column scanner
    // (onMatrixTimer); nothing to do here.
  }

  updateLed();
}
