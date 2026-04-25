/*
 * BSS138 Level Shifter Bench Test
 *
 * Validates a SparkFun BOB-12009 (or equivalent BSS138-based) 4-channel
 * bidirectional level shifter for the TI-99/4A keyboard adapter.
 *
 * Modes (switch via serial monitor at 115200):
 *   c - Clean push-pull 500Hz on GPIO 4  (basic LV->HV edge quality)
 *   o - Open-drain row emulation 500Hz on GPIO 4
 *       (mimics updateRowOutputs(): OUTPUT-LOW <-> INPUT high-Z)
 *   m - Multi-channel: GPIO 4/5/6/7 at 500Hz with 90-degree phase offset
 *   s - Slow blink 2Hz on GPIO 4 (for LED / visual test)
 *   ? - help
 *
 * Wiring (BOB-12009):
 *   LV  <- ESP32 3V3
 *   HV  <- bench 5V (or TI 5V rail)
 *   GND <- ESP32 GND, tied to bench GND
 *   LV1 <- GPIO 4   (scope CH1 here)
 *   HV1 <- (scope CH2 here, otherwise floating)
 *   LV2 <- GPIO 5   (multi-channel test)
 *   LV3 <- GPIO 6   (multi-channel test)
 *   LV4 <- GPIO 7   (multi-channel test)
 *
 * Scope: both channels DC-coupled, 1V/div, start at 1ms/div and zoom
 * in on an edge to check rise/fall shape.
 *
 * Board settings (Arduino IDE):
 *   Board: "ESP32S3 Dev Module"
 *   USB CDC On Boot: "Disabled" (use UART port for serial monitor)
 */

#define PIN_CH1 4
#define PIN_CH2 5
#define PIN_CH3 6
#define PIN_CH4 7

// Onboard WS2812 RGB LED on LBGE (GPIO 48). Used as a visible heartbeat
// so you can tell the sketch is running even without a scope.
#define PIN_LED        48
#define LED_BRIGHTNESS 20

static void setLed(uint8_t r, uint8_t g, uint8_t b)
{
  rgbLedWrite(PIN_LED, r, g, b);
}

enum TestMode
{
  MODE_CLEAN,
  MODE_OPEN_DRAIN,
  MODE_MULTI,
  MODE_SLOW,
  MODE_HOLD_HIGH,
  MODE_HOLD_LOW
};

static TestMode currentMode = MODE_CLEAN;

static void allInputs()
{
  pinMode(PIN_CH1, INPUT);
  pinMode(PIN_CH2, INPUT);
  pinMode(PIN_CH3, INPUT);
  pinMode(PIN_CH4, INPUT);
}

static void printHelp()
{
  Serial.println();
  Serial.println("BSS138 Level Shifter Bench Test");
  Serial.println("================================");
  Serial.println("  c - Clean push-pull  500Hz");
  Serial.println("  o - Open-drain row   500Hz");
  Serial.println("  m - Multi-channel    500Hz");
  Serial.println("  s - Slow blink        2Hz");
  Serial.println("  h - Hold HIGH steady  (DMM-friendly)");
  Serial.println("  l - Hold LOW  steady  (DMM-friendly)");
  Serial.println("  ? - help");
  Serial.println();
}

static void switchMode(char c)
{
  // Ignore line endings and whitespace from the serial monitor. Without
  // this, a trailing '\n' would re-enter switchMode, call allInputs(),
  // and silently drop the pins back to high-Z.
  if (c == '\n' || c == '\r' || c == ' ' || c == '\t')
  {
    return;
  }

  allInputs();
  switch (c)
  {
    case 'c':
      pinMode(PIN_CH1, OUTPUT);
      digitalWrite(PIN_CH1, LOW);
      currentMode = MODE_CLEAN;
      Serial.println("Mode: CLEAN push-pull (500Hz)");
      break;
    case 'o':
      currentMode = MODE_OPEN_DRAIN;
      Serial.println("Mode: OPEN-DRAIN (OUTPUT-LOW / INPUT, 500Hz)");
      break;
    case 'm':
      pinMode(PIN_CH1, OUTPUT); digitalWrite(PIN_CH1, LOW);
      pinMode(PIN_CH2, OUTPUT); digitalWrite(PIN_CH2, LOW);
      pinMode(PIN_CH3, OUTPUT); digitalWrite(PIN_CH3, LOW);
      pinMode(PIN_CH4, OUTPUT); digitalWrite(PIN_CH4, LOW);
      currentMode = MODE_MULTI;
      Serial.println("Mode: MULTI (500Hz, 90deg offset)");
      break;
    case 's':
      pinMode(PIN_CH1, OUTPUT);
      digitalWrite(PIN_CH1, LOW);
      currentMode = MODE_SLOW;
      Serial.println("Mode: SLOW blink (2Hz)");
      break;
    case 'h':
      pinMode(PIN_CH1, OUTPUT); digitalWrite(PIN_CH1, HIGH);
      pinMode(PIN_CH2, OUTPUT); digitalWrite(PIN_CH2, HIGH);
      pinMode(PIN_CH3, OUTPUT); digitalWrite(PIN_CH3, HIGH);
      pinMode(PIN_CH4, OUTPUT); digitalWrite(PIN_CH4, HIGH);
      currentMode = MODE_HOLD_HIGH;
      Serial.println("Mode: HOLD HIGH (all 4 channels driven HIGH)");
      break;
    case 'l':
      pinMode(PIN_CH1, OUTPUT); digitalWrite(PIN_CH1, LOW);
      pinMode(PIN_CH2, OUTPUT); digitalWrite(PIN_CH2, LOW);
      pinMode(PIN_CH3, OUTPUT); digitalWrite(PIN_CH3, LOW);
      pinMode(PIN_CH4, OUTPUT); digitalWrite(PIN_CH4, LOW);
      currentMode = MODE_HOLD_LOW;
      Serial.println("Mode: HOLD LOW (all 4 channels driven LOW)");
      break;
    case '?':
      printHelp();
      break;
    default:
      break;
  }
}

