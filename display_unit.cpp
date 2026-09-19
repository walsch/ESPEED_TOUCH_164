#include "display_unit.h"

#include <Arduino_GFX_Library.h>
#include <canvas/Arduino_Canvas.h>
#include <Wire.h>
#include <string.h>

namespace {
constexpr int16_t LCD_WIDTH = 280;
constexpr int16_t LCD_HEIGHT = 456;
constexpr int8_t LCD_CS = 9;
constexpr int8_t LCD_SCK = 10;
constexpr int8_t LCD_D0 = 11;
constexpr int8_t LCD_D1 = 12;
constexpr int8_t LCD_D2 = 13;
constexpr int8_t LCD_D3 = 14;
constexpr int8_t LCD_RST = 21;
constexpr uint8_t LCD_COLUMN_OFFSET = 20;
constexpr uint8_t TOUCH_ADDRESS = 0x38;
constexpr int8_t TOUCH_SDA = 47;
constexpr int8_t TOUCH_SCL = 48;

Arduino_DataBus *lcdBus = new Arduino_ESP32QSPI(
    LCD_CS, LCD_SCK, LCD_D0, LCD_D1, LCD_D2, LCD_D3);
Arduino_GFX *display = new Arduino_SH8601(
    lcdBus, LCD_RST, 0, LCD_WIDTH, LCD_HEIGHT, LCD_COLUMN_OFFSET);
Arduino_Canvas *screen = new Arduino_Canvas(
    LCD_WIDTH, LCD_HEIGHT, display, 0, 0, 1);

enum class MenuPage : uint8_t {
  Status,
  Settings,
  Cars,
  Edit,
  CarEdit
};

struct SettingItem {
  const char *name;
  int16_t value;
  int16_t minimum;
  int16_t maximum;
  char unit;
};

MenuPage currentPage = MenuPage::Status;
uint8_t selectedSetting = 0;
uint8_t selectedCar = 0;
int16_t editOriginalValue = 0;
uint8_t carEditOriginal = 0;
bool carMenuReturnToSettings = false;
bool carMenuReturnToStatus = false;
int16_t curveXValue = 50;
int16_t curveOriginalX = 50;
int16_t curveOriginalY = 50;
bool touchWasDown = false;
bool welcomeVisible = false;
bool touchMoved = false;
bool touchSliderActive = false;
bool settingsDragging = false;
int16_t touchStartX = 0;
int16_t touchStartY = 0;
int16_t touchLastX = 0;
int16_t touchLastY = 0;
float sliderRemainder = 0.0f;
int8_t sliderDirection = 0;
int16_t settingsScroll = 0;
int16_t settingsScrollStart = 0;
int16_t settingsTouchStartScroll = 0;

constexpr int16_t SETTINGS_BUTTON_WIDTH = 126;
constexpr int16_t SETTINGS_BUTTON_HEIGHT = 82;
constexpr int16_t SETTINGS_COLUMN_STEP = 138;
constexpr int16_t SETTINGS_MAX_SCROLL = 388;

// RGB565 high-contrast palette
constexpr uint16_t COLOR_BG = 0x0000;
constexpr uint16_t COLOR_PANEL = 0x2124;
constexpr uint16_t COLOR_PANEL_ALT = 0x31A6;
constexpr uint16_t COLOR_BORDER = 0x7BEF;
constexpr uint16_t COLOR_ACCENT = 0x07FF;
constexpr uint16_t COLOR_ACCENT_WARM = 0xFD20;
constexpr uint16_t COLOR_TEXT = 0xFFFF;
constexpr uint16_t COLOR_LABEL = 0xFFE0;
constexpr uint16_t COLOR_TRACK = 0x8410;
constexpr uint16_t COLOR_DANGER = 0xF800;

SettingItem settings[] = {
    {"BRAKE", 60, 0, 100, '%'},
    {"SENSI", 0, 0, 90, '%'},
    {"EXPO", 0, -100, 100, ' '},
    {"ANTIS", 0, 0, 250, 'm'},
    {"DRAGB", 0, 0, 100, '%'},
    {"WIRE", 0, 0, 250, 'f'},
    {"WLIM", 99, 0, 100, '%'},
    {"CURVE", 50, 10, 90, '%'},
    {"PWM_F", 15, 10, 250, 'k'},
    {"LIMIT", 100, 5, 100, '%'},
    {"BYPAS", 0, 0, 1, ' '},
};

const char *carNames[] = {
    "CAR 01", "CAR 02", "CAR 03", "CAR 04", "CAR 05",
    "CAR 06", "CAR 07", "CAR 08", "CAR 09", "CAR 10",
    "CAR 11", "CAR 12", "CAR 13", "CAR 14", "CAR 15",
    "CAR 16", "CAR 17", "CAR 18", "CAR 19", "CAR 20"};
constexpr uint8_t CAR_COUNT = sizeof(carNames) / sizeof(carNames[0]);
constexpr int16_t CARS_MAX_SCROLL = ((CAR_COUNT + 1) / 2 - 3) * SETTINGS_COLUMN_STEP;
int16_t carsScroll = 0;
int16_t carsTouchStartScroll = 0;
bool carsDragging = false;

void configurePanel() {
  lcdBus->writeCommand(0x11);
  delay(80);
  lcdBus->beginWrite();
  lcdBus->writeC8D8(0xC4, 0x80);
  lcdBus->writeC8D8(0x35, 0x00);
  lcdBus->writeC8D8(0x53, 0x20);
  lcdBus->writeC8D8(0x63, 0xFF);
  lcdBus->endWrite();
  lcdBus->writeCommand(0x29);
  lcdBus->beginWrite();
  lcdBus->writeC8D8(0x51, 0xFF);
  lcdBus->endWrite();
  delay(10);
}

void drawWelcomeScreen() {
  const uint16_t background = COLOR_BG;
  const uint16_t panel = COLOR_PANEL;
  const uint16_t accent = COLOR_ACCENT;
  const uint16_t accentWarm = COLOR_ACCENT_WARM;
  const uint16_t text = COLOR_TEXT;
  const uint16_t muted = COLOR_LABEL;

  screen->fillScreen(background);
  screen->fillRoundRect(20, 18, 416, 244, 18, panel);
  screen->drawRoundRect(20, 18, 416, 244, 18, accent);

  screen->fillCircle(82, 112, 42, accent);
  screen->fillCircle(82, 112, 30, background);
  screen->fillRect(72, 92, 20, 40, accent);
  screen->fillRect(62, 102, 40, 20, accent);

  screen->setTextColor(text);
  screen->setTextSize(2);
  screen->setCursor(150, 68);
  screen->print("WELCOME");

  screen->setTextColor(muted);
  screen->setTextSize(1);
  screen->setCursor(150, 108);
  screen->print("READY TO RACE?");
  screen->setCursor(150, 126);
  screen->print("YOUR NEXT RUN STARTS HERE");

  screen->drawFastHLine(150, 158, 220, accentWarm);
  screen->fillCircle(150, 158, 3, accentWarm);
  screen->fillCircle(370, 158, 3, accentWarm);

  screen->drawRoundRect(150, 188, 164, 52, 12, accentWarm);
  screen->setTextColor(text);
  screen->setTextSize(2);
  screen->setCursor(198, 206);
  screen->print("START");
}

void drawHeader(const char *title) {
  screen->fillScreen(COLOR_BG);
  screen->setTextColor(COLOR_TEXT);
  screen->setTextSize(3);
  screen->setCursor(20, 11);
  screen->print(title);
  screen->drawFastHLine(20, 46, 416, COLOR_ACCENT);
}

void drawStatusPage() {
  drawHeader("ESPEED32");
  const int16_t cardWidth = 196;
  const int16_t cardHeight = 64;
  const int16_t leftX = 20;
  const int16_t rightX = 240;
  const int16_t topY = 58;
  const int16_t bottomY = 130;

  screen->fillRoundRect(leftX, topY, cardWidth, cardHeight, 10, COLOR_PANEL);
  screen->drawRoundRect(leftX, topY, cardWidth, cardHeight, 10, COLOR_BORDER);
  screen->fillRoundRect(rightX, topY, cardWidth, cardHeight, 10, COLOR_PANEL);
  screen->drawRoundRect(rightX, topY, cardWidth, cardHeight, 10, COLOR_BORDER);
  screen->fillRoundRect(leftX, bottomY, cardWidth, cardHeight, 10, COLOR_PANEL);
  screen->drawRoundRect(leftX, bottomY, cardWidth, cardHeight, 10, COLOR_BORDER);
  screen->fillRoundRect(rightX, bottomY, cardWidth, cardHeight, 10, COLOR_PANEL);
  screen->drawRoundRect(rightX, bottomY, cardWidth, cardHeight, 10, COLOR_BORDER);

  screen->setTextColor(COLOR_LABEL);
  screen->setTextSize(2);
  screen->setCursor(leftX + 14, topY + 10);
  screen->print("SENS");
  screen->setCursor(rightX + 14, topY + 10);
  screen->print("BRAKE");
  screen->setCursor(leftX + 14, bottomY + 10);
  screen->print("EXPO");
  screen->setCursor(rightX + 14, bottomY + 10);
  screen->print("CAR");

  screen->setTextColor(COLOR_TEXT);
  screen->setTextSize(3);
  screen->setCursor(leftX + 14, topY + 32);
  screen->print(settings[1].value);
  screen->print("%");
  screen->setCursor(rightX + 14, topY + 32);
  screen->print(settings[0].value);
  screen->print("%");
  screen->setCursor(leftX + 14, bottomY + 32);
  screen->print(settings[2].value);
  screen->setCursor(rightX + 14, bottomY + 32);
  screen->print(carNames[selectedCar]);

  screen->fillRoundRect(20, 208, 416, 64, 10, COLOR_PANEL_ALT);
  screen->drawRoundRect(20, 208, 416, 64, 10, COLOR_ACCENT);
  screen->setTextColor(COLOR_LABEL);
  screen->setTextSize(2);
  screen->setCursor(38, 218);
  screen->print("TRIGGER");
  screen->setCursor(300, 218);
  screen->print("BAHN V");
  screen->setTextColor(COLOR_TEXT);
  screen->setTextSize(3);
  screen->setCursor(38, 240);
  screen->print("0%");
  screen->setCursor(300, 240);
  screen->print("--.- V");
}

void printSettingValue(const SettingItem &item, int16_t x, int16_t y) {
  screen->setTextColor(COLOR_TEXT);
  screen->setTextSize(3);
  screen->setCursor(x, y);
  if (item.name[0] == 'B' && item.name[1] == 'Y') {
    screen->print(item.value ? "ON" : "OFF");
  } else {
    screen->print(item.value);
    if (item.unit != ' ') {
      screen->print(item.unit);
    }
  }
}

float originalExpoCurve(float inputPercent) {
  const float minSpeed = settings[1].value;
  const float maxSpeed = settings[9].value;
  const float vertexInput = curveXValue;
  const float vertexSpeed = minSpeed + (maxSpeed - minSpeed) * settings[7].value / 100.0f;
  float speed;

  if (inputPercent <= 0.0f) {
    speed = 0.0f;
  } else if (inputPercent <= vertexInput) {
    speed = minSpeed + (vertexSpeed - minSpeed) * inputPercent / vertexInput;
  } else {
    speed = vertexSpeed + (maxSpeed - vertexSpeed) * (inputPercent - vertexInput) / (100.0f - vertexInput);
  }

  if (speed >= maxSpeed || maxSpeed <= 0.0f) {
    return speed;
  }

  const float legacyExpo = settings[selectedSetting].value + 100.0f;
  const float exponent = legacyExpo / 100.0f;
  return maxSpeed / powf(maxSpeed, exponent) * powf(speed, exponent);
}

void drawSettingsPage() {
  drawHeader("SETTINGS");
  screen->fillRoundRect(322, 6, 120, 34, 8, COLOR_PANEL);
  screen->drawRoundRect(322, 6, 120, 34, 8, COLOR_ACCENT_WARM);
  screen->setTextColor(COLOR_TEXT);
  screen->setTextSize(2);
  screen->setCursor(338, 15);
  screen->print(carNames[selectedCar]);

  for (uint8_t index = 0; index < sizeof(settings) / sizeof(settings[0]); index++) {
    const uint8_t column = index / 2;
    const uint8_t row = index % 2;
    const int16_t x = 14 + column * SETTINGS_COLUMN_STEP - settingsScroll;
    const int16_t y = 56 + row * 92;
    if (x < 0 || x + SETTINGS_BUTTON_WIDTH > 456) {
      continue;
    }
    screen->fillRoundRect(x, y, SETTINGS_BUTTON_WIDTH, SETTINGS_BUTTON_HEIGHT, 12, COLOR_PANEL);
    screen->drawRoundRect(x, y, SETTINGS_BUTTON_WIDTH, SETTINGS_BUTTON_HEIGHT, 12, COLOR_BORDER);
    screen->setTextColor(COLOR_LABEL);
    screen->setTextSize(2);
    screen->setCursor(x + 12, y + 14);
    screen->print(settings[index].name);
    printSettingValue(settings[index], x + 12, y + 42);
  }

  screen->fillRoundRect(14, 240, 428, 34, 8, COLOR_PANEL);
  screen->drawRoundRect(14, 240, 428, 34, 8, COLOR_ACCENT);
  screen->setTextColor(COLOR_TEXT);
  screen->setTextSize(2);
  screen->setCursor(60, 249);
  screen->print("BACK");
  screen->setCursor(190, 249);
  screen->print("SWIPE LEFT / RIGHT");
}

void drawEditPage() {
  const SettingItem &item = settings[selectedSetting];
  drawHeader(item.name);

  screen->setTextColor(COLOR_LABEL);
  screen->setTextSize(1);
  screen->setCursor(330, 22);
  screen->print("EDIT VALUE");

  screen->setTextColor(COLOR_TEXT);
  screen->setTextSize(4);
  screen->setCursor(24, 62);
  screen->print(item.value);
  if (item.unit != ' ') {
    screen->setTextSize(2);
    screen->print(item.unit);
  }

  if (strcmp(item.name, "CURVE") == 0) {
    const int16_t graphX = 34;
    const int16_t graphY = 52;
    const int16_t graphWidth = 386;
    const int16_t graphHeight = 168;
    const int16_t graphBottom = graphY + graphHeight;
    const int16_t minSpeed = settings[1].value;
    const int16_t maxSpeed = settings[9].value;
    const int16_t vertexSpeed = minSpeed + ((maxSpeed - minSpeed) * settings[7].value) / 100;
    const int16_t vertexX = graphX + (curveXValue * graphWidth) / 100;
    const int16_t vertexY = graphBottom - (vertexSpeed * graphHeight) / 100;

    screen->drawFastHLine(graphX, graphBottom, graphWidth, COLOR_TEXT);
    screen->drawFastVLine(graphX, graphY, graphHeight, COLOR_TEXT);
    screen->setTextColor(COLOR_LABEL);
    screen->setTextSize(1);
    screen->setCursor(2, graphY - 2);
    screen->print("100%");
    screen->setCursor(10, graphBottom - 8);
    screen->print("0%");
    screen->setCursor(graphX, graphBottom + 2);
    screen->print("0%");
    screen->setCursor(graphX + graphWidth - 24, graphBottom + 2);
    screen->print("100%");
    screen->drawLine(graphX, graphBottom - (minSpeed * graphHeight) / 100, vertexX, vertexY, COLOR_ACCENT_WARM);
    screen->drawLine(vertexX, vertexY, graphX + graphWidth, graphBottom - (maxSpeed * graphHeight) / 100, COLOR_ACCENT_WARM);
    screen->fillCircle(vertexX, vertexY, 7, COLOR_TEXT);
    screen->setTextColor(COLOR_LABEL);
    screen->setCursor(42, 58);
    screen->print("CURVE X/Y");
    screen->setCursor(42, 216);
    screen->print("X: ");
    screen->print(curveXValue);
    screen->print("%   Y: ");
    screen->print(settings[7].value);
    screen->print("%");
  } else if (strcmp(item.name, "EXPO") == 0) {
    const int16_t graphX = 34;
    const int16_t graphY = 52;
    const int16_t graphWidth = 386;
    const int16_t graphHeight = 168;

    const int16_t graphBottom = graphY + graphHeight;
    screen->drawFastHLine(graphX, graphBottom, graphWidth, COLOR_TEXT);
    screen->drawFastVLine(graphX, graphY, graphHeight, COLOR_TEXT);
    screen->setTextColor(COLOR_LABEL);
    screen->setTextSize(1);
    screen->setCursor(2, graphY - 2);
    screen->print("100%");
    screen->setCursor(10, graphBottom - 8);
    screen->print("0%");
    screen->setCursor(graphX, graphBottom + 2);
    screen->print("0%");
    screen->setCursor(graphX + graphWidth - 24, graphBottom + 2);
    screen->print("100%");
    screen->setTextColor(COLOR_LABEL);
    screen->setTextSize(1);
    screen->setCursor(310, 58);
    screen->print("EXPO CURVE");
    int16_t previousX = graphX;
    int16_t previousY = graphY + graphHeight;
    for (int16_t point = 1; point <= 100; point++) {
      const float input = (float)point / 100.0f;
      const float output = originalExpoCurve(input) / 100.0f;
      const int16_t currentX = graphX + (point * (graphWidth - 1)) / 100;
      const int16_t currentY = graphBottom - (int16_t)(output * graphHeight);
      screen->drawLine(previousX, previousY, currentX, currentY, COLOR_ACCENT_WARM);
      previousX = currentX;
      previousY = currentY;
    }
  } else {

  screen->fillRoundRect(20, 104, 416, 52, 10, COLOR_PANEL);
  screen->drawRoundRect(20, 104, 416, 52, 10, COLOR_ACCENT);
  screen->setTextColor(COLOR_LABEL);
  screen->setTextSize(1);
  screen->setCursor(36, 114);
  screen->print("RELATIVE SWIPE");
  screen->drawFastHLine(36, 140, 384, COLOR_TRACK);
  screen->fillRoundRect(36, 137, 190, 7, 3, COLOR_ACCENT);
  screen->setCursor(36, 147);
  screen->print("LEFT / RIGHT TO ADJUST");

  screen->fillRoundRect(20, 166, 416, 52, 10, COLOR_PANEL);
  screen->drawRoundRect(20, 166, 416, 52, 10, COLOR_ACCENT_WARM);
  screen->setCursor(36, 176);
  screen->print("ABSOLUTE SLIDER");
  screen->drawFastHLine(36, 201, 384, COLOR_TRACK);
  const int16_t knobX = map(item.value, item.minimum, item.maximum, 36, 420);
  screen->fillRoundRect(36, 198, knobX - 36, 7, 3, COLOR_ACCENT_WARM);
  screen->fillCircle(knobX, 201, 8, COLOR_TEXT);
  }

  screen->fillRoundRect(20, 226, 198, 46, 10, COLOR_PANEL);
  screen->drawRoundRect(20, 226, 198, 46, 10, COLOR_DANGER);
  screen->fillRoundRect(238, 226, 198, 46, 10, COLOR_PANEL);
  screen->drawRoundRect(238, 226, 198, 46, 10, COLOR_ACCENT);
  screen->setTextColor(COLOR_TEXT);
  screen->setTextSize(2);
  screen->setCursor(76, 241);
  screen->print("CANCEL");
  screen->setCursor(302, 241);
  screen->print("SAVE");
}

void drawCarsPage() {
  drawHeader("SELECT CAR");
  for (uint8_t i = 0; i < CAR_COUNT; i++) {
    const int16_t x = 14 + (i / 2) * SETTINGS_COLUMN_STEP - carsScroll;
    const int16_t y = 56 + (i % 2) * 92;
    if (x < 0 || x + SETTINGS_BUTTON_WIDTH > 456) {
      continue;
    }
    screen->fillRoundRect(x, y, SETTINGS_BUTTON_WIDTH, SETTINGS_BUTTON_HEIGHT, 12, COLOR_PANEL);
    screen->drawRoundRect(x, y, SETTINGS_BUTTON_WIDTH, SETTINGS_BUTTON_HEIGHT, 12,
                          i == selectedCar ? COLOR_ACCENT_WARM : COLOR_BORDER);
    screen->setTextColor(COLOR_TEXT);
    screen->setTextSize(3);
    screen->setCursor(x + 9, y + 29);
    screen->print(carNames[i]);
  }
  screen->fillRoundRect(14, 240, 428, 34, 8, COLOR_PANEL);
  screen->drawRoundRect(14, 240, 428, 34, 8, COLOR_ACCENT);
  screen->setTextColor(COLOR_TEXT);
  screen->setTextSize(2);
  screen->setCursor(60, 249);
  screen->print("BACK");
  screen->setCursor(190, 249);
  screen->print("SWIPE LEFT / RIGHT");
}

void drawCarEditPage() {
  drawHeader("CAR SETTINGS");
  screen->setTextColor(COLOR_TEXT);
  screen->setTextSize(3);
  screen->setCursor(26, 72);
  screen->print(carNames[selectedCar]);
  screen->setTextColor(COLOR_LABEL);
  screen->setTextSize(1);
  screen->setCursor(28, 116);
  screen->print("ACTIVE CAR PROFILE");
  screen->setCursor(28, 136);
  screen->print("Use this car profile for the next run.");

  screen->fillRoundRect(20, 166, 416, 48, 10, COLOR_PANEL);
  screen->drawRoundRect(20, 166, 416, 48, 10, COLOR_BORDER);
  screen->setTextColor(COLOR_LABEL);
  screen->setTextSize(2);
  screen->setCursor(132, 182);
  screen->print("CAR PROFILE READY");

  screen->fillRoundRect(20, 226, 198, 46, 10, COLOR_PANEL);
  screen->drawRoundRect(20, 226, 198, 46, 10, COLOR_DANGER);
  screen->fillRoundRect(238, 226, 198, 46, 10, COLOR_PANEL);
  screen->drawRoundRect(238, 226, 198, 46, 10, COLOR_ACCENT);
  screen->setCursor(76, 241);
  screen->print("CANCEL");
  screen->setCursor(302, 241);
  screen->print("SAVE");
}

void drawCurrentPage() {
  if (currentPage == MenuPage::Status) {
    drawStatusPage();
  } else if (currentPage == MenuPage::Settings) {
    drawSettingsPage();
  } else if (currentPage == MenuPage::Cars) {
    drawCarsPage();
  } else if (currentPage == MenuPage::Edit) {
    drawEditPage();
  } else {
    drawCarEditPage();
  }
  screen->flush();
}

bool readTouch(int16_t &x, int16_t &y) {
  Wire.beginTransmission(TOUCH_ADDRESS);
  Wire.write(0x02);
  if (Wire.endTransmission(false) != 0 || Wire.requestFrom(TOUCH_ADDRESS, (uint8_t)5) != 5) {
    return false;
  }

  const uint8_t points = Wire.read();
  const uint8_t xHigh = Wire.read();
  const uint8_t xLow = Wire.read();
  const uint8_t yHigh = Wire.read();
  const uint8_t yLow = Wire.read();
  if (!points) {
    return false;
  }

  const int16_t physicalX = ((xHigh & 0x0F) << 8) | xLow;
  const int16_t physicalY = ((yHigh & 0x0F) << 8) | yLow;
  x = constrain(physicalY, 0, LCD_HEIGHT - 1);
  y = constrain(LCD_WIDTH - 1 - physicalX, 0, LCD_WIDTH - 1);
  return true;
}

void updateValueFromAbsoluteSlider(int16_t x);

void handleTouch(int16_t x, int16_t y) {
  if (welcomeVisible) {
    if (x >= 140 && x <= 330 && y >= 180 && y <= 260) {
      welcomeVisible = false;
      drawCurrentPage();
    }
    return;
  }

  if (currentPage == MenuPage::Status) {
    if (y >= 190 && y <= 270 && x < 228) {
      currentPage = MenuPage::Settings;
    } else if (y >= 190 && y <= 270) {
      carMenuReturnToStatus = true;
      carMenuReturnToSettings = false;
      currentPage = MenuPage::Cars;
    }
    drawCurrentPage();
    return;
  }

  if (currentPage == MenuPage::Settings) {
    if (x >= 322 && x < 442 && y < 50) {
      carMenuReturnToSettings = true;
      carMenuReturnToStatus = false;
      currentPage = MenuPage::Cars;
    } else if (y >= 55 && y < 238) {
      for (uint8_t index = 0; index < sizeof(settings) / sizeof(settings[0]); index++) {
        const int16_t cardX = 14 + (index / 2) * SETTINGS_COLUMN_STEP - settingsScroll;
        const int16_t cardY = 56 + (index % 2) * 92;
        if (x >= cardX && x < cardX + SETTINGS_BUTTON_WIDTH &&
            y >= cardY && y < cardY + SETTINGS_BUTTON_HEIGHT) {
          selectedSetting = index;
          editOriginalValue = settings[index].value;
          if (strcmp(settings[index].name, "CURVE") == 0) {
            curveOriginalX = curveXValue;
            curveOriginalY = settings[index].value;
          }
          currentPage = MenuPage::Edit;
          break;
        }
      }
    } else if (y >= 238) {
      currentPage = MenuPage::Status;
    }
    drawCurrentPage();
    return;
  }

  if (currentPage == MenuPage::Edit) {
    if (strcmp(settings[selectedSetting].name, "CURVE") == 0 && y >= 220) {
      if (x < 228) {
        curveXValue = curveOriginalX;
        settings[selectedSetting].value = curveOriginalY;
      }
      currentPage = MenuPage::Settings;
      drawCurrentPage();
      return;
    }
    if (y >= 220) {
      if (x < 228) {
        settings[selectedSetting].value = editOriginalValue;
      }
      currentPage = MenuPage::Settings;
      drawCurrentPage();
    } else if (y >= 160) {
      updateValueFromAbsoluteSlider(x);
    }
    return;
  }

  if (currentPage == MenuPage::Cars) {
    const MenuPage returnPage = carMenuReturnToSettings ? MenuPage::Settings : MenuPage::Status;
    if (y >= 238) {
      currentPage = returnPage;
    } else if (y >= 56 && y < 238) {
      for (uint8_t index = 0; index < CAR_COUNT; index++) {
        const int16_t cardX = 14 + (index / 2) * SETTINGS_COLUMN_STEP - carsScroll;
        const int16_t cardY = 56 + (index % 2) * 92;
        if (x >= cardX && x < cardX + SETTINGS_BUTTON_WIDTH &&
            y >= cardY && y < cardY + SETTINGS_BUTTON_HEIGHT) {
          selectedCar = index;
          currentPage = returnPage;
          break;
        }
      }
    }
    drawCurrentPage();
    return;
  }

}

void handleSwipe(int16_t startX, int16_t startY, int16_t endX, int16_t endY) {
  const int16_t deltaX = endX - startX;
  const int16_t deltaY = endY - startY;
  const int16_t threshold = 35;

  if (currentPage == MenuPage::Settings) {
    if (abs(deltaY) >= abs(deltaX) && abs(deltaY) >= threshold) {
      if (deltaY < 0) {
        if (selectedSetting > 0) {
          selectedSetting--;
        }
      } else {
        if (selectedSetting + 1 < sizeof(settings) / sizeof(settings[0])) {
          selectedSetting++;
        }
      }
      drawCurrentPage();
    } else if (abs(deltaX) >= threshold) {
      if (deltaX < 0) {
        settings[selectedSetting].value = max(settings[selectedSetting].minimum, (int16_t)(settings[selectedSetting].value - 1));
      } else {
        settings[selectedSetting].value = min(settings[selectedSetting].maximum, (int16_t)(settings[selectedSetting].value + 1));
      }
      drawCurrentPage();
    }
  } else if (currentPage == MenuPage::Cars && abs(deltaY) >= threshold) {
    if (deltaY < 0) {
      if (selectedCar > 0) {
        selectedCar--;
      }
    } else {
      if (selectedCar + 1 < CAR_COUNT) {
        selectedCar++;
      }
    }
    drawCurrentPage();
  }
}

void updateValueFromSlider(int16_t deltaX) {
  const float sliderTravel = 416.0f;
  SettingItem &item = settings[selectedSetting];
  const int16_t valueRange = item.maximum - item.minimum;
  const int8_t direction = deltaX > 0 ? 1 : -1;
  if (sliderDirection != 0 && direction != sliderDirection) {
    sliderRemainder = 0.0f;
  }
  sliderDirection = direction;
  sliderRemainder += ((float)deltaX * valueRange) / sliderTravel;
  int16_t valueDelta = (int16_t)sliderRemainder;
  if (valueDelta > 1) {
    valueDelta = 1;
  } else if (valueDelta < -1) {
    valueDelta = -1;
  }
  if (valueDelta != 0) {
    item.value = constrain(item.value + valueDelta, item.minimum, item.maximum);
    sliderRemainder -= valueDelta;
    drawCurrentPage();
  }
}

void updateStatusValueFromGesture(int16_t deltaX, int16_t touchY) {
  const uint8_t settingIndex = touchY < 140 ? 0 : 1;
  const float sliderTravel = 416.0f;
  SettingItem &item = settings[settingIndex];
  const int16_t valueRange = item.maximum - item.minimum;
  const int8_t direction = deltaX > 0 ? 1 : -1;
  if (sliderDirection != 0 && direction != sliderDirection) {
    sliderRemainder = 0.0f;
  }
  sliderDirection = direction;
  sliderRemainder += ((float)deltaX * valueRange) / sliderTravel;
  int16_t valueDelta = (int16_t)sliderRemainder;
  if (valueDelta > 1) {
    valueDelta = 1;
  } else if (valueDelta < -1) {
    valueDelta = -1;
  }
  if (valueDelta != 0) {
    item.value = constrain(item.value + valueDelta, item.minimum, item.maximum);
    sliderRemainder -= valueDelta;
    drawCurrentPage();
  }
}

void updateCurveFromGesture(int16_t deltaX, int16_t deltaY) {
  const int16_t xStep = deltaX / 8;
  const int16_t yStep = -deltaY / 8;
  if (xStep != 0) {
    curveXValue = constrain(curveXValue + xStep, (int16_t)20, (int16_t)80);
  }
  if (yStep != 0) {
    settings[7].value = constrain(settings[7].value + yStep, (int16_t)10, (int16_t)90);
  }
  if (xStep != 0 || yStep != 0) {
    drawCurrentPage();
  }
}

void updateValueFromAbsoluteSlider(int16_t x) {
  SettingItem &item = settings[selectedSetting];
  const int16_t sliderStart = 36;
  const int16_t sliderEnd = 420;
  const int16_t value = map(constrain(x, sliderStart, sliderEnd), sliderStart, sliderEnd, item.minimum, item.maximum);
  if (value != item.value) {
    item.value = value;
    drawCurrentPage();
  }
}

void updateSettingsScroll(int16_t deltaX) {
  settingsScroll = constrain(settingsTouchStartScroll - deltaX, (int16_t)0, SETTINGS_MAX_SCROLL);
  drawCurrentPage();
}

void updateCarsScroll(int16_t deltaX) {
  carsScroll = constrain(carsTouchStartScroll - deltaX, (int16_t)0, CARS_MAX_SCROLL);
  drawCurrentPage();
}
}

