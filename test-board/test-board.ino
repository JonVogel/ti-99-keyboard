/*
 * TI-99/4A Keyboard Adapter - Assembled Board Test
 *
 * Bench-tests a freshly-assembled adapter PCB by driving each TI
 * keyboard connector pin individually and reporting column input
 * state. Addressed by TI pin number (1-15), so you can probe the
 * connector with a DMM and type the pin you're touching.
 *
 * Commands (line-buffered, ends with newline):
 *   1..15        Drive that TI pin LOW (rows) or report state (cols, NC).
 *                Releases any previously-driven pin first.
 *                Pin 6 is Alpha Lock (NC) - reports an error.
 *   <enter>      Step to the next row pin in sequence (1,2,3,4,5,7,10,11).
 *   off / 0      Release all pins back to high-Z INPUT.
 *   r            Read+print current state of all 6 column inputs.
 *   m            Toggle 2 Hz column-monitor mode (prints on change).
 *   ?            Print pin map.
 *
 * Open-drain mode: rows are driven LOW or released to INPUT (high-Z);
 * the BSS138 BOB's 10k pull-up to 5V handles the HIGH state at the TI
 * side. Same scheme as the main sketch's updateRowOutputs().
 *
 * Board settings:
 *   Board: "ESP32S3 Dev Module"
 *   USB CDC On Boot: "Disabled" (use UART for serial monitor)
 */

#include <Arduino.h>

#define PIN_LED        48
#define LED_BRIGHTNESS 20

struct PinMap
{
  uint8_t ti_pin;
  const char* signal;
  int8_t gpio;       // -1 = no GPIO (NC)
  bool is_row;       // true = ESP32->TI output, false = TI->ESP32 input
};

static const PinMap PINS[] =
{
  { 1, "INT5",   4, true  },
  { 2, "INT6",   5, true  },
  { 3, "INT8",   6, true  },
  { 4, "INT4",   7, true  },
  { 5, "INT3",  15, true  },
  { 6, "P5",    -1, false },
  { 7, "INT7",  16, true  },
  { 8, "1Y1",   17, false },
  { 9, "1Y0",   18, false },
  {10, "INT9",   9, true  },
  {11, "INT10", 10, true  },
  {12, "2Y0",   11, false },
  {13, "2Y1",   12, false },
  {14, "2Y2",   13, false },
  {15, "2Y3",   14, false },
};
static const size_t NUM_PINS = sizeof(PINS) / sizeof(PINS[0]);

static const uint8_t WALK_SEQUENCE[] = { 1, 2, 3, 4, 5, 7, 10, 11 };
static const size_t WALK_LEN = sizeof(WALK_SEQUENCE) / sizeof(WALK_SEQUENCE[0]);

static int activeTiPin = 0;       // 0 = nothing driven
static size_t walkIndex = 0;
static bool monitorOn = false;
static uint8_t lastColMask = 0;

static void setLed(uint8_t r, uint8_t g, uint8_t b)
{
  rgbLedWrite(PIN_LED, r, g, b);
}

// Number of column inputs in PINS[]. Mask is all-1 when every column is HIGH (idle).
static const uint8_t COL_ALL_HIGH = 0x3F;

static const PinMap* findByTiPin(int ti_pin)
{
  for (size_t i = 0; i < NUM_PINS; i++)
  {
    if (PINS[i].ti_pin == ti_pin)
    {
      return &PINS[i];
    }
  }
  return nullptr;
}

static void releaseAll()
{
  for (size_t i = 0; i < NUM_PINS; i++)
  {
    if (PINS[i].gpio >= 0)
    {
      pinMode(PINS[i].gpio, INPUT);
    }
  }
  activeTiPin = 0;
}

static void drivePinLow(const PinMap* p)
{
  pinMode(p->gpio, OUTPUT);
  digitalWrite(p->gpio, LOW);
  activeTiPin = p->ti_pin;
}

static uint8_t readColMask()
{
  uint8_t mask = 0;
  uint8_t bit = 0;
  for (size_t i = 0; i < NUM_PINS; i++)
  {
    if (!PINS[i].is_row && PINS[i].gpio >= 0)
    {
      if (digitalRead(PINS[i].gpio) == HIGH)
      {
        mask |= (1u << bit);
      }
      bit++;
    }
  }
  return mask;
}

// In monitor mode the LED reflects column input state:
//   green = all columns HIGH (idle, nothing grounded)
//   red   = at least one column pulled LOW (grounded / driven)
static void updateLedFromCols()
{
  uint8_t mask = readColMask();
  if (mask == COL_ALL_HIGH)
  {
    setLed(0, LED_BRIGHTNESS, 0);
  }
  else
  {
    setLed(LED_BRIGHTNESS, 0, 0);
  }
}

static void printColState()
{
  Serial.print("cols: ");
  for (size_t i = 0; i < NUM_PINS; i++)
  {
    if (!PINS[i].is_row && PINS[i].gpio >= 0)
    {
      int v = digitalRead(PINS[i].gpio);
      Serial.printf("[ti%-2u %s GPIO%-2d=%c] ",
                    PINS[i].ti_pin, PINS[i].signal,
                    PINS[i].gpio, v ? 'H' : 'L');
    }
  }
  Serial.println();
}

