/*
 * test-matrix-loopback.ino
 * ---------------------------------------------------------------------------
 * Loopback self-test for the matrix GPIOs on the TI-99/4A keyboard adapter's
 * ESP32-S3 module (Hosyond N16R8). Incoming-inspection / post-solder go/no-go
 * for a bare module.
 *
 * v4 pin set: all 16 adapter GPIOs are in service on the v4 board -- 15
 * keyboard lines (including GPIO3 = alpha lock) plus GPIO14 = the SPARE
 * repair channel -- tested as 8 loopback pairs. Labels are v4 roles; for
 * the rev-3 pin set see the `v3` git tag.
 *
 * WHY THIS EXISTS: an ESD-damaged pin often loses only its INPUT stage while
 * the output driver still works. A drive-only bench check (set pin high/low,
 * scope it) therefore PASSES a bad module. That bit us on a hand-soldered
 * board -- GPIO13 drove 5V/0V perfectly but read nothing, so a whole key
 * column was dead. This test exercises BOTH directions on every pin.
 *
 * FIXTURE, bare module: socket the ESP32-S3 (e.g. across a breadboard's
 * centre channel) and jumper the 16 GPIOs into the 8 pairs below -- one
 * wire per pair.
 *
 * FIXTURE, assembled v4 board (tests the ENTIRE path -- solder joints,
 * sockets, carrier traces, and both BSS138 FETs of every BOB channel):
 * the pairs land on ADJACENT J10 pins, so the whole fixture is seven
 * 2.54mm shunt jumpers pushed onto J10 pins 1-14 plus ONE wire from
 * J10 pin 15 to the J15 SPARE pad. Board powered via module USB.
 *
 *   GPIO4  (col3/2Y3)   <->  GPIO5  (col2/2Y2)    = J10 1-2   (shunt)
 *   GPIO6  (col1/2Y1)   <->  GPIO7  (col5/2Y0)    = J10 3-4   (shunt)
 *   GPIO15 (row4/INT10) <->  GPIO16 (row6/INT9)   = J10 5-6   (shunt)
 *   GPIO17 (col4/1Y0)   <->  GPIO18 (col0/1Y1)    = J10 7-8   (shunt)
 *   GPIO8  (row7/INT7)  <->  GPIO3  (alpha/P5)    = J10 9-10  (shunt)
 *   GPIO9  (row0/INT3)  <->  GPIO10 (row1/INT4)   = J10 11-12 (shunt)
 *   GPIO11 (row5/INT8)  <->  GPIO12 (row3/INT6)   = J10 13-14 (shunt)
 *   GPIO13 (row2/INT5)  <->  GPIO14 (SPARE)       = J10 15 <-> J15 (wire)
 *
 * USE: flash, open serial @115200. It prints a PASS/FAIL table. Socket the
 * next module and press BOOT (or send any serial char) to re-run.
 *
 * BUILD: arduino-cli compile --fqbn esp32:esp32:esp32s3 test-matrix-loopback
 *        (default S3 options are fine -- no PSRAM/partition settings needed)
 *
 * READING A FAIL: each pair is tested both ways. "GPIO_X -> GPIO_Y FAIL" means
 * X could not drive Y OR Y could not read -- i.e. X's OUTPUT or Y's INPUT is
 * bad. The passing opposite direction usually isolates it (a dead input shows
 * as "GPIO_Y -> GPIO_X FAIL" while "GPIO_X -> GPIO_Y" passes -> X's input).
 * To be certain, re-jumper the suspect pin to a known-good pin.
 */

#include <Arduino.h>

#define BOOT_BUTTON 0   // not a matrix pin; used to re-trigger the test

struct Pair { int a; int b; };

// All 16 v4 fabric GPIOs in 8 loopback pairs (each pin once). Pair order
// follows J10 pin order so an assembled board is jumpered with shunts
// straight down the J10 header (see FIXTURE above).
static const Pair PAIRS[] = {
  {  4,  5 },   // J10 1-2
  {  6,  7 },   // J10 3-4
  { 15, 16 },   // J10 5-6
  { 17, 18 },   // J10 7-8
  {  8,  3 },   // J10 9-10
  {  9, 10 },   // J10 11-12
  { 11, 12 },   // J10 13-14
  { 13, 14 },   // J10 15 <-> J15 SPARE pad
};
static const int NUM_PAIRS = sizeof(PAIRS) / sizeof(PAIRS[0]);