void setup()
{
  Serial.begin(115200);
  delay(200);
  allInputs();
  pinMode(PIN_CH1, OUTPUT);
  digitalWrite(PIN_CH1, LOW);
  printHelp();
  Serial.printf("PIN_CH1 = GPIO %d\n", PIN_CH1);
  Serial.println("BUILD-TAG: cornflower-badger-9213");
  Serial.println("Starting in mode 'c' (clean push-pull).");
}

static void heartbeat()
{
  // 1Hz visible blink independent of the fast test waveform.
  // Proves the main loop is running even when the scope shows nothing.
  static unsigned long lastToggle = 0;
  static bool on = false;
  unsigned long now = millis();
  if (now - lastToggle >= 500)
  {
    lastToggle = now;
    on = !on;
    setLed(0, on ? LED_BRIGHTNESS : 0, 0);
  }
}

void loop()
{
  if (Serial.available())
  {
    switchMode(Serial.read());
  }

  switch (currentMode)
  {
    case MODE_CLEAN:
    {
      digitalWrite(PIN_CH1, HIGH);
      delayMicroseconds(1000);
      digitalWrite(PIN_CH1, LOW);
      delayMicroseconds(1000);
      heartbeat();
      break;
    }
    case MODE_OPEN_DRAIN:
    {
      // Matches updateRowOutputs() in ti-99-keyboard.ino: when active,
      // drive LOW; when inactive, release to high-Z and let the shifter
      // pull-up pull the line back high. This is the real matrix path.
      pinMode(PIN_CH1, OUTPUT);
      digitalWrite(PIN_CH1, LOW);
      delayMicroseconds(1000);
      pinMode(PIN_CH1, INPUT);
      delayMicroseconds(1000);
      heartbeat();
      break;
    }
    case MODE_MULTI:
    {
      // 500Hz square wave (2000us period), each channel phase-shifted
      // by 500us so the scope shows four distinct waveforms.
      unsigned long t = micros();
      digitalWrite(PIN_CH1, (t / 1000) & 1);
      digitalWrite(PIN_CH2, ((t +  500) / 1000) & 1);
      digitalWrite(PIN_CH3, ((t + 1000) / 1000) & 1);
      digitalWrite(PIN_CH4, ((t + 1500) / 1000) & 1);
      heartbeat();
      break;
    }
    case MODE_HOLD_HIGH:
    {
      // Pins already set in switchMode. Just keep the sketch alive and
      // blink the LED so you know the sketch didn't crash.
      setLed(LED_BRIGHTNESS, LED_BRIGHTNESS, 0);  // yellow = HOLD HIGH
      delay(50);
      break;
    }
    case MODE_HOLD_LOW:
    {
      setLed(0, 0, LED_BRIGHTNESS);  // blue = HOLD LOW
      delay(50);
      break;
    }
    case MODE_SLOW:
    {
      // LED mirrors the pin exactly at 2Hz. Drives several GPIOs at
      // once so you can probe any of them — if one is dead (damaged
      // pad) but others toggle, you've isolated the fault.
      pinMode(PIN_CH1, OUTPUT);  // GPIO 16
      pinMode(PIN_CH2, OUTPUT);  // GPIO 5
      pinMode(PIN_CH3, OUTPUT);  // GPIO 6
      pinMode(PIN_CH4, OUTPUT);  // GPIO 7
      pinMode(8,       OUTPUT);  // never touched TXS - control pin
      digitalWrite(PIN_CH1, HIGH);
      digitalWrite(PIN_CH2, HIGH);
      digitalWrite(PIN_CH3, HIGH);
      digitalWrite(PIN_CH4, HIGH);
      digitalWrite(8,       HIGH);
      setLed(0, LED_BRIGHTNESS, 0);
      delay(250);
      digitalWrite(PIN_CH1, LOW);
      digitalWrite(PIN_CH2, LOW);
      digitalWrite(PIN_CH3, LOW);
      digitalWrite(PIN_CH4, LOW);
      digitalWrite(8,       LOW);
      setLed(0, 0, 0);
      delay(250);
      break;
    }
  }
}
