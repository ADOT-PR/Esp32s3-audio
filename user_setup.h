// ═══════════════════════════════════════════════════════════════════════════
//  TFT_eSPI  User_Setup.h
//  Copy this file to:  Arduino/libraries/TFT_eSPI/User_Setup.h
//  (replace the existing file — back it up first if needed)
// ═══════════════════════════════════════════════════════════════════════════

#define ST7796_DRIVER      // ST7796S controller (most 3.5” 320x480 boards)

#define TFT_WIDTH  320
#define TFT_HEIGHT 480

// SPI pins — match the lonely binary Gold Edition screw terminal wiring
#define TFT_MOSI  6        // SDA label on your TFT board
#define TFT_SCLK  5        // SCL label on your TFT board
#define TFT_CS    8
#define TFT_DC    9
#define TFT_RST   10
// TFT_BL (GPIO 11) is controlled with analogWrite() in the sketch — not here
// TFT_MISO is NOT defined — display is write-only, no data comes back

#define SPI_FREQUENCY       40000000   // 40 MHz — stable; try 80000000 if display is solid
#define SPI_READ_FREQUENCY  20000000

// Fonts — comment out any you don’t need to save a little flash
#define LOAD_GLCD            // Font 1  — 8px built-in
#define LOAD_FONT2           // Font 2  — 16px
#define LOAD_FONT4           // Font 4  — 26px
#define LOAD_FONT6           // Font 6  — 48px (numbers)
#define LOAD_FONT7           // Font 7  — 48px 7-seg style
#define LOAD_FONT8           // Font 8  — 75px (numbers only)
#define LOAD_GFXFF           // Enable GFX Free Fonts

#define SMOOTH_FONT          // Anti-aliased fonts (uses extra flash)