static void printPinMap()
{
  Serial.println();
  Serial.println("TI pin  Signal  GPIO  Direction");
  Serial.println("------  ------  ----  ---------");
  for (size_t i = 0; i < NUM_PINS; i++)
  {
    const PinMap& p = PINS[i];
    if (p.gpio < 0)
    {
      Serial.printf(" %2u     %-6s   --   NC (alpha lock)\n",
                    p.ti_pin, p.signal);
    }
    else
    {
      Serial.printf(" %2u     %-6s  %3d   %s\n",
                    p.ti_pin, p.signal, p.gpio,
                    p.is_row ? "row (ESP32->TI)" : "col (TI->ESP32)");
    }
  }
  Serial.println();
}

static void printHelp()
{
  Serial.println();
  Serial.println("TI-99/4A Adapter Board Test");
  Serial.println("===========================");
  Serial.println("  1..15       drive TI pin N LOW (rows) or report (cols)");
  Serial.println("  <enter>     step to next row in walk sequence");
  Serial.println("  off / 0     release everything");
  Serial.println("  r           read column states once");
  Serial.println("  m           toggle 2Hz column-change monitor");
  Serial.println("  ?           print pin map");
  Serial.println();
}

static void handleSelect(int ti_pin)
{
  const PinMap* p = findByTiPin(ti_pin);
  if (p == nullptr)
  {
    Serial.printf("ERR: TI pin %d out of range (1-15)\n", ti_pin);
    setLed(LED_BRIGHTNESS, 0, 0);
    return;
  }
  releaseAll();
  if (p->gpio < 0)
  {
    Serial.printf("pin %u -> %s -> NC (alpha lock, no GPIO)\n",
                  p->ti_pin, p->signal);
    setLed(LED_BRIGHTNESS, 0, 0);
    return;
  }
  if (p->is_row)
  {
    drivePinLow(p);
    Serial.printf("pin %u -> %s -> GPIO %d driven LOW\n",
                  p->ti_pin, p->signal, p->gpio);
    setLed(0, LED_BRIGHTNESS, 0);
  }
  else
  {
    int v = digitalRead(p->gpio);
    Serial.printf("pin %u -> %s -> GPIO %d INPUT, reads %s\n",
                  p->ti_pin, p->signal, p->gpio, v ? "HIGH" : "LOW");
    setLed(0, LED_BRIGHTNESS / 2, LED_BRIGHTNESS / 2);
  }
}

static void stepWalk()
{
  uint8_t ti_pin = WALK_SEQUENCE[walkIndex];
  walkIndex = (walkIndex + 1) % WALK_LEN;
  handleSelect(ti_pin);
}

static void handleCommand(String cmd)
{
  cmd.trim();
  cmd.toLowerCase();

  if (cmd.length() == 0)
  {
    stepWalk();
    return;
  }
  if (cmd == "?")
  {
    printPinMap();
    return;
  }
  if (cmd == "off" || cmd == "0")
  {
    releaseAll();
    Serial.println("released all pins");
    setLed(LED_BRIGHTNESS / 4, 0, 0);
    return;
  }
  if (cmd == "r")
  {
    printColState();
    return;
  }
  if (cmd == "m")
  {
    monitorOn = !monitorOn;
    Serial.printf("monitor: %s\n", monitorOn ? "ON" : "OFF");
    if (monitorOn)
    {
      lastColMask = readColMask();
      printColState();
      updateLedFromCols();
    }
    else
    {
      setLed(activeTiPin ? 0 : LED_BRIGHTNESS / 4,
             activeTiPin ? LED_BRIGHTNESS : 0,
             0);
    }
    return;
  }

  // Numeric: TI pin number
  char* end = nullptr;
  long n = strtol(cmd.c_str(), &end, 10);
  if (end != cmd.c_str() && *end == '\0')
  {
    handleSelect((int)n);
    return;
  }

  Serial.printf("ERR: unknown command '%s' (try '?')\n", cmd.c_str());
  setLed(LED_BRIGHTNESS, 0, 0);
}

void setup()
{
  Serial.begin(115200);
  delay(200);
  releaseAll();
  printHelp();
  printPinMap();
  Serial.println("Idle. Type 1..15 or press <enter> to start walking.");
  setLed(LED_BRIGHTNESS / 4, 0, 0);
}

void loop()
{
  static String buf;
  while (Serial.available())
  {
    char c = (char)Serial.read();
    if (c == '\r')
    {
      continue;
    }
    if (c == '\n')
    {
      handleCommand(buf);
      buf = "";
    }
    else
    {
      buf += c;
      if (buf.length() > 32)
      {
        buf = "";
      }
    }
  }

  if (monitorOn)
  {
    static unsigned long lastPrint = 0;
    static unsigned long lastLed = 0;
    unsigned long now = millis();
    if (now - lastLed >= 50)
    {
      lastLed = now;
      updateLedFromCols();
    }
    if (now - lastPrint >= 500)
    {
      lastPrint = now;
      uint8_t m = readColMask();
      if (m != lastColMask)
      {
        lastColMask = m;
        printColState();
      }
    }
  }
}
