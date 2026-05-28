//#define TFT_ESPI_ENABLE_DMA
#include "DispGP.h"


void dGPscreen2(JsonObject data, int pageIndexInput,bool ztw,bool ztm,const String& hourmin,const String& mac) {
    // 内存安全检查
    uint32_t freeHeap = ESP.getFreeHeap();
    if (freeHeap < 50000) { // 如果内存不足50KB
        Serial.printf("[WARN] dGPscreen2: 内存不足，跳过显示: %lu\n", freeHeap);
        return;
    }
    
    // JSON数据有效性检查
    if (data.isNull()) {
        Serial.println("[WARN] dGPscreen2: JSON数据为空");
        return;
    }
    
    // 显示顶部状态栏
    tft.fillRect(0, 0, 240, 33, TFT_BLACK);
    displayNetstatus(ztw, ztm);
    tft.setTextFont(1); 
    tft.setTextSize(1);
    tft.setCursor(209, 11);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.print(hourmin);
    
    // 显示城市和天气信息
    if (data.containsKey("city")) {
        const char* city = data["city"];
        const char* tiqi_temp = data["now"]["temp"] | ""; // 使用空字符串默认值
        const char* tiqi_wind = data["now"]["wind"] | "";
        
        displayChineseString(city, 0, 0, TFT_BLUE, NO_BACKGROUND, 2, 240);
        displayChineseString(tiqi_wind, 72, 0, TFT_RED, NO_BACKGROUND, 1, 350);
        displayChineseString(tiqi_temp, 72, 17, TFT_RED, NO_BACKGROUND, 1, 350);
    }
    
    // 检查设备绑定状态
    if (data.containsKey("cs")) {
        int isBound = 0;
        if (data["cs"].is<int>()) {
            isBound = data["cs"].as<int>();
        }
        
        if (isBound == 0) {
            displayChineseString("请打开小程序扫码绑定设备", 18, 120, TFT_GREEN, TFT_BLACK, 1, 240);
            displayChineseString("绑定后自定义股票代码", 36, 145, TFT_RED, NO_BACKGROUND, 1, 240);
            tft.fillRect(57, 166, 125, 125, TFT_WHITE);
            generateQRCode(mac.c_str(), 62, 170);
            return;
        }
    }
    
    // 清空股票显示区域
    tft.fillRect(0, 111, 240, 320, TFT_BLACK);
    
    // 检查股票数据
    if (!data.containsKey("set") || !data["set"]["gp"].is<JsonArray>()) {
        displayChineseString("未加载到数据，可断电重启..", 80, 140, TFT_YELLOW, NO_BACKGROUND, 1, 240);
        return;
    }
    
    JsonArray gp_data = data["set"]["gp"].as<JsonArray>();
    const int itemsPerPage = 4;
    int totalItems = gp_data.size();
    
    // 数据有效性检查
    if (totalItems <= 1) {
        displayChineseString("万事俱备！还差一步！", 50, 140, TFT_RED, NO_BACKGROUND, 1, 240);
        displayChineseString("请您前往小程序设置股票代码。", 10, 165, TFT_RED, NO_BACKGROUND, 1, 240);
        return;
    }
    
    int validItems = totalItems - 1; // 跳过第0个（通常是标题行）
    if (validItems <= 0) {
        displayChineseString("暂无股票数据", 80, 140, TFT_YELLOW, NO_BACKGROUND, 1, 240);
        return;
    }
    
    // 计算分页
    int totalPages = (validItems + itemsPerPage - 1) / itemsPerPage;
    if (totalPages == 0) totalPages = 1;
    
    int currentPage = pageIndexInput % totalPages;
    int startIndex = 1 + (currentPage * itemsPerPage);
    
    // 边界检查
    if (startIndex >= totalItems) {
        startIndex = 1;
        currentPage = 0;
    }
    
    // 显示股票数据
    int shown = 0;
    int txtY = 114;
    
    for (int i = startIndex; i < totalItems && shown < itemsPerPage; i++) {
        // 数组越界检查
        if (i >= gp_data.size()) {
            Serial.printf("[ERROR] dGPscreen2: JSON数组越界 i=%d, size=%d\n", i, gp_data.size());
            break;
        }
        
        JsonVariant value = gp_data[i];
        if (value.isNull()) {
            continue;
        }
        
        // 使用const char*避免String对象创建
        const char* code = value["co"] | "";
        const char* price = value["price"] | "";
        const char* change = value["ch"] | "";
        const char* name = value["name"] | "";
        const char* area = value["ex"] | "";
        const char* status = value["st"] | "";
        const char* currency = value["cu"] | "";
        const char* volume = value["vo"] | "";
        const char* amount = value["am"] | "";
        const char* high = value["hi"] | "";
        const char* low = value["lo"] | "";
        const char* rate = value["ra"] | "";
        
        // 确定颜色方案
        ColorScheme scheme;
        bool isNegative = false;
        
        // 安全的涨跌判断
        if (change && change[0] != '\0') {
            isNegative = (change[0] == '-') || (strstr(change, "－") != NULL);
        }
        
        scheme = isNegative ? SCHEME_GREEN : SCHEME_RED;
        
        // 绘制股票块
        drawStockBlock(
            code, price, change, 0,
            txtY, 240, 50,
            scheme, MODE_NEON,
            name, area, status, currency, volume, amount, high, low, rate
        );
        
        txtY += 52;
        shown++;
        
        // 每处理一个股票后检查内存
        if (shown % 2 == 0) {
            uint32_t currentHeap = ESP.getFreeHeap();
            if (currentHeap < 30000) {
                Serial.printf("[WARN] dGPscreen2: 内存紧张，停止显示更多股票: %lu\n", currentHeap);
                break;
            }
        }
    }
    
    // 显示页码信息（调试用）
    // Serial.printf("[DEBUG] dGPscreen2: 显示第%d页/%d页，显示%d个股票\n", 
    //               currentPage + 1, totalPages, shown);
}

