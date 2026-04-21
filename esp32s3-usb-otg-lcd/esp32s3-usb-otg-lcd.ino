/*
 * ESP32-S3-USB-OTG — "Lines" Demo (faithful port of TI-99/4A original)
 *
 * Recreation of the classic TI-99/4A Mini Memory "Lines" demo.
 * Based on disassembled source by Michael "miriki" Rittweger.
 *
 * Algorithm:
 *   - Two points with velocity vectors (X0,Y0) and (X1,Y1)
 *   - Points bounce off screen edges (reverse velocity component)
 *   - Every 80 lines: velocities randomized (-8 to +7), screen clears
 *   - Lines accumulate until periodic clear (no per-line trail erase)
 *
 * Original: TMS9918A bitmap mode, 256px192, 16 colors
 * This version: ST7789VW, 240px180 window, 65K colors
 *
 * Buttons:
 *   OK   - toggle color/monochrome mode
 *   UP   - pause/resume
 *   MENU - reset (randomize now)
 */

#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

// LCD pins
#define LCD_DC    4
#define LCD_CS    5
#define LCD_SCLK  6
#define LCD_MOSI  7
#define LCD_RST   8
#define LCD_BL    9

// Buttons
#define BTN_OK    0
#define BTN_UP   10
#define BTN_DOWN 11
#define BTN_MENU 14

// LEDs
#define LED_GREEN  15
#define LED_YELLOW 16

// The TI used 256px192; scale to fit the 240px240 display
// We'll use 240px180 centered (leaves 30px top, 30px bottom margin)
#define TI_W       256
#define TI_H       192
#define DISP_W     240
#define DISP_H     180
#define DISP_Y     30      // vertical offset

// Lines per screen clear cycle
#define LINES_PER_CYCLE 80

SPIClass lcd_spi(FSPI);
Adafruit_ST7789 tft(&lcd_spi, LCD_CS, LCD_DC, LCD_RST);

// TI-99/4A color palette (RGB565)
// Colors 0-15; 0=transparent (use black), colors 2-15 used by demo
static const uint16_t tiColors[16] =
{
  0x0000,   // 0  transparent (shown as black)
  0x0000,   // 1  black
  0x0585,   // 2  medium green
  0x2D8B,   // 3  light green
  0x0012,   // 4  dark blue
  0x0417,   // 5  light blue
  0x8000,   // 6  dark red
  0x0EBF,   // 7  cyan
  0xE000,   // 8  medium red
  0xF2A3,   // 9  light red
  0xD5C0,   // 10 dark yellow
  0xE600,   // 11 light yellow
  0x0280,   // 12 dark green
  0xB816,   // 13 magenta
  0xC618,   // 14 gray
  0xFFFF,   // 15 white
};

// Line endpoints (in TI coords 0-255 x 0-191)
int px1 = 128, py1 = 96;
int px2 = 211, py2 = 163;

// Velocity vectors
int vx0 = 2, vy0 = 4;
int vpx1 = 6, vpy1 = 8;

// Line counter (clear every LINES_PER_CYCLE)
int lineCount = 0;

// Color mode: true=color, false=monochrome (white on black)
bool colorMode = true;

// Pause state
bool paused = false;

// Button state
bool lastOk = true, lastUp = true, lastDn = true, lastMenu = true;

// Random seed (TI's initial value)
uint32_t seed = 0xD88C;

// Match the TI's RANDOM routine: add 0x1D6B, avoid 0
int tiRandom()
{
  do
  {
    seed = (seed + 0x1D6B) & 0xFFFF;
  } while (seed == 0);
  return (int)(int16_t)seed;
}

// Mask random to 3-bit signed value (-8 to +7)
int randomStep()
{
  int r = tiRandom();
  if (r < 0)
  {
    return (int)(int16_t)(r | 0xFFF8);    // force negative 3-bit
  }
  return r & 0x0007;
}

// Clear the drawing area
void clearDisplay()
{
  tft.fillRect(0, DISP_Y, DISP_W, DISP_H, ST77XX_BLACK);
}