static const char *labelFor(int gpio)
{
  switch (gpio)
  {
    case 3:  return "alpha/P5";
    case 4:  return "col3/2Y3";
    case 5:  return "col2/2Y2";
    case 6:  return "col1/2Y1";
    case 7:  return "col5/2Y0";
    case 8:  return "row7/INT7";
    case 9:  return "row0/INT3";
    case 10: return "row1/INT4";
    case 11: return "row5/INT8";
    case 12: return "row3/INT6";
    case 13: return "row2/INT5";
    case 14: return "SPARE";
    case 15: return "row4/INT10";
    case 16: return "row6/INT9";
    case 17: return "col4/1Y0";
    case 18: return "col0/1Y1";
    default: return "?";
  }
}

// Drive `driver` low then high; confirm `reader` follows both. Tests the
// driver's OUTPUT and the reader's INPUT together. Returns true if both track.
static bool testDirection(int driver, int reader)
{
  bool ok = true;

  pinMode(reader, INPUT_PULLUP);
  pinMode(driver, OUTPUT);

  digitalWrite(driver, LOW);
  delayMicroseconds(100);
  if (digitalRead(reader) != LOW)
  {
    ok = false;
  }

  digitalWrite(driver, HIGH);
  delayMicroseconds(100);
  if (digitalRead(reader) != HIGH)
  {
    ok = false;
  }

  pinMode(driver, INPUT_PULLUP);   // release
  return ok;
}

static void runTest()
{
  Serial.println();
  Serial.println("=== TI-99 adapter: matrix-pin loopback self-test ===");
  Serial.println("Jumper these pairs (one wire each):");
  for (int i = 0; i < NUM_PAIRS; i++)
  {
    Serial.printf("  GPIO%-2d (%-10s) <-> GPIO%-2d (%-10s)\n",
                  PAIRS[i].a, labelFor(PAIRS[i].a),
                  PAIRS[i].b, labelFor(PAIRS[i].b));
  }
  Serial.println("----------------------------------------------------------");

  bool allPass = true;

  for (int i = 0; i < NUM_PAIRS; i++)
  {
    int a = PAIRS[i].a;
    int b = PAIRS[i].b;

    bool aDrivesB = testDirection(a, b);   // a OUTPUT + b INPUT
    bool bDrivesA = testDirection(b, a);   // b OUTPUT + a INPUT

    if (!aDrivesB || !bDrivesA)
    {
      allPass = false;
    }

    Serial.printf("GPIO%-2d -> GPIO%-2d : %s   |   GPIO%-2d -> GPIO%-2d : %s\n",
                  a, b, aDrivesB ? "OK  " : "FAIL",
                  b, a, bDrivesA ? "OK  " : "FAIL");
  }

  Serial.println("----------------------------------------------------------");
  if (allPass)
  {
    Serial.println(">>> MODULE PASS -- all 16 pins drive AND read.");
  }
  else
  {
    Serial.println(">>> MODULE FAIL -- see FAIL above.");
    Serial.println("    'GPIO_X -> GPIO_Y FAIL' = X output bad OR Y input bad.");
    Serial.println("    (A dead input reads its own idle level, so the pin");
    Serial.println("     that FAILS as the reader is the usual ESD casualty.)");
  }
}

void setup()
{
  Serial.begin(115200);
  pinMode(BOOT_BUTTON, INPUT_PULLUP);
  delay(400);
  runTest();
  Serial.println("Socket the next module, then press BOOT (or send any key) to re-test.");
}

void loop()
{
  static bool prevBoot = HIGH;
  bool boot = digitalRead(BOOT_BUTTON);
  bool bootPressed = (prevBoot == HIGH && boot == LOW);
  prevBoot = boot;

  if (bootPressed || Serial.available())
  {
    while (Serial.available())
    {
      Serial.read();
    }
    delay(50);   // debounce
    runTest();
    Serial.println("Socket the next module, then press BOOT (or send any key) to re-test.");
  }

  delay(10);
}
