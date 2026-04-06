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
 *   ESP32-S3 GPIO  ->  TXS0108E level shifter  ->  TI-99/4A keyboard connector
 *
 *   Column inputs (active-low, from 74LS156 outputs):
 *     GPIO 4  <- 1Y1 (column 0)
 *     GPIO 5  <- 2Y1 (column 1)
 *     GPIO 6  <- 2Y2 (column 2)
 *     GPIO 7  <- 2Y3 (column 3)
 *     GPIO 15 <- 1Y0 (column 4)
 *     GPIO 16 <- 2Y0 (column 5)
 *
 *   Row outputs (active-low, to TMS9901 INT lines):
 *     GPIO 17 -> INT3  (row 0)
 *     GPIO 18 -> INT4  (row 1)
 *     GPIO  8 -> INT5  (row 2)
 *     GPIO  9 -> INT6  (row 3)
 *     GPIO 10 -> INT10 (row 4)
 *     GPIO 11 -> INT8  (row 5)
 *     GPIO 12 -> INT9  (row 6)
 *     GPIO 13 -> INT7  (row 7)
 *
 *   Alpha Lock output:
 *     GPIO 14 -> P5 / Alpha Lock line
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
#include <BLEDevice.h>
#include <BLESecurity.h>
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
#define PIN_COL_1Y1   4
#define PIN_COL_2Y1   5
#define PIN_COL_2Y2   6
#define PIN_COL_2Y3   7
#define PIN_COL_1Y0  15
#define PIN_COL_2Y0  16

#define PIN_ROW_INT3  17
#define PIN_ROW_INT4  18
#define PIN_ROW_INT5   8
#define PIN_ROW_INT6   9
#define PIN_ROW_INT10 10
#define PIN_ROW_INT8  11
#define PIN_ROW_INT9  12
#define PIN_ROW_INT7  13

#define PIN_ALPHA_LOCK 14

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

