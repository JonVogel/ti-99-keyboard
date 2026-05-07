/*
 * Minimal display test for Sunton ESP32-8048S043C-I.
 * Arduino_GFX 1.6.5+ API. Writes "Hello World" and cycles colors.
 */

#include <Arduino_GFX_Library.h>

// RGB565 color constants (the library dropped its own defines in 1.6.x)
#define BLACK   0x0000
#define WHITE   0xFFFF
#define RED     0xF800
#define GREEN   0x07E0
#define BLUE    0x001F
#define YELLOW  0xFFE0
#define CYAN    0x07FF
#define MAGENTA 0xF81F

#define TFT_BL 2

Arduino_ESP32RGBPanel *bus = new Arduino_ESP32RGBPanel(
    40 /* DE */, 41 /* VSYNC */, 39 /* HSYNC */, 42 /* PCLK */,
    45 /* R0 */, 48 /* R1 */, 47 /* R2 */, 21 /* R3 */, 14 /* R4 */,
    5  /* G0 */, 6  /* G1 */, 7  /* G2 */, 15 /* G3 */, 16 /* G4 */, 4 /* G5 */,
    8  /* B0 */, 3  /* B1 */, 46 /* B2 */, 9  /* B3 */, 1  /* B4 */,
    0 /* hsync_polarity */, 8 /* hsync_front_porch */, 4 /* hsync_pulse_width */, 8 /* hsync_back_porch */,
    0 /* vsync_polarity */, 8 /* vsync_front_porch */, 4 /* vsync_pulse_width */, 8 /* vsync_back_porch */,
    1 /* pclk_active_neg */, 16000000 /* prefer_speed */
);

Arduino_RGB_Display *gfx = new Arduino_RGB_Display(
    800 /* width */, 480 /* height */, bus, 0 /* rotation */, true /* auto_flush */
);

void setup()
{
  Serial.begin(115200);
  delay(200);
  Serial.println("display-test starting");

  gfx->begin();
  gfx->fillScreen(BLACK);

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  gfx->setTextSize(4);
  gfx->setTextColor(RED);
  gfx->setCursor(20, 20);
  gfx->println("Hello World");

  gfx->setTextColor(GREEN);
  gfx->setCursor(20, 80);
  gfx->println("800x480 RGB");

  gfx->setTextColor(YELLOW);
  gfx->setCursor(20, 140);
  gfx->println("ESP32-S3");
}

void loop()
{
  static uint16_t colors[] = {RED, GREEN, BLUE, YELLOW, CYAN, MAGENTA, WHITE};
  static int idx = 0;
  gfx->fillRect(20, 220, 400, 40, colors[idx]);
  idx = (idx + 1) % (sizeof(colors) / sizeof(colors[0]));
  delay(500);
}