// 绘制股票信息方块（优化版，使用const char*减少内存分配）
void drawStockBlock(const char* symbol, const char* price, const char* change,
                    int x, int y, int width, int height,
                    ColorScheme scheme = SCHEME_BLUE,
                    DisplayMode mode = MODE_NORMAL,
                    const char* name = "",              // 中文名称
                    const char* exchange = "",          // 中文交易所
                    const char* status = "",            // 中文状态
                    const char* currency = "",          // 币种
                    const char* volume = "",
                    const char* amount = "",
                    const char* high = "",
                    const char* low = "",
                    const char* rate = ""
                    ) { 

  // 颜色配置
  uint16_t primaryColor, secondaryColor, textColor, accentColor;
  selectColors(scheme, primaryColor, secondaryColor, textColor, accentColor);
  applyDisplayMode(mode, primaryColor, secondaryColor, textColor, accentColor);
  
  // 安全的涨跌判断
  bool isPositive = false;
  if (change && change[0] != '\0') {
    isPositive = (change[0] == '-') || (strstr(change, "－") != NULL);
  }
  
  // 背景
  tft.fillRoundRect(x, y, width, height, 4, primaryColor);
  tft.drawRoundRect(x, y, width, height, 4, secondaryColor);

  // 顶部光泽
  tft.fillRoundRect(x + 2, y + 2, width - 4, height / 6, 3, blendColors(primaryColor, TFT_WHITE, 25));

  int padding = 4;
  int lineY = y + padding;
  
  // 组合交易所和股票名称（避免创建String对象）
  char nameBuffer[64] = {0};
  if (exchange && exchange[0] != '\0') {
    strcat(nameBuffer, exchange);
  }
  if (name && name[0] != '\0') {
    strcat(nameBuffer, name);
  }
  
  uint16_t titleColor = isPositive ? 0x3FE0 : 0xAA5F;
  displayChineseString(nameBuffer, x + padding, lineY, titleColor, NO_BACKGROUND, 1, 240);
  lineY += 14;
  
  uint16_t PriceColor = isPositive ? TFT_CYAN : 0xFFE0;
  // 行2：价格 + 涨跌幅
  tft.setTextFont(2);
  tft.setTextSize(2);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(PriceColor);  
  
  if (price && price[0] != '\0') {
    tft.drawString(price, x + padding, lineY);
  }

  tft.setTextSize(1);
  tft.setTextFont(1);
  
  // 计算价格文本宽度
  int priceWidth = 0;
  if (price && price[0] != '\0') {
    priceWidth = getTextPixelWidth(price, 1);
  }
  
  int currency_x = x + priceWidth + padding + 15;
  
  if (currency && currency[0] != '\0') {
    tft.drawString(currency, currency_x, lineY + 6);   // 币种
  }
  
  // 交易股数
  char volumeBuffer[32] = {0};
  if (volume && volume[0] != '\0') {
    strcat(volumeBuffer, volume);
  }
  
  int volumeWidth = getTextPixelWidth(volumeBuffer, 1);
  int currency_x2 = width - volumeWidth - 6;
  
  if (volumeBuffer[0] != '\0') {
    displayChineseString(volumeBuffer, currency_x2, lineY - 3, 0xFD20, primaryColor, 1, 240);
  }

  // 涨跌幅颜色
  uint16_t changeColor = isPositive ? TFT_GREEN : TFT_RED;
  if (mode == MODE_NEON) {
    changeColor = isPositive
      ? blendColors(TFT_GREEN, TFT_YELLOW, 50)
      : blendColors(TFT_RED, TFT_MAGENTA, 50);
  }

  // 交易所状态
  if (status && status[0] != '\0') {
    int statusWidth = getTextPixelWidth(status, 1);
    int currency_x3 = width - statusWidth - 6;
    displayChineseString(status, currency_x3, lineY + 12, 0xFFFF, NO_BACKGROUND, 1, 240);
  }

  // 涨跌百分比
  tft.setTextSize(2);
  tft.setTextFont(1);
  uint16_t bfbColor = isPositive ? 0xFFC0 : 0xFB00;
  tft.setTextColor(bfbColor);
  tft.setTextDatum(TL_DATUM);
  
  if (change && change[0] != '\0') {
    tft.drawString(change, currency_x + 17, lineY + 14);
  }

  // 高/低/换手率
  lineY -= 15;
  tft.setTextSize(1);
  tft.setTextFont(1);
  
  char statusBuffer[48] = {0};
  if (high && high[0] != '\0') {
    strcat(statusBuffer, "H");
    strcat(statusBuffer, high);
  }
  if (low && low[0] != '\0') {
    strcat(statusBuffer, "/L");
    strcat(statusBuffer, low);
  }
  if (rate && rate[0] != '\0') {
    strcat(statusBuffer, "/R");
    strcat(statusBuffer, rate);
  }
  
  if (statusBuffer[0] != '\0') {
    tft.setTextDatum(TR_DATUM);
    tft.drawString(statusBuffer, x + width - padding - 12, lineY + 1);
  }

  // ▲/▼ 箭头图标（右上角）
  int markerX = x + width - 10;
  int markerY = y + 7;
  if (!isPositive) {
    tft.fillTriangle(markerX, markerY - 4,
                     markerX - 4, markerY + 4,
                     markerX + 4, markerY + 4,
                     changeColor);
  } else {
    tft.fillTriangle(markerX, markerY + 4,
                     markerX - 4, markerY - 4,
                     markerX + 4, markerY - 4,
                     changeColor);
  }
}

