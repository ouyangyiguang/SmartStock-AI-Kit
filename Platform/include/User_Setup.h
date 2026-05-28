//                            USER DEFINED SETTINGS
//   Set driver type, fonts to be loaded, pins used and SPI control method etc.

// User defined information reported by "Read_User_Setup" test & diagnostics example
#define USER_SETUP_INFO "User_Setup"

// 显存优化设置
#define BUFFER_SIZE  240
#define TFT_RAM_SAVER

// ##################################################################################
// Section 1. Call up the right driver file and any options for it
// ##################################################################################

// 显示驱动类型 - ST7789
#define ST7789_DRIVER

// 颜色顺序（蓝绿红）
#define TFT_RGB_ORDER TFT_BGR

// 屏幕尺寸
#define TFT_WIDTH  240
#define TFT_HEIGHT 320

// 不启用反色
#define TFT_INVERSION_OFF

// ##################################################################################
// Section 2. Define the pins that are used to interface with the display here
// ##################################################################################

#if defined(CONFIG_IDF_TARGET_ESP32S3)
// ESP32-S3 专用引脚定义
// 使用 HSPI 总线
#define USE_HSPI_PORT
#define TFT_MISO 13  // HSPI MISO
#define TFT_MOSI 11  // HSPI MOSI  
#define TFT_SCLK 12  // HSPI SCLK
#define TFT_CS   10  // CS 引脚
#define TFT_DC    9  // DC 引脚
#define TFT_RST   8  // 复位引脚
#define TFT_BL    3  // 背光控制

#elif defined(CONFIG_IDF_TARGET_ESP32)
// 普通 ESP32 专用引脚定义 (从 pins.h 读取)
#define TFT_MOSI 23   // SDA
#define TFT_SCLK 18   // SCL
#define TFT_CS   -1   // CS 不连接
#define TFT_DC   21   // DC 引脚
#define TFT_RST   4   // 复位引脚
#define TFT_BL   22   // 背光控制

#endif

// ##################################################################################
// Section 3. Define the fonts that are to be used here
// ##################################################################################

#define LOAD_GLCD   // Font 1. Original Adafruit 8 pixel font
#define LOAD_FONT2  // Font 2. Small 16 pixel high font
#define LOAD_FONT4  // Font 4. Medium 26 pixel font
#define LOAD_FONT6  // Font 6. Large 48 pixel font
#define LOAD_FONT7  // Font 7. 7 segment 48 pixel font
#define LOAD_FONT8  // Font 8. Large 75 pixel font
#define LOAD_GFXFF  // FreeFonts

#define SMOOTH_FONT

// ##################################################################################
// Section 4. Other options
// ##################################################################################

// SPI 频率设置
#define SPI_FREQUENCY  40000000
#define SPI_READ_FREQUENCY  20000000
#define SPI_TOUCH_FREQUENCY  2500000