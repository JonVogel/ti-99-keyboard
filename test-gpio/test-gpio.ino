/*
 * ESP32-S3 LBGE - Left Header GPIO Toggle Test
 *
 * Toggles all usable GPIOs on the left header on/off once per second.
 * GPIO46 is skipped (restricted on LBGE).
 * Pins 1-2 are 3V3, pin 3 is RST, pins 21-22 are 5V/GND — all skipped.
 *
 * Monitor with serial (115200) to see which pin is toggling.
 * Onboard RGB LED (GPIO 48) toggles green in sync.
 */

#define PIN_LED 48
#define LED_BRIGHTNESS 20

static const int leftPins[] =
{
  4, 5, 6, 7, 15, 16, 17, 18,   // header pins 4-11
  8, 3,                           // header pins 12-13 (gap)
  // GPIO46 skipped (pin 14)
  9, 10, 11, 12, 13, 14           // header pins 15-20
};

static const int numPins = sizeof(leftPins) / sizeof(leftPins[0]);
static bool pinState = false;

void setup()
{
  Serial.begin(115200);
  delay(1000);
  Serial.println("ESP32-S3 LBGE - Left Header GPIO Toggle Test");
  Serial.println("=============================================");

  for (int i = 0; i < numPins; i++)
  {
    pinMode(leftPins[i], OUTPUT);
    digitalWrite(leftPins[i], LOW);
  }
}

void loop()
{
  pinState = !pinState;

  Serial.printf("All left-header GPIOs -> %s\n", pinState ? "HIGH" : "LOW");

  for (int i = 0; i < numPins; i++)
  {
    digitalWrite(leftPins[i], pinState ? HIGH : LOW);
  }

  rgbLedWrite(PIN_LED, 0, pinState ? LED_BRIGHTNESS : 0, 0);

  delay(1000);
}