// 选择颜色方案
void selectColors(ColorScheme scheme, 
                 uint16_t &primary, uint16_t &secondary, 
                 uint16_t &text, uint16_t &accent) {
  
  switch(scheme) {
    case SCHEME_BLUE:
      primary = 0x025F;    // 深蓝
      secondary = 0x051F;  // 更深的蓝
      text = TFT_WHITE;
      accent = 0x2D5F;     // 浅蓝
      break;
      
    case SCHEME_GREEN:
      primary = 0x0600;    // 深绿
      secondary = 0x0500;  // 更深的绿
      text = TFT_WHITE;
      accent = 0x3FE0;     // 鲜绿
      break;
      
    case SCHEME_RED:
      primary = 0xA000;    // 深红
      secondary = 0x8000;  // 更深的红
      text = TFT_WHITE;
      accent = 0xFB00;     // 亮红
      break;
      
    case SCHEME_PURPLE:
      primary = 0x6018;    // 深紫
      secondary = 0x4010;  // 更深的紫
      text = TFT_WHITE;
      accent = 0xAA5F;     // 亮紫
      break;
      
    case SCHEME_GOLD:
      primary = 0xBC60;    // 金色
      secondary = 0x9B40;  // 暗金
      text = TFT_BLACK;
      accent = 0xFFC0;     // 亮金
      break;
      
    case SCHEME_MONOCHROME:
    default:
      primary = 0x39C7;    // 中灰
      secondary = 0x2104;  // 深灰
      text = TFT_WHITE;
      accent = 0xCE79;     // 浅灰
      break;
  }
}


void scrollingChineseTicker(const String& content, int y,
                             uint32_t textColor, uint32_t bgColor,
                             int textSize,
                             ScrollState &state) {
  const int speed = 1;
  const int interval = 30;
  const int screenWidth = 240;

  // 检查是否是新内容
  if (content != state.lastContent) {
    state.lastContent = content;
    state.textPixelWidth = 0;

    // 计算文本实际宽度
    for (int i = 0; i < content.length();) {
      if ((content[i] & 0xF0) == 0xE0) {
        state.textPixelWidth += 16 * textSize;
        i += 3;
      } else {
        state.textPixelWidth += 12 * textSize;
        i += 1;
      }
    }

    state.textPixelWidth += 20;
    state.scrollX = 0;

    // 绘制初始文本
    tft.fillRect(0, y, screenWidth, 16 * textSize + 2, bgColor);
    displayChineseString(content.c_str(), 0, y + 1, textColor, bgColor, textSize, 241);
    return;
  }

  // 文本不超过屏幕，不滚
  if (state.textPixelWidth <= screenWidth) {
    return;
  }

  // 正常滚动
  if (millis() - state.lastTick >= interval) {
    state.lastTick = millis();
    tft.fillRect(0, y, screenWidth, 16 * textSize + 1, bgColor);
    displayChineseString(content.c_str(), -state.scrollX, y + 1, textColor, NO_BACKGROUND, textSize, 241);
    state.scrollX += speed;

    if (state.scrollX > state.textPixelWidth) {
      state.scrollX = -screenWidth;
    }
  }
}

