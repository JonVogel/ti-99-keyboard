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

enum TestMode
{
  MODE_CLEAN,
  MODE_OPEN_DRAIN,
  MODE_MULTI,
  MODE_SLOW
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
  Serial.println("  c - Clean push-pull  500Hz on GPIO 4");
  Serial.println("  o - Open-drain row   500Hz on GPIO 4");
  Serial.println("  m - Multi-channel    500Hz on GPIO 4/5/6/7");
  Serial.println("  s - Slow blink        2Hz on GPIO 4");
  Serial.println("  ? - help");
  Serial.println();
}

static void switchMode(char c)
{
  allInputs();
  switch (c)
  {
    case 'c':
      pinMode(PIN_CH1, OUTPUT);
      currentMode = MODE_CLEAN;
      Serial.println("Mode: CLEAN push-pull (GPIO 4, 500Hz)");
      break;
    case 'o':
      currentMode = MODE_OPEN_DRAIN;
      Serial.println("Mode: OPEN-DRAIN (GPIO 4 toggles OUTPUT-LOW / INPUT, 500Hz)");
      break;
    case 'm':
      pinMode(PIN_CH1, OUTPUT);
      pinMode(PIN_CH2, OUTPUT);
      pinMode(PIN_CH3, OUTPUT);
      pinMode(PIN_CH4, OUTPUT);
      currentMode = MODE_MULTI;
      Serial.println("Mode: MULTI (GPIO 4/5/6/7, 500Hz, 90deg offset)");
      break;
    case 's':
      pinMode(PIN_CH1, OUTPUT);
      currentMode = MODE_SLOW;
      Serial.println("Mode: SLOW blink (GPIO 4, 2Hz)");
      break;
    case '?':
    case 'h':
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
  printHelp();
  Serial.println("Starting in mode 'c' (clean push-pull).");
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
      break;
    }
    case MODE_SLOW:
    {
      digitalWrite(PIN_CH1, HIGH);
      delay(250);
      digitalWrite(PIN_CH1, LOW);
      delay(250);
      break;
    }
  }
}