void processHidReport(const uint8_t *report, int len)
{
  if (len < 8)
  {
    return;
  }

  uint8_t modifiers = report[0];
  const uint8_t *keys = &report[2];

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

static BLEUUID hidServiceUUID((uint16_t)0x1812);
static BLEUUID reportCharUUID((uint16_t)0x2A4D);

static BLEClient *pClient = nullptr;
static BLEAdvertisedDevice *pTargetDevice = nullptr;
static volatile bool bleConnected = false;
static volatile bool bleReady = false;
static volatile bool bleDoConnect = false;
static volatile bool bleDoScan = false;

#define PIN_BOOT_BUTTON 0

// Notification callback — receives HID key reports
static void bleNotifyCallback(
  BLERemoteCharacteristic *pChar,
  uint8_t *pData,
  size_t length,
  bool isNotify)
{
  if (length >= 8)
  {
    processHidReport(pData, length);
  }
}

// Client callbacks
class BleClientCallbacks : public BLEClientCallbacks
{
  void onConnect(BLEClient *client)
  {
    Serial.println("BLE: Connected.");
    bleConnected = true;
  }

  void onDisconnect(BLEClient *client)
  {
    Serial.println("BLE: Disconnected.");
    bleConnected = false;
    bleReady = false;
    memset((void *)keyState, 0, sizeof(keyState));
    setLedState(LED_SCANNING);
    bleDoScan = true;
  }
};

// Connect to keyboard and subscribe to HID input reports
bool bleConnectToServer()
{
  Serial.printf("BLE: Connecting to %s (%s)...\n",
                pTargetDevice->getName().c_str(),
                pTargetDevice->getAddress().toString().c_str());

  pClient = BLEDevice::createClient();
  pClient->setClientCallbacks(new BleClientCallbacks());
  pClient->connect(pTargetDevice);
  pClient->setMTU(185);

  BLERemoteService *pHidService = pClient->getService(hidServiceUUID);
  if (pHidService == nullptr)
  {
    Serial.println("BLE: HID service not found.");
    pClient->disconnect();
    return false;
  }
  Serial.println("BLE: Found HID service.");

  int subscribed = 0;
  std::map<std::string, BLERemoteCharacteristic *> *pCharMap =
    pHidService->getCharacteristics();

  for (auto const &entry : *pCharMap)
  {
    BLERemoteCharacteristic *pChar = entry.second;

    if (pChar->getUUID().equals(reportCharUUID) && pChar->canNotify())
    {
      BLERemoteDescriptor *pReportRef =
        pChar->getDescriptor(BLEUUID((uint16_t)0x2908));

      if (pReportRef != nullptr)
      {
        String refValue = pReportRef->readValue();
        if (refValue.length() >= 2)
        {
          uint8_t reportId = refValue[0];
          uint8_t reportType = refValue[1];
          if (reportType == 1)
          {
            pChar->registerForNotify(bleNotifyCallback);
            subscribed++;
            Serial.printf("BLE: Subscribed to report ID %d.\n", reportId);
          }
        }
      }
      else
      {
        pChar->registerForNotify(bleNotifyCallback);
        subscribed++;
      }
    }
  }

  if (subscribed == 0)
  {
    Serial.println("BLE: No input reports found.");
    pClient->disconnect();
    return false;
  }

  bleReady = true;
  setLedState(LED_CONNECTED);
  Serial.printf("BLE: Ready. %d input report(s).\n", subscribed);
  return true;
}

// Scan callback
class BleScanCallbacks : public BLEAdvertisedDeviceCallbacks
{
  void onResult(BLEAdvertisedDevice advertisedDevice)
  {
    String addr = advertisedDevice.getAddress().toString();
    String name = advertisedDevice.getName();
    bool isHid = advertisedDevice.haveServiceUUID() &&
                 advertisedDevice.isAdvertisingService(hidServiceUUID);
    bool isKnown = addr.startsWith("f4:ee:25:") ||
                   (name.indexOf("L75") >= 0) ||
                   (name.indexOf("BT5.0") >= 0);

    if (isHid || isKnown)
    {
      Serial.printf("BLE: Found keyboard: %s (%s)\n",
                    name.c_str(), addr.c_str());

      BLEDevice::getScan()->stop();

      if (pTargetDevice != nullptr)
      {
        delete pTargetDevice;
      }
      pTargetDevice = new BLEAdvertisedDevice(advertisedDevice);
      bleDoConnect = true;
      bleDoScan = true;
    }
  }
};

void bleInit()
{
  BLEDevice::init("TI99-KB");

  BLESecurity *pSecurity = new BLESecurity();
  pSecurity->setCapability(ESP_IO_CAP_NONE);
  pSecurity->setAuthenticationMode(true, false, true);
  BLEDevice::setSecurityCallbacks(new BLESecurityCallbacks());

  BLEScan *pScan = BLEDevice::getScan();
  pScan->setAdvertisedDeviceCallbacks(new BleScanCallbacks());
  pScan->setInterval(1349);
  pScan->setWindow(449);
  pScan->setActiveScan(true);
  pScan->start(5, false);
  bleDoScan = true;

  pinMode(PIN_BOOT_BUTTON, INPUT_PULLUP);
  Serial.println("BLE: Initialized. Scanning...");
  setLedState(LED_SCANNING);
}

void bleTask()
{
  // Connect to device found during scan
  if (bleDoConnect)
  {
    bleDoConnect = false;
    if (!bleConnectToServer())
    {
      Serial.println("BLE: Connection failed.");
    }
  }

  // Keep scanning while disconnected
  if (!bleConnected && bleDoScan)
  {
    bleDoScan = false;
    setLedState(LED_SCANNING);
    BLEScan *pScan = BLEDevice::getScan();
    pScan->clearResults();
    pScan->start(5, false);
    bleDoScan = true;
  }

  // BOOT button — clear bonds and rescan for new keyboard
  if (digitalRead(PIN_BOOT_BUTTON) == LOW && !bleReady)
  {
    delay(50);
    if (digitalRead(PIN_BOOT_BUTTON) == LOW)
    {
      Serial.println("BLE: Pairing mode...");

      if (pClient != nullptr && pClient->isConnected())
      {
        pClient->disconnect();
      }

      BLEDevice::deinit(true);
      delay(500);
      BLEDevice::init("TI99-KB");

      BLESecurity *pSec = new BLESecurity();
      pSec->setCapability(ESP_IO_CAP_NONE);
      pSec->setAuthenticationMode(true, false, true);
      BLEDevice::setSecurityCallbacks(new BLESecurityCallbacks());

      BLEScan *pScan = BLEDevice::getScan();
      pScan->setAdvertisedDeviceCallbacks(new BleScanCallbacks());
      pScan->setInterval(1349);
      pScan->setWindow(449);
      pScan->setActiveScan(true);

      setLedState(LED_STARTUP);
      pScan->start(30, false);
      bleDoScan = true;

      while (digitalRead(PIN_BOOT_BUTTON) == LOW)
      {
        delay(50);
      }
    }
  }
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

  if (alphaLockActive)
  {
    pinMode(PIN_ALPHA_LOCK, OUTPUT);
    digitalWrite(PIN_ALPHA_LOCK, LOW);
  }
  else
  {
    pinMode(PIN_ALPHA_LOCK, INPUT);
  }
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

  pinMode(PIN_ALPHA_LOCK, INPUT);

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
#ifdef INPUT_USB
  usbHost.task();
#endif

#ifdef INPUT_BLE
  bleTask();
#endif

  updateRowOutputs();
  updateLed();
}