void drawIndexBlock(const String& name, float value,float percent,
                    int x, int y, int width, int height) {
  bool isPositive = percent >= 0;
  uint16_t bgColor = tft.color565(30, 30, 30);
  uint16_t borderColor = isPositive ? TFT_RED : TFT_GREEN;
  uint16_t textColor = TFT_WHITE;
  uint16_t valueColor = TFT_YELLOW;
  uint16_t chgColor = isPositive ? TFT_RED : TFT_GREEN;

  // 背景方块
  tft.fillRoundRect(x, y, width, height, 4, bgColor);
  tft.drawRoundRect(x, y, width, height, 4, borderColor);

  int padding = 4;
  int lineY = y + padding;

  // 指数名称（可能为中文）
  displayChineseString(name.c_str(), x + padding, lineY, textColor, NO_BACKGROUND, 1, 240);

  lineY += 24;
  // 当前点数（大字）
  tft.setTextFont(1);
  tft.setTextSize(2);
  tft.setTextColor(valueColor);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_YELLOW);
  tft.drawString(String(value, 2), x + padding , lineY);
  
  // 涨跌百分比
  lineY -= 22;
  String fullStr = String(percent, 2) + "%";
  tft.setTextSize(1);
  tft.setTextColor(chgColor);
  tft.setTextDatum(TR_DATUM);
  tft.drawString(fullStr, x + width - 10 - padding, lineY);

  // 箭头图标（右上角）
  int markerX = x + width - 10;
  int markerY = y + 10;
  if (isPositive) {
    tft.fillTriangle(markerX, markerY - 4,
                     markerX - 4, markerY + 4,
                     markerX + 4, markerY + 4,
                     chgColor);
  } else {
    tft.fillTriangle(markerX, markerY + 4,
                     markerX - 4, markerY - 4,
                     markerX + 4, markerY - 4,
                     chgColor);
  }
}
/**
 * 计算字符串的像素长度。
 * UTF-8检测规则：
 * - 检查是否是中文（以0xE0开头），中文默认16像素
 * - 否则为英文或其他，默认12像素
 *
 * @param content  输入字符串
 * @param textSize 字体缩放倍数
 * @return 像素长度
 */
int getTextPixelWidth(const String &content, int textSize = 1) {
  int textPixelWidth = 0;

  for (int i = 0; i < content.length();) {
    if ((content[i] & 0xF0) == 0xE0) {
      // 检测为中文3字节
      textPixelWidth += 16 * textSize;
      i += 3;
    } else {
      // 非中文，默认英文/数字为1字节
      textPixelWidth += 12 * textSize;
      i += 1;
    }
  }

  return textPixelWidth;
}

// 应用显示模式效果
void applyDisplayMode(DisplayMode mode, uint16_t &primary, uint16_t &secondary, uint16_t &text, uint16_t &accent) {
  switch(mode) {
    case MODE_GLOSSY:
      primary = blendColors(primary, TFT_WHITE, 20);
      secondary = blendColors(secondary, TFT_BLACK, 15);
      break;
    case MODE_NEON:
      primary = blendColors(primary, TFT_BLACK, 50);
      secondary = accent;
      text = blendColors(text, TFT_WHITE, 80);
      break;
    case MODE_MINIMAL:
      secondary = primary;
      primary = TFT_BLACK;
      text = blendColors(text, TFT_WHITE, 70);
      break;
    case MODE_NORMAL:
    default:
      break;
  }
}

// 混合两种颜色
uint16_t blendColors(uint16_t c1, uint16_t c2, uint8_t ratio) {
  uint8_t r1 = (c1 >> 11) & 0x1F, g1 = (c1 >> 5) & 0x3F, b1 = c1 & 0x1F;
  uint8_t r2 = (c2 >> 11) & 0x1F, g2 = (c2 >> 5) & 0x3F, b2 = c2 & 0x1F;
  uint8_t r = (r1 * (255 - ratio) + r2 * ratio) / 255;
  uint8_t g = (g1 * (255 - ratio) + g2 * ratio) / 255;
  uint8_t b = (b1 * (255 - ratio) + b2 * ratio) / 255;
  return (r << 11) | (g << 5) | b;
}