void displayInit() {
  Wire.begin(TOUCH_SDA, TOUCH_SCL, 300000L);
  screen->begin();
  configurePanel();
  drawCurrentPage();
  screen->flush();
}

void displayUpdate() {
  int16_t x;
  int16_t y;
  const bool touchDown = readTouch(x, y);
  if (touchDown && !touchWasDown) {
    touchStartX = x;
    touchStartY = y;
    touchLastX = x;
    touchLastY = y;
    touchMoved = false;
    touchSliderActive = false;
    settingsDragging = false;
    carsDragging = false;
    sliderRemainder = 0.0f;
    sliderDirection = 0;
    settingsTouchStartScroll = settingsScroll;
    carsTouchStartScroll = carsScroll;
  } else if (touchDown && touchWasDown) {
    touchLastX = x;
    touchLastY = y;
    if (abs(touchLastX - touchStartX) >= 5 || abs(touchLastY - touchStartY) >= 5) {
      touchMoved = true;
    }
    if (!welcomeVisible && currentPage == MenuPage::Status &&
        abs(touchLastX - touchStartX) >= abs(touchLastY - touchStartY) &&
        abs(touchLastX - touchStartX) >= 5) {
      updateStatusValueFromGesture(touchLastX - touchStartX, touchStartY);
      touchStartX = touchLastX;
      touchStartY = touchLastY;
      touchSliderActive = true;
    } else if (!welcomeVisible && currentPage == MenuPage::Settings &&
        abs(touchLastX - touchStartX) >= abs(touchLastY - touchStartY) &&
        abs(touchLastX - touchStartX) >= 5) {
      updateSettingsScroll(touchLastX - touchStartX);
      settingsDragging = true;
      touchSliderActive = true;
    } else if (!welcomeVisible && currentPage == MenuPage::Cars &&
        abs(touchLastX - touchStartX) >= abs(touchLastY - touchStartY) &&
        abs(touchLastX - touchStartX) >= 5) {
      updateCarsScroll(touchLastX - touchStartX);
      carsDragging = true;
      touchSliderActive = true;
    } else if (!welcomeVisible && currentPage == MenuPage::Edit &&
        strcmp(settings[selectedSetting].name, "CURVE") == 0 && touchMoved) {
      updateCurveFromGesture(touchLastX - touchStartX, touchLastY - touchStartY);
      touchStartX = touchLastX;
      touchStartY = touchLastY;
      touchSliderActive = true;
    } else if (!welcomeVisible && currentPage == MenuPage::Edit &&
        abs(touchLastX - touchStartX) >= abs(touchLastY - touchStartY) &&
        abs(touchLastX - touchStartX) >= 5) {
      if (touchStartY < 160) {
        updateValueFromSlider(touchLastX - touchStartX);
      } else {
        updateValueFromAbsoluteSlider(touchLastX);
      }
      touchStartX = touchLastX;
      touchStartY = touchLastY;
      touchSliderActive = true;
    }
  } else if (!touchDown && touchWasDown) {
    if (settingsDragging) {
      settingsScroll = constrain((int16_t)((settingsScroll + SETTINGS_COLUMN_STEP / 2) / SETTINGS_COLUMN_STEP) * SETTINGS_COLUMN_STEP,
                                 (int16_t)0, SETTINGS_MAX_SCROLL);
      drawCurrentPage();
    } else if (carsDragging) {
      carsScroll = constrain((int16_t)((carsScroll + SETTINGS_COLUMN_STEP / 2) / SETTINGS_COLUMN_STEP) * SETTINGS_COLUMN_STEP,
                             (int16_t)0, CARS_MAX_SCROLL);
      drawCurrentPage();
    } else if (touchMoved && !touchSliderActive) {
      handleSwipe(touchStartX, touchStartY, touchLastX, touchLastY);
    } else if (!touchMoved) {
      handleTouch(touchLastX, touchLastY);
    }
  }
  touchWasDown = touchDown;
}