// Draw a line mapping TI coords to display coords
void drawTiLine(int px1t, int py1t, int px2t, int py2t, uint16_t color)
{
  int dpx1 = (px1t * DISP_W) / TI_W;
  int dpy1 = (py1t * DISP_H) / TI_H + DISP_Y;
  int dpx2 = (px2t * DISP_W) / TI_W;
  int dpy2 = (py2t * DISP_H) / TI_H + DISP_Y;
  tft.drawLine(dpx1, dpy1, dpx2, dpy2, color);
}

// Randomize all four velocity components (called every LINES_PER_CYCLE)
void randomizeVelocities()
{
  vx0 = randomStep();
  vpx1 = randomStep();
  vy0 = randomStep();
  vpy1 = randomStep();

  // Avoid all-zero velocities (stuck lines)
  if (vx0 == 0 && vy0 == 0)
  {
    vx0 = 2;
  }
  if (vpx1 == 0 && vpy1 == 0)
  {
    vpx1 = 3;
  }
}

void setup()
{
  Serial.begin(115200);
  delay(500);
  Serial.println("Lines Demo - Faithful TI-99/4A Port");

  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_YELLOW, OUTPUT);
  digitalWrite(LED_GREEN, LOW);
  digitalWrite(LED_YELLOW, LOW);

  pinMode(BTN_OK, INPUT_PULLUP);
  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_MENU, INPUT_PULLUP);

  // Enable boost for battery operation
  pinMode(13, OUTPUT);
  digitalWrite(13, HIGH);

  lcd_spi.begin(LCD_SCLK, -1, LCD_MOSI, LCD_CS);
  tft.init(240, 240, SPI_MODE0);
  tft.setRotation(2);
  tft.fillScreen(ST77XX_BLACK);

  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, HIGH);

  // Seed the random number generator from an ADC read
  seed = 0xD88C ^ analogRead(0);
}

void handleButtons()
{
  bool ok = digitalRead(BTN_OK);
  bool up = digitalRead(BTN_UP);
  bool dn = digitalRead(BTN_DOWN);
  bool menu = digitalRead(BTN_MENU);

  if (!ok && lastOk)
  {
    colorMode = !colorMode;
    Serial.printf("Color mode: %s\n", colorMode ? "ON" : "OFF");
  }

  if (!up && lastUp)
  {
    paused = !paused;
    Serial.printf("Paused: %s\n", paused ? "YES" : "NO");
  }

  if (!menu && lastMenu)
  {
    // Reset and randomize
    clearDisplay();
    randomizeVelocities();
    lineCount = 0;
    px1 = 128; py1 = 96;
    px2 = 211; py2 = 163;
    Serial.println("Reset");
  }

  lastOk = ok;
  lastUp = up;
  lastDn = dn;
  lastMenu = menu;
}

void loop()
{
  handleButtons();

  if (paused)
  {
    delay(50);
    return;
  }

  // Pick color (monochrome white or random color 2-15)
  uint16_t color;
  if (colorMode)
  {
    int c = tiRandom() & 0x0F;
    if (c < 2)
    {
      c |= 2;
    }
    color = tiColors[c];
  }
  else
  {
    color = 0xFFFF;  // white
  }

  // Update endpoints by velocity
  px1 += vx0;
  py1 += vy0;
  px2 += vpx1;
  py2 += vpy1;

  // Bounce off edges (reverse velocity component, reapply)
  if (px1 < 0 || px1 >= TI_W)
  {
    vx0 = -vx0;
    px1 += vx0;
  }
  if (px2 < 0 || px2 >= TI_W)
  {
    vpx1 = -vpx1;
    px2 += vpx1;
  }
  if (py1 < 0 || py1 >= TI_H)
  {
    vy0 = -vy0;
    py1 += vy0;
  }
  if (py2 < 0 || py2 >= TI_H)
  {
    vpy1 = -vpy1;
    py2 += vpy1;
  }

  // Draw the line
  drawTiLine(px1, py1, px2, py2, color);

  // Every LINES_PER_CYCLE lines: clear and randomize
  lineCount++;
  if (lineCount >= LINES_PER_CYCLE)
  {
    lineCount = 0;
    delay(200);          // brief pause before clear
    clearDisplay();
    randomizeVelocities();
  }

  // Small delay so we can see the animation
  delay(30);
}
