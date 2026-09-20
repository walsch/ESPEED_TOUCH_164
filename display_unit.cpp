#include "display_unit.h"

#include <Arduino_GFX_Library.h>
#include <canvas/Arduino_Canvas.h>
#include <Wire.h>
#include <Preferences.h>
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
// BAT_ADC: VCC through 200K/100K divider -> Vcc = 3 * Vadc (≈4.9 V on USB, 3.3..4.2 V on battery)
constexpr uint8_t SUPPLY_ADC_PIN = 4;
constexpr float SUPPLY_DIVIDER = 3.0f;
constexpr uint32_t SUPPLY_SAMPLE_MS = 500;
float supplyVoltage = 0.0f;
uint32_t supplySampledAt = 0;

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
  CarEdit,
  CarOptions,
  CarCopy,
  CarCopyConfirm,
  CarDefaultsConfirm,
  Config,
  AssignSlot,
  Help,
  AllDefaultsConfirm
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
uint8_t carEditCursor = 0;
constexpr uint8_t CAR_NAME_LENGTH = 8;
char carEditOriginalName[CAR_NAME_LENGTH + 1] = "";
uint8_t copyTargetCar = 0;
bool copyWithName = false;
bool copyGotoCar = false;
// false: COPY selected -> target; true: RESTORE target -> selected
bool copyReverse = false;
// two-stage confirmation before resetting every car
uint8_t allDefaultsStage = 0;
// single tap on a car returns to the caller only after the double-tap window has passed
int8_t pendingCarTapIndex = -1;
uint32_t pendingCarTapAt = 0;

// race screen: three assignable cards (0 top-left, 1 top-right, 2 bottom-left); bottom-right is always CAR
constexpr uint8_t STATUS_SLOT_COUNT = 3;
uint8_t slotSetting[STATUS_SLOT_COUNT] = {1, 0, 2};
uint8_t savedSlotSetting[STATUS_SLOT_COUNT];
uint8_t assignSlot = 0;
int8_t pendingSlotTap = -1;
uint32_t pendingSlotTapAt = 0;
// race gestures lock to the axis of the first movement; card halves decide the target
uint8_t statusGestureAxis = 0;
int16_t statusGestureStartX = 0;
int16_t statusGestureStartY = 0;
int16_t carSwipeRemainder = 0;
constexpr int16_t CAR_SWIPE_STEP = 60;
constexpr int16_t STATUS_CARD_WIDTH = 196;
constexpr int16_t STATUS_CARD_HEIGHT = 64;
constexpr int16_t STATUS_LEFT_X = 20;
constexpr int16_t STATUS_RIGHT_X = 240;
constexpr int16_t STATUS_TOP_Y = 58;
constexpr int16_t STATUS_BOTTOM_Y = 130;
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

// 3 columns of 148px + 4px gaps fill the 456px row edge-to-edge (same grid as car menu)
constexpr int16_t SETTINGS_BUTTON_WIDTH = 148;
constexpr int16_t SETTINGS_BUTTON_HEIGHT = 82;
constexpr int16_t SETTINGS_COLUMN_STEP = 152;
constexpr int16_t SETTINGS_GRID_LEFT = 2;
// 11 settings -> 6 columns, 3 visible
constexpr int16_t SETTINGS_MAX_SCROLL = 3 * SETTINGS_COLUMN_STEP;
// edit screen: content spans y 50..280 (CANCEL/SAVE in header); upper half relative, lower half absolute
constexpr int16_t EDIT_SPLIT_Y = 165;

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

char carNames[][CAR_NAME_LENGTH + 1] = {
    "FORMEL 1", "CAR 02", "CAR 03", "CAR 04", "CAR 05",
    "CAR 06", "CAR 07", "CAR 08", "CAR 09", "CAR 10",
    "CAR 11", "CAR 12", "CAR 13", "CAR 14", "CAR 15",
    "CAR 16", "CAR 17", "CAR 18", "CAR 19", "CAR 20",
    "CAR 21", "CAR 22", "CAR 23", "CAR 24", "CAR 25",
    "CAR 26", "CAR 27", "CAR 28", "CAR 29", "CAR 30"};
constexpr uint8_t CAR_COUNT = sizeof(carNames) / sizeof(carNames[0]);
constexpr uint8_t SETTING_COUNT = sizeof(settings) / sizeof(settings[0]);

// global (not per-car) options shown on the Config page; edited with the same Edit screen
SettingItem configItems[] = {
    {"DBLTAP", 400, 200, 1000, 'm'},
};
constexpr uint8_t CONFIG_COUNT = sizeof(configItems) / sizeof(configItems[0]);
constexpr uint8_t CONFIG_DOUBLE_TAP = 0;
uint8_t selectedConfig = 0;
bool editingConfig = false;
int16_t savedConfigValues[CONFIG_COUNT];

SettingItem &editItem() {
  return editingConfig ? configItems[selectedConfig] : settings[selectedSetting];
}

uint32_t doubleTapMs() {
  return (uint32_t)configItems[CONFIG_DOUBLE_TAP].value;
}
constexpr int16_t CURVE_X_DEFAULT = 50;
// per-car profiles; `settings[].value` / `curveXValue` are the working copy of the selected car
int16_t carValues[CAR_COUNT][SETTING_COUNT];
int16_t carCurveX[CAR_COUNT];
int16_t settingDefaults[SETTING_COUNT];

// NVS persistence: one blob per car, written debounced after the last touch
struct CarBlob {
  char name[CAR_NAME_LENGTH + 1];
  int16_t values[SETTING_COUNT];
  int16_t curveX;
};
constexpr uint16_t PROFILE_VERSION = 1;
constexpr uint32_t PERSIST_DELAY_MS = 2000;
Preferences prefs;
CarBlob savedBlobs[CAR_COUNT];
uint8_t savedSelectedCar = 0;
uint32_t pendingSaveAt = 0;

void storeCurrentCar() {
  for (uint8_t i = 0; i < SETTING_COUNT; i++) {
    carValues[selectedCar][i] = settings[i].value;
  }
  carCurveX[selectedCar] = curveXValue;
}

void loadCar(uint8_t index) {
  selectedCar = index;
  for (uint8_t i = 0; i < SETTING_COUNT; i++) {
    settings[i].value = carValues[index][i];
  }
  curveXValue = carCurveX[index];
}

void selectCar(uint8_t index) {
  storeCurrentCar();
  loadCar(index);
}

void resetCarToDefaults(uint8_t car) {
  for (uint8_t i = 0; i < SETTING_COUNT; i++) {
    carValues[car][i] = settingDefaults[i];
  }
  carCurveX[car] = CURVE_X_DEFAULT;
}

void fillBlob(uint8_t car, CarBlob &blob) {
  memset(&blob, 0, sizeof(blob));
  strcpy(blob.name, carNames[car]);
  memcpy(blob.values, carValues[car], sizeof(blob.values));
  blob.curveX = carCurveX[car];
}

void carKey(uint8_t car, char *key, size_t size) {
  snprintf(key, size, "car%02u", car);
}

void persistProfiles() {
  storeCurrentCar();
  char key[8];
  for (uint8_t car = 0; car < CAR_COUNT; car++) {
    CarBlob blob;
    fillBlob(car, blob);
    if (memcmp(&blob, &savedBlobs[car], sizeof(blob)) != 0) {
      carKey(car, key, sizeof(key));
      prefs.putBytes(key, &blob, sizeof(blob));
      savedBlobs[car] = blob;
    }
  }
  if (selectedCar != savedSelectedCar) {
    prefs.putUChar("sel", selectedCar);
    savedSelectedCar = selectedCar;
  }
  for (uint8_t i = 0; i < CONFIG_COUNT; i++) {
    if (configItems[i].value != savedConfigValues[i]) {
      prefs.putShort(configItems[i].name, configItems[i].value);
      savedConfigValues[i] = configItems[i].value;
    }
  }
  char slotKey[8];
  for (uint8_t i = 0; i < STATUS_SLOT_COUNT; i++) {
    if (slotSetting[i] != savedSlotSetting[i]) {
      snprintf(slotKey, sizeof(slotKey), "slot%u", i);
      prefs.putUChar(slotKey, slotSetting[i]);
      savedSlotSetting[i] = slotSetting[i];
    }
  }
}

void initCars() {
  // settings[] initializers are the defaults
  for (uint8_t i = 0; i < SETTING_COUNT; i++) {
    settingDefaults[i] = settings[i].value;
  }
  for (uint8_t car = 0; car < CAR_COUNT; car++) {
    resetCarToDefaults(car);
  }

  prefs.begin("espeed", false);
  const bool valid = prefs.getUShort("ver", 0) == PROFILE_VERSION;
  char key[8];
  for (uint8_t car = 0; car < CAR_COUNT; car++) {
    CarBlob blob;
    carKey(car, key, sizeof(key));
    if (valid && prefs.getBytes(key, &blob, sizeof(blob)) == sizeof(blob)) {
      blob.name[CAR_NAME_LENGTH] = '\0';
      strcpy(carNames[car], blob.name);
      memcpy(carValues[car], blob.values, sizeof(blob.values));
      carCurveX[car] = blob.curveX;
    }
    fillBlob(car, savedBlobs[car]);
    if (!valid) {
      prefs.putBytes(key, &savedBlobs[car], sizeof(CarBlob));
    }
  }
  if (!valid) {
    prefs.putUShort("ver", PROFILE_VERSION);
    prefs.putUChar("sel", 0);
  }
  savedSelectedCar = min(prefs.getUChar("sel", 0), (uint8_t)(CAR_COUNT - 1));
  loadCar(savedSelectedCar);
  for (uint8_t i = 0; i < CONFIG_COUNT; i++) {
    configItems[i].value = constrain(prefs.getShort(configItems[i].name, configItems[i].value),
                                     configItems[i].minimum, configItems[i].maximum);
    savedConfigValues[i] = configItems[i].value;
  }
  char slotKey[8];
  for (uint8_t i = 0; i < STATUS_SLOT_COUNT; i++) {
    snprintf(slotKey, sizeof(slotKey), "slot%u", i);
    slotSetting[i] = min(prefs.getUChar(slotKey, slotSetting[i]), (uint8_t)(SETTING_COUNT - 1));
    savedSlotSetting[i] = slotSetting[i];
  }
}
// GFX text size is integer only: size 3 = 18px/char -> 144px for 8 chars; 3 columns of 148px fill 456px
constexpr uint8_t CAR_TEXT_SIZE = 3;
constexpr int16_t CAR_BUTTON_WIDTH = 8 * 6 * CAR_TEXT_SIZE + 4;
constexpr int16_t CAR_BUTTON_HEIGHT = 104;
constexpr int16_t CAR_COLUMN_STEP = CAR_BUTTON_WIDTH + 4;
constexpr int16_t CAR_ROW_STEP = 112;
constexpr int16_t CAR_GRID_LEFT = 2;
constexpr int16_t CAR_GRID_TOP = 56;
constexpr int16_t CARS_MAX_SCROLL = ((CAR_COUNT + 1) / 2 - 3) * CAR_COLUMN_STEP;
int16_t carsScroll = 0;
int16_t carsTouchStartScroll = 0;
bool carsDragging = false;

// CarEdit character picker: two independently scrollable rows of 60px buttons
const char *const charRows[] = {
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz",
    "0123456789 !\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~"};
constexpr uint8_t CHAR_ROW_COUNT = sizeof(charRows) / sizeof(charRows[0]);
constexpr int16_t CHAR_ROW_TOP = 100;
constexpr int16_t CHAR_ROW_HEIGHT = 82;
constexpr int16_t CHAR_ROW_STEP_Y = 92;
constexpr uint8_t CHAR_PICKER_TEXT_SIZE = 4;
constexpr int16_t CHAR_PICKER_WIDTH = 60;
constexpr int16_t CHAR_PICKER_STEP = CHAR_PICKER_WIDTH + 4;
constexpr int16_t CHAR_PICKER_LEFT = 2;
int16_t charScroll[CHAR_ROW_COUNT] = {};
int16_t charTouchStartScroll[CHAR_ROW_COUNT] = {};

int16_t charRowMaxScroll(uint8_t row) {
  const int16_t count = strlen(charRows[row]);
  return max((int16_t)0, (int16_t)(CHAR_PICKER_LEFT + (count - 1) * CHAR_PICKER_STEP - (456 - CHAR_PICKER_LEFT - CHAR_PICKER_WIDTH)));
}

// returns row index for a y coordinate inside a button row, or -1
int8_t charRowAt(int16_t y) {
  if (y < CHAR_ROW_TOP) {
    return -1;
  }
  const int16_t offset = (y - CHAR_ROW_TOP) % CHAR_ROW_STEP_Y;
  const int16_t row = (y - CHAR_ROW_TOP) / CHAR_ROW_STEP_Y;
  return (row < CHAR_ROW_COUNT && offset < CHAR_ROW_HEIGHT) ? (int8_t)row : -1;
}

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

void printSettingValue(const SettingItem &item, int16_t x, int16_t y);

void statusCardRect(uint8_t card, int16_t &x, int16_t &y) {
  x = (card % 2 == 0) ? STATUS_LEFT_X : STATUS_RIGHT_X;
  y = (card < 2) ? STATUS_TOP_Y : STATUS_BOTTOM_Y;
}

// 0..2 = assignable slots, 3 = CAR card, -1 = none
int8_t statusCardAt(int16_t x, int16_t y) {
  for (uint8_t card = 0; card < 4; card++) {
    int16_t cardX;
    int16_t cardY;
    statusCardRect(card, cardX, cardY);
    if (x >= cardX && x < cardX + STATUS_CARD_WIDTH && y >= cardY && y < cardY + STATUS_CARD_HEIGHT) {
      return (int8_t)card;
    }
  }
  return -1;
}

void printSupplyVoltage() {
  screen->setTextColor(COLOR_TEXT);
  screen->setTextSize(3);
  screen->setCursor(300, 240);
  screen->print(supplyVoltage, 2);
  screen->print(" V");
}

void drawStatusPage() {
  drawHeader("ESPEED32");
  screen->fillRoundRect(322, 6, 120, 34, 8, COLOR_PANEL);
  screen->drawRoundRect(322, 6, 120, 34, 8, COLOR_ACCENT);
  screen->setTextColor(COLOR_TEXT);
  screen->setTextSize(2);
  screen->setCursor(334, 15);
  screen->print("SETTINGS");

  for (uint8_t card = 0; card < 4; card++) {
    int16_t x;
    int16_t y;
    statusCardRect(card, x, y);
    screen->fillRoundRect(x, y, STATUS_CARD_WIDTH, STATUS_CARD_HEIGHT, 10, COLOR_PANEL);
    screen->drawRoundRect(x, y, STATUS_CARD_WIDTH, STATUS_CARD_HEIGHT, 10, card == 3 ? COLOR_ACCENT_WARM : COLOR_BORDER);
    screen->setTextColor(COLOR_LABEL);
    screen->setTextSize(2);
    screen->setCursor(x + 14, y + 8);
    if (card == 3) {
      screen->print("CAR");
      screen->setTextColor(COLOR_TEXT);
      screen->setTextSize(3);
      screen->setCursor(x + 14, y + 32);
      screen->print(carNames[selectedCar]);
    } else {
      screen->print(settings[slotSetting[card]].name);
      printSettingValue(settings[slotSetting[card]], x + 14, y + 28);
    }
  }

  screen->fillRoundRect(20, 208, 416, 64, 10, COLOR_PANEL_ALT);
  screen->drawRoundRect(20, 208, 416, 64, 10, COLOR_ACCENT);
  screen->setTextColor(COLOR_LABEL);
  screen->setTextSize(2);
  screen->setCursor(38, 218);
  screen->print("TRIGGER");
  screen->setCursor(300, 218);
  screen->print("VCC");
  screen->setTextColor(COLOR_TEXT);
  screen->setTextSize(3);
  screen->setCursor(38, 240);
  screen->print("0%");
  printSupplyVoltage();
}

void printSettingValue(const SettingItem &item, int16_t x, int16_t y) {
  screen->setTextColor(COLOR_TEXT);
  screen->setTextSize(4);
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
  screen->fillRoundRect(196, 6, 116, 34, 8, COLOR_PANEL);
  screen->drawRoundRect(196, 6, 116, 34, 8, COLOR_ACCENT);
  screen->setTextColor(COLOR_TEXT);
  screen->setTextSize(2);
  screen->setCursor(218, 15);
  screen->print("CONFIG");
  screen->fillRoundRect(322, 6, 120, 34, 8, COLOR_PANEL);
  screen->drawRoundRect(322, 6, 120, 34, 8, COLOR_ACCENT_WARM);
  screen->setTextColor(COLOR_TEXT);
  screen->setTextSize(2);
  screen->setCursor(338, 15);
  screen->print(carNames[selectedCar]);

  for (uint8_t index = 0; index < sizeof(settings) / sizeof(settings[0]); index++) {
    const uint8_t column = index / 2;
    const uint8_t row = index % 2;
    const int16_t x = SETTINGS_GRID_LEFT + column * SETTINGS_COLUMN_STEP - settingsScroll;
    const int16_t y = 56 + row * 92;
    if (x < 0 || x + SETTINGS_BUTTON_WIDTH > 456) {
      continue;
    }
    screen->fillRoundRect(x, y, SETTINGS_BUTTON_WIDTH, SETTINGS_BUTTON_HEIGHT, 12, COLOR_PANEL);
    screen->drawRoundRect(x, y, SETTINGS_BUTTON_WIDTH, SETTINGS_BUTTON_HEIGHT, 12, COLOR_BORDER);
    // label 24px + value 32px fill the 82px card with ~7px margins
    screen->setTextColor(COLOR_LABEL);
    screen->setTextSize(3);
    screen->setCursor(x + 12, y + 7);
    screen->print(settings[index].name);
    printSettingValue(settings[index], x + 12, y + 43);
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
  const SettingItem &item = editItem();
  drawHeader(item.name);

  screen->fillRoundRect(232, 6, 100, 34, 8, COLOR_PANEL);
  screen->drawRoundRect(232, 6, 100, 34, 8, COLOR_DANGER);
  screen->fillRoundRect(342, 6, 100, 34, 8, COLOR_PANEL);
  screen->drawRoundRect(342, 6, 100, 34, 8, COLOR_ACCENT);
  screen->setTextColor(COLOR_TEXT);
  screen->setTextSize(2);
  screen->setCursor(246, 15);
  screen->print("CANCEL");
  screen->setCursor(368, 15);
  screen->print("SAVE");

  const bool hasGraph = strcmp(item.name, "CURVE") == 0 || strcmp(item.name, "EXPO") == 0;
  const bool isToggle = item.name[0] == 'B' && item.name[1] == 'Y';
  if (hasGraph) {
    screen->setTextColor(COLOR_TEXT);
    screen->setTextSize(4);
    screen->setCursor(24, 62);
    screen->print(item.value);
    if (item.unit != ' ') {
      screen->setTextSize(2);
      screen->print(item.unit);
    }
  } else {
    char range[32];
    if (isToggle) {
      snprintf(range, sizeof(range), "OFF - ON");
    } else if (item.unit != ' ') {
      snprintf(range, sizeof(range), "%d%c - %d%c", item.minimum, item.unit, item.maximum, item.unit);
    } else {
      snprintf(range, sizeof(range), "%d - %d", item.minimum, item.maximum);
    }
    screen->setTextColor(COLOR_LABEL);
    screen->setTextSize(3);
    screen->setCursor((456 - (int16_t)strlen(range) * 18) / 2, 56);
    screen->print(range);

    char value[8];
    if (isToggle) {
      snprintf(value, sizeof(value), "%s", item.value ? "ON" : "OFF");
    } else {
      snprintf(value, sizeof(value), "%d", item.value);
    }
    const uint8_t valueSize = 8;
    const uint8_t unitSize = 4;
    const bool showUnit = !isToggle && item.unit != ' ';
    const int16_t valueWidth = strlen(value) * 6 * valueSize + (showUnit ? 6 * unitSize : 0);
    const int16_t valueX = (456 - valueWidth) / 2;
    // vertically centered in the area below the range line (80..280)
    const int16_t valueY = 180 - 4 * valueSize;
    screen->setTextColor(COLOR_TEXT);
    screen->setTextSize(valueSize);
    screen->setCursor(valueX, valueY);
    screen->print(value);
    if (showUnit) {
      screen->setTextSize(unitSize);
      screen->setCursor(valueX + strlen(value) * 6 * valueSize, valueY + 8 * valueSize - 8 * unitSize);
      screen->print(item.unit);
    }
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
  }
}

void drawCarGrid(uint8_t highlighted) {
  for (uint8_t i = 0; i < CAR_COUNT; i++) {
    const int16_t x = CAR_GRID_LEFT + (i / 2) * CAR_COLUMN_STEP - carsScroll;
    const int16_t y = CAR_GRID_TOP + (i % 2) * CAR_ROW_STEP;
    if (x < 0 || x + CAR_BUTTON_WIDTH > 456) {
      continue;
    }
    screen->fillRoundRect(x, y, CAR_BUTTON_WIDTH, CAR_BUTTON_HEIGHT, 12, COLOR_PANEL);
    screen->drawRoundRect(x, y, CAR_BUTTON_WIDTH, CAR_BUTTON_HEIGHT, 12,
                          i == highlighted ? COLOR_ACCENT_WARM : COLOR_BORDER);
    screen->setTextColor(COLOR_TEXT);
    screen->setTextSize(CAR_TEXT_SIZE);
    const int16_t textWidth = strlen(carNames[i]) * 6 * CAR_TEXT_SIZE;
    screen->setCursor(x + (CAR_BUTTON_WIDTH - textWidth) / 2, y + (CAR_BUTTON_HEIGHT - 8 * CAR_TEXT_SIZE) / 2);
    screen->print(carNames[i]);
  }
}

// returns the car index under (x, y) in the grid, or -1
int8_t carAt(int16_t x, int16_t y) {
  for (uint8_t index = 0; index < CAR_COUNT; index++) {
    const int16_t cardX = CAR_GRID_LEFT + (index / 2) * CAR_COLUMN_STEP - carsScroll;
    const int16_t cardY = CAR_GRID_TOP + (index % 2) * CAR_ROW_STEP;
    if (x >= cardX && x < cardX + CAR_BUTTON_WIDTH &&
        y >= cardY && y < cardY + CAR_BUTTON_HEIGHT) {
      return (int8_t)index;
    }
  }
  return -1;
}

void drawCarsPage() {
  drawHeader("SELECT CAR");
  screen->fillRoundRect(232, 6, 100, 34, 8, COLOR_PANEL);
  screen->drawRoundRect(232, 6, 100, 34, 8, COLOR_ACCENT_WARM);
  screen->setTextColor(COLOR_TEXT);
  screen->setTextSize(2);
  screen->setCursor(258, 15);
  screen->print("EDIT");
  screen->fillRoundRect(342, 6, 100, 34, 8, COLOR_PANEL);
  screen->drawRoundRect(342, 6, 100, 34, 8, COLOR_ACCENT);
  screen->setCursor(368, 15);
  screen->print("BACK");
  drawCarGrid(selectedCar);
}

void drawCarOptionsPage() {
  drawHeader("CAR");
  screen->fillRoundRect(342, 6, 100, 34, 8, COLOR_PANEL);
  screen->drawRoundRect(342, 6, 100, 34, 8, COLOR_ACCENT);
  screen->setTextColor(COLOR_TEXT);
  screen->setTextSize(2);
  screen->setCursor(368, 15);
  screen->print("BACK");

  screen->setTextColor(COLOR_ACCENT_WARM);
  screen->setTextSize(4);
  screen->setCursor((456 - (int16_t)strlen(carNames[selectedCar]) * 24) / 2, 62);
  screen->print(carNames[selectedCar]);

  const char *labels[] = {"RENAME", "COPY", "RESTORE", "DEFAULTS"};
  const uint16_t colors[] = {COLOR_ACCENT, COLOR_ACCENT_WARM, COLOR_LABEL, COLOR_DANGER};
  for (uint8_t i = 0; i < 4; i++) {
    const int16_t x = (i % 2 == 0) ? 20 : 238;
    const int16_t y = (i < 2) ? 116 : 196;
    screen->fillRoundRect(x, y, 198, 66, 12, COLOR_PANEL);
    screen->drawRoundRect(x, y, 198, 66, 12, colors[i]);
    screen->setTextColor(COLOR_TEXT);
    screen->setTextSize(3);
    screen->setCursor(x + (198 - (int16_t)strlen(labels[i]) * 18) / 2, y + 21);
    screen->print(labels[i]);
  }
}

void drawCarCopyPage() {
  if (copyReverse) {
    drawHeader("RESTORE");
    screen->fillRoundRect(160, 6, 120, 34, 8, COLOR_PANEL);
    screen->drawRoundRect(160, 6, 120, 34, 8, COLOR_ACCENT_WARM);
    screen->setTextColor(COLOR_TEXT);
    screen->setTextSize(2);
    screen->setCursor(160 + (120 - (int16_t)strlen(carNames[selectedCar]) * 12) / 2, 15);
    screen->print(carNames[selectedCar]);
  } else {
    drawHeader("COPY");
    // source name badge between COPY and TO
    screen->fillRoundRect(102, 6, 120, 34, 8, COLOR_PANEL);
    screen->drawRoundRect(102, 6, 120, 34, 8, COLOR_ACCENT_WARM);
    screen->setTextColor(COLOR_TEXT);
    screen->setTextSize(2);
    screen->setCursor(102 + (120 - (int16_t)strlen(carNames[selectedCar]) * 12) / 2, 15);
    screen->print(carNames[selectedCar]);
    screen->setTextSize(3);
    screen->setCursor(236, 11);
    screen->print("TO");
  }
  screen->fillRoundRect(342, 6, 100, 34, 8, COLOR_PANEL);
  screen->drawRoundRect(342, 6, 100, 34, 8, COLOR_ACCENT);
  screen->setTextColor(COLOR_TEXT);
  screen->setTextSize(2);
  screen->setCursor(368, 15);
  screen->print("BACK");
  drawCarGrid(selectedCar);
}

void drawCarCopyConfirmPage() {
  const uint8_t sourceCar = copyReverse ? copyTargetCar : selectedCar;
  const uint8_t destCar = copyReverse ? selectedCar : copyTargetCar;
  drawHeader(copyReverse ? "RESTORE" : "COPY");
  screen->fillRoundRect(232, 6, 100, 34, 8, COLOR_PANEL);
  screen->drawRoundRect(232, 6, 100, 34, 8, COLOR_DANGER);
  screen->fillRoundRect(342, 6, 100, 34, 8, COLOR_PANEL);
  screen->drawRoundRect(342, 6, 100, 34, 8, COLOR_ACCENT);
  screen->setTextColor(COLOR_TEXT);
  screen->setTextSize(2);
  screen->setCursor(246, 15);
  screen->print("CANCEL");
  screen->setCursor(368, 15);
  screen->print("SAVE");

  screen->setTextColor(COLOR_LABEL);
  screen->setTextSize(2);
  screen->setCursor(20, 64);
  screen->print("FROM");
  screen->setCursor(240, 64);
  screen->print("TO");
  screen->setTextColor(COLOR_ACCENT_WARM);
  screen->setTextSize(4);
  screen->setCursor(20, 88);
  screen->print(carNames[sourceCar]);
  screen->setTextColor(COLOR_TEXT);
  screen->setCursor(240, 88);
  screen->print(carNames[destCar]);

  screen->drawRoundRect(20, 136, 28, 28, 6, COLOR_ACCENT);
  if (copyWithName) {
    screen->fillRoundRect(26, 142, 16, 16, 3, COLOR_ACCENT);
  }
  screen->setTextColor(COLOR_TEXT);
  screen->setTextSize(2);
  screen->setCursor(60, 142);
  screen->print("WITH NAME");
  if (!copyReverse) {
    screen->drawRoundRect(240, 136, 28, 28, 6, COLOR_ACCENT);
    if (copyGotoCar) {
      screen->fillRoundRect(246, 142, 16, 16, 3, COLOR_ACCENT);
    }
    screen->setCursor(280, 142);
    screen->print("GOTO CAR");
  }

  screen->fillRoundRect(20, 176, 416, 80, 10, COLOR_PANEL);
  screen->drawRoundRect(20, 176, 416, 80, 10, COLOR_BORDER);
  screen->setTextColor(COLOR_LABEL);
  screen->setTextSize(2);
  screen->setCursor(36, 188);
  screen->print("WILL BE OVERWRITTEN:");
  screen->setTextColor(COLOR_TEXT);
  screen->setTextSize(3);
  screen->setCursor(36, 216);
  screen->print(copyWithName ? "SETTINGS + NAME" : "SETTINGS");
}

void drawCarDefaultsConfirmPage() {
  drawHeader("DEFAULTS");
  screen->fillRoundRect(232, 6, 100, 34, 8, COLOR_PANEL);
  screen->drawRoundRect(232, 6, 100, 34, 8, COLOR_ACCENT);
  screen->fillRoundRect(342, 6, 100, 34, 8, COLOR_PANEL);
  screen->drawRoundRect(342, 6, 100, 34, 8, COLOR_DANGER);
  screen->setTextColor(COLOR_TEXT);
  screen->setTextSize(2);
  screen->setCursor(246, 15);
  screen->print("CANCEL");
  screen->setCursor(362, 15);
  screen->print("RESET");

  screen->setTextColor(COLOR_ACCENT_WARM);
  screen->setTextSize(4);
  screen->setCursor((456 - (int16_t)strlen(carNames[selectedCar]) * 24) / 2, 70);
  screen->print(carNames[selectedCar]);

  screen->fillRoundRect(20, 130, 416, 96, 10, COLOR_PANEL);
  screen->drawRoundRect(20, 130, 416, 96, 10, COLOR_DANGER);
  screen->setTextColor(COLOR_TEXT);
  screen->setTextSize(2);
  const char *line1 = "RESET ALL SETTINGS";
  const char *line2 = "TO DEFAULT VALUES?";
  screen->setCursor((456 - (int16_t)strlen(line1) * 12) / 2, 150);
  screen->print(line1);
  screen->setCursor((456 - (int16_t)strlen(line2) * 12) / 2, 174);
  screen->print(line2);
  screen->setTextColor(COLOR_LABEL);
  const char *line3 = "NAME IS KEPT";
  screen->setCursor((456 - (int16_t)strlen(line3) * 12) / 2, 200);
  screen->print(line3);
}

void drawCarEditPage() {
  drawHeader("RENAME");
  screen->fillRoundRect(232, 6, 100, 34, 8, COLOR_PANEL);
  screen->drawRoundRect(232, 6, 100, 34, 8, COLOR_ACCENT);
  screen->fillRoundRect(342, 6, 100, 34, 8, COLOR_PANEL);
  screen->drawRoundRect(342, 6, 100, 34, 8, COLOR_DANGER);
  screen->setTextColor(COLOR_TEXT);
  screen->setTextSize(2);
  screen->setCursor(258, 15);
  screen->print("SAVE");
  screen->setCursor(368, 15);
  screen->print("BACK");

  // name padded to 8 slots so the cursor can reach every editable position
  screen->setTextSize(4);
  const char *name = carNames[selectedCar];
  const uint8_t nameLength = strlen(name);
  for (uint8_t i = 0; i < CAR_NAME_LENGTH; i++) {
    const int16_t charX = 20 + i * 24;
    const char c = i < nameLength ? name[i] : ' ';
    if (i == carEditCursor) {
      screen->fillRect(charX, 58, 24, 32, COLOR_ACCENT_WARM);
      screen->setTextColor(COLOR_BG);
    } else {
      screen->setTextColor(COLOR_ACCENT_WARM);
    }
    screen->setCursor(charX, 58);
    screen->print(c);
  }

  screen->fillRoundRect(232, 56, 100, 36, 8, COLOR_PANEL);
  screen->drawRoundRect(232, 56, 100, 36, 8, COLOR_BORDER);
  screen->fillRoundRect(342, 56, 100, 36, 8, COLOR_PANEL);
  screen->drawRoundRect(342, 56, 100, 36, 8, COLOR_BORDER);
  screen->setTextColor(COLOR_TEXT);
  screen->setTextSize(3);
  screen->setCursor(273, 62);
  screen->print("<");
  screen->setCursor(383, 62);
  screen->print(">");

  for (uint8_t row = 0; row < CHAR_ROW_COUNT; row++) {
    const int16_t y = CHAR_ROW_TOP + row * CHAR_ROW_STEP_Y;
    const char *chars = charRows[row];
    for (uint8_t i = 0; chars[i] != '\0'; i++) {
      const int16_t x = CHAR_PICKER_LEFT + i * CHAR_PICKER_STEP - charScroll[row];
      if (x < 0 || x + CHAR_PICKER_WIDTH > 456) {
        continue;
      }
      screen->fillRoundRect(x, y, CHAR_PICKER_WIDTH, CHAR_ROW_HEIGHT, 8, COLOR_PANEL);
      screen->drawRoundRect(x, y, CHAR_PICKER_WIDTH, CHAR_ROW_HEIGHT, 8, COLOR_BORDER);
      screen->setTextColor(COLOR_TEXT);
      screen->setTextSize(CHAR_PICKER_TEXT_SIZE);
      screen->setCursor(x + (CHAR_PICKER_WIDTH - 6 * CHAR_PICKER_TEXT_SIZE) / 2,
                        y + (CHAR_ROW_HEIGHT - 8 * CHAR_PICKER_TEXT_SIZE) / 2);
      screen->print(chars[i]);
    }
  }
}

void drawAssignSlotPage() {
  char title[16];
  snprintf(title, sizeof(title), "SLOT %u", assignSlot + 1);
  drawHeader(title);
  screen->fillRoundRect(342, 6, 100, 34, 8, COLOR_PANEL);
  screen->drawRoundRect(342, 6, 100, 34, 8, COLOR_ACCENT);
  screen->setTextColor(COLOR_TEXT);
  screen->setTextSize(2);
  screen->setCursor(368, 15);
  screen->print("BACK");

  for (uint8_t index = 0; index < SETTING_COUNT; index++) {
    const int16_t x = SETTINGS_GRID_LEFT + (index / 2) * SETTINGS_COLUMN_STEP - settingsScroll;
    const int16_t y = 56 + (index % 2) * 92;
    if (x < 0 || x + SETTINGS_BUTTON_WIDTH > 456) {
      continue;
    }
    screen->fillRoundRect(x, y, SETTINGS_BUTTON_WIDTH, SETTINGS_BUTTON_HEIGHT, 12, COLOR_PANEL);
    screen->drawRoundRect(x, y, SETTINGS_BUTTON_WIDTH, SETTINGS_BUTTON_HEIGHT, 12,
                          index == slotSetting[assignSlot] ? COLOR_ACCENT_WARM : COLOR_BORDER);
    screen->setTextColor(COLOR_TEXT);
    screen->setTextSize(3);
    screen->setCursor(x + 12, y + (SETTINGS_BUTTON_HEIGHT - 24) / 2);
    screen->print(settings[index].name);
  }
}

void drawConfigPage() {
  drawHeader("CONFIG");
  screen->fillRoundRect(342, 6, 100, 34, 8, COLOR_PANEL);
  screen->drawRoundRect(342, 6, 100, 34, 8, COLOR_ACCENT);
  screen->setTextColor(COLOR_TEXT);
  screen->setTextSize(2);
  screen->setCursor(368, 15);
  screen->print("BACK");

  // config cards use the top grid row only; action buttons sit below
  for (uint8_t index = 0; index < CONFIG_COUNT; index++) {
    const int16_t x = SETTINGS_GRID_LEFT + index * SETTINGS_COLUMN_STEP;
    const int16_t y = 56;
    screen->fillRoundRect(x, y, SETTINGS_BUTTON_WIDTH, SETTINGS_BUTTON_HEIGHT, 12, COLOR_PANEL);
    screen->drawRoundRect(x, y, SETTINGS_BUTTON_WIDTH, SETTINGS_BUTTON_HEIGHT, 12, COLOR_BORDER);
    screen->setTextColor(COLOR_LABEL);
    screen->setTextSize(3);
    screen->setCursor(x + 12, y + 7);
    screen->print(configItems[index].name);
    printSettingValue(configItems[index], x + 12, y + 43);
  }

  screen->fillRoundRect(20, 196, 198, 66, 12, COLOR_PANEL);
  screen->drawRoundRect(20, 196, 198, 66, 12, COLOR_LABEL);
  screen->fillRoundRect(238, 196, 198, 66, 12, COLOR_PANEL);
  screen->drawRoundRect(238, 196, 198, 66, 12, COLOR_DANGER);
  screen->setTextColor(COLOR_TEXT);
  screen->setTextSize(3);
  screen->setCursor(20 + (198 - 4 * 18) / 2, 217);
  screen->print("HELP");
  screen->setCursor(238 + (198 - 8 * 18) / 2, 217);
  screen->print("DEFAULTS");
}

void drawAllDefaultsConfirmPage() {
  drawHeader(allDefaultsStage == 0 ? "WARNING" : "CONFIRM");
  screen->fillRoundRect(232, 6, 100, 34, 8, COLOR_PANEL);
  screen->drawRoundRect(232, 6, 100, 34, 8, COLOR_ACCENT);
  screen->fillRoundRect(342, 6, 100, 34, 8, COLOR_PANEL);
  screen->drawRoundRect(342, 6, 100, 34, 8, COLOR_DANGER);
  screen->setTextColor(COLOR_TEXT);
  screen->setTextSize(2);
  screen->setCursor(246, 15);
  screen->print("CANCEL");
  if (allDefaultsStage == 0) {
    screen->setCursor(368, 15);
    screen->print("NEXT");
  } else {
    screen->setCursor(362, 15);
    screen->print("RESET");
  }

  const uint16_t panelFill = allDefaultsStage == 0 ? COLOR_PANEL : COLOR_DANGER;
  const uint16_t textColor = allDefaultsStage == 0 ? COLOR_DANGER : COLOR_TEXT;
  screen->fillRoundRect(20, 60, 416, 204, 12, panelFill);
  screen->drawRoundRect(20, 60, 416, 204, 12, COLOR_DANGER);
  screen->setTextColor(textColor);
  screen->setTextSize(3);
  const char *line1 = allDefaultsStage == 0 ? "RESET ALL 30 CARS?" : "LAST WARNING!";
  screen->setCursor((456 - (int16_t)strlen(line1) * 18) / 2, 76);
  screen->print(line1);
  screen->setTextSize(2);
  const char *line2 = allDefaultsStage == 0 ? "ALL CAR SETTINGS WILL BE" : "ALL SETTINGS OF ALL CARS";
  const char *line3 = allDefaultsStage == 0 ? "REPLACED BY DEFAULT VALUES" : "WILL BE LOST PERMANENTLY.";
  const char *line4 = allDefaultsStage == 0 ? "NAMES ARE KEPT" : "THIS CANNOT BE UNDONE!";
  const char *line5 = allDefaultsStage == 0 ? "NEXT: SECOND CONFIRMATION" : "PRESS RESET TO PROCEED";
  screen->setCursor((456 - (int16_t)strlen(line2) * 12) / 2, 122);
  screen->print(line2);
  screen->setCursor((456 - (int16_t)strlen(line3) * 12) / 2, 146);
  screen->print(line3);
  screen->setTextColor(allDefaultsStage == 0 ? COLOR_LABEL : COLOR_TEXT);
  screen->setCursor((456 - (int16_t)strlen(line4) * 12) / 2, 184);
  screen->print(line4);
  screen->setCursor((456 - (int16_t)strlen(line5) * 12) / 2, 230);
  screen->print(line5);
}

// help screen: icon glyphs H = horizontal swipe, V = vertical swipe, T = tap, D = double tap, B = button, ' ' = none
struct HelpLine {
  char icon;
  const char *text;
};
const HelpLine helpPage1[] = {
    {' ', "RACE SCREEN"},
    {'H', "TOP: CARD 1  BOTTOM: CARD 2"},
    {'V', "LEFT: CARD 3  RIGHT: CAR"},
    {'T', "CAR CARD: CAR MENU"},
    {'D', "CARD 1-3: ASSIGN FUNCTION"},
    {'B', "SETTINGS: ALL VALUES"},
    {' ', "CARS"},
    {'T', "SELECT   DOUBLE: OPTIONS"},
};
const HelpLine helpPage2[] = {
    {' ', "SETTINGS / CARS"},
    {'H', "TOP: SCROLL  BOTTOM: JUMP"},
    {'T', "CARD: EDIT / SELECT"},
    {' ', "EDIT VALUE"},
    {'H', "TOP: RELATIVE +/-"},
    {'H', "BOTTOM: ABSOLUTE SLIDER"},
    {'T', "BOTTOM: SET VALUE"},
    {'B', "SAVE / CANCEL IN HEADER"},
};
constexpr uint8_t HELP_PAGE_COUNT = 2;
uint8_t helpPage = 0;

void drawHelpIcon(char icon, int16_t x, int16_t cy) {
  const uint16_t color = COLOR_ACCENT;
  switch (icon) {
    case 'H':
      screen->drawFastHLine(x + 4, cy, 24, color);
      screen->fillTriangle(x, cy, x + 7, cy - 5, x + 7, cy + 5, color);
      screen->fillTriangle(x + 32, cy, x + 25, cy - 5, x + 25, cy + 5, color);
      break;
    case 'V':
      screen->drawFastVLine(x + 16, cy - 10, 20, color);
      screen->fillTriangle(x + 16, cy - 13, x + 11, cy - 6, x + 21, cy - 6, color);
      screen->fillTriangle(x + 16, cy + 13, x + 11, cy + 6, x + 21, cy + 6, color);
      break;
    case 'T':
      screen->drawCircle(x + 16, cy, 10, color);
      screen->fillCircle(x + 16, cy, 5, color);
      break;
    case 'D':
      screen->drawCircle(x + 9, cy, 8, color);
      screen->fillCircle(x + 9, cy, 4, color);
      screen->drawCircle(x + 24, cy, 8, color);
      screen->fillCircle(x + 24, cy, 4, color);
      break;
    case 'B':
      screen->drawRoundRect(x + 2, cy - 8, 28, 16, 4, color);
      break;
    default:
      break;
  }
}

void drawHelpPage() {
  char title[16];
  snprintf(title, sizeof(title), "HELP %u/%u", helpPage + 1, HELP_PAGE_COUNT);
  drawHeader(title);
  screen->fillRoundRect(232, 6, 100, 34, 8, COLOR_PANEL);
  screen->drawRoundRect(232, 6, 100, 34, 8, COLOR_LABEL);
  screen->fillRoundRect(342, 6, 100, 34, 8, COLOR_PANEL);
  screen->drawRoundRect(342, 6, 100, 34, 8, COLOR_ACCENT);
  screen->setTextColor(COLOR_TEXT);
  screen->setTextSize(2);
  screen->setCursor(258, 15);
  screen->print("NEXT");
  screen->setCursor(368, 15);
  screen->print("BACK");

  const HelpLine *lines = helpPage == 0 ? helpPage1 : helpPage2;
  const uint8_t count = helpPage == 0 ? sizeof(helpPage1) / sizeof(helpPage1[0]) : sizeof(helpPage2) / sizeof(helpPage2[0]);
  for (uint8_t i = 0; i < count; i++) {
    const int16_t y = 56 + i * 27;
    if (lines[i].icon == ' ') {
      screen->setTextColor(COLOR_LABEL);
      screen->setTextSize(2);
      screen->setCursor(20, y + 5);
      screen->print(lines[i].text);
    } else {
      drawHelpIcon(lines[i].icon, 20, y + 12);
      screen->setTextColor(COLOR_TEXT);
      screen->setTextSize(2);
      screen->setCursor(64, y + 5);
      screen->print(lines[i].text);
    }
  }
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
  } else if (currentPage == MenuPage::Config) {
    drawConfigPage();
  } else if (currentPage == MenuPage::Help) {
    drawHelpPage();
  } else if (currentPage == MenuPage::AllDefaultsConfirm) {
    drawAllDefaultsConfirmPage();
  } else if (currentPage == MenuPage::AssignSlot) {
    drawAssignSlotPage();
  } else if (currentPage == MenuPage::CarCopy) {
    drawCarCopyPage();
  } else if (currentPage == MenuPage::CarOptions) {
    drawCarOptionsPage();
  } else if (currentPage == MenuPage::CarCopyConfirm) {
    drawCarCopyConfirmPage();
  } else if (currentPage == MenuPage::CarDefaultsConfirm) {
    drawCarDefaultsConfirmPage();
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

void setCarNameChar(uint8_t position, char c) {
  char *name = carNames[selectedCar];
  uint8_t length = strlen(name);
  // pad with spaces when writing beyond the current end
  while (length < position) {
    name[length++] = ' ';
  }
  name[position] = c;
  // everything after the edited character is discarded
  length = position + 1;
  name[length] = '\0';
  // trailing spaces would skew centering in the car grid
  while (length > 0 && name[length - 1] == ' ') {
    name[--length] = '\0';
  }
}

void openCarMenu(bool fromSettings) {
  carMenuReturnToSettings = fromSettings;
  carMenuReturnToStatus = !fromSettings;
  pendingCarTapIndex = -1;
  // center the selected car's column among the 3 visible columns
  const int16_t column = selectedCar / 2;
  carsScroll = constrain((int16_t)((column - 1) * CAR_COLUMN_STEP), (int16_t)0, CARS_MAX_SCROLL);
  currentPage = MenuPage::Cars;
}

void openCarEdit() {
  carEditCursor = 0;
  strcpy(carEditOriginalName, carNames[selectedCar]);
  for (uint8_t row = 0; row < CHAR_ROW_COUNT; row++) {
    charScroll[row] = 0;
  }
  currentPage = MenuPage::CarEdit;
}

void openCarOptions() {
  storeCurrentCar();
  pendingCarTapIndex = -1;
  currentPage = MenuPage::CarOptions;
}

void openCarCopy(bool reverse) {
  copyReverse = reverse;
  // grid centered on the selected car
  const int16_t column = selectedCar / 2;
  carsScroll = constrain((int16_t)((column - 1) * CAR_COLUMN_STEP), (int16_t)0, CARS_MAX_SCROLL);
  currentPage = MenuPage::CarCopy;
}

void handleTouch(int16_t x, int16_t y) {
  if (welcomeVisible) {
    if (x >= 140 && x <= 330 && y >= 180 && y <= 260) {
      welcomeVisible = false;
      drawCurrentPage();
    }
    return;
  }

  if (currentPage == MenuPage::Status) {
    if (y < 50 && x >= 322 && x < 442) {
      currentPage = MenuPage::Settings;
      drawCurrentPage();
      return;
    }
    const int8_t card = statusCardAt(x, y);
    if (card == 3) {
      openCarMenu(false);
      drawCurrentPage();
    } else if (card >= 0) {
      // double tap on an assignable card opens the function picker
      if (pendingSlotTap == card && millis() - pendingSlotTapAt < doubleTapMs()) {
        pendingSlotTap = -1;
        assignSlot = card;
        settingsScroll = 0;
        currentPage = MenuPage::AssignSlot;
        drawCurrentPage();
      } else {
        pendingSlotTap = card;
        pendingSlotTapAt = millis();
      }
    }
    return;
  }

  if (currentPage == MenuPage::AssignSlot) {
    if (x >= 342 && x < 442 && y < 50) {
      currentPage = MenuPage::Status;
      drawCurrentPage();
      return;
    }
    for (uint8_t index = 0; index < SETTING_COUNT; index++) {
      const int16_t cardX = SETTINGS_GRID_LEFT + (index / 2) * SETTINGS_COLUMN_STEP - settingsScroll;
      const int16_t cardY = 56 + (index % 2) * 92;
      if (x >= cardX && x < cardX + SETTINGS_BUTTON_WIDTH &&
          y >= cardY && y < cardY + SETTINGS_BUTTON_HEIGHT) {
        slotSetting[assignSlot] = index;
        currentPage = MenuPage::Status;
        drawCurrentPage();
        break;
      }
    }
    return;
  }

  if (currentPage == MenuPage::Settings) {
    if (x >= 196 && x < 312 && y < 50) {
      currentPage = MenuPage::Config;
    } else if (x >= 322 && x < 442 && y < 50) {
      openCarMenu(true);
    } else if (y >= 55 && y < 238) {
      for (uint8_t index = 0; index < sizeof(settings) / sizeof(settings[0]); index++) {
        const int16_t cardX = SETTINGS_GRID_LEFT + (index / 2) * SETTINGS_COLUMN_STEP - settingsScroll;
        const int16_t cardY = 56 + (index % 2) * 92;
        if (x >= cardX && x < cardX + SETTINGS_BUTTON_WIDTH &&
            y >= cardY && y < cardY + SETTINGS_BUTTON_HEIGHT) {
          selectedSetting = index;
          editingConfig = false;
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
    const bool cancelTap = x >= 232 && x < 332 && y < 50;
    const bool saveTap = x >= 342 && x < 442 && y < 50;
    if (cancelTap || saveTap) {
      if (cancelTap) {
        editItem().value = editOriginalValue;
        if (strcmp(editItem().name, "CURVE") == 0) {
          curveXValue = curveOriginalX;
          editItem().value = curveOriginalY;
        }
      }
      currentPage = editingConfig ? MenuPage::Config : MenuPage::Settings;
      drawCurrentPage();
    } else if (y >= EDIT_SPLIT_Y) {
      updateValueFromAbsoluteSlider(x);
    }
    return;
  }

  if (currentPage == MenuPage::Help) {
    if (y < 50 && x >= 232 && x < 332) {
      helpPage = (helpPage + 1) % HELP_PAGE_COUNT;
      drawCurrentPage();
    } else if (y < 50 && x >= 342 && x < 442) {
      currentPage = MenuPage::Config;
      drawCurrentPage();
    }
    return;
  }

  if (currentPage == MenuPage::AllDefaultsConfirm) {
    if (y < 50 && x >= 232 && x < 332) {
      currentPage = MenuPage::Config;
      drawCurrentPage();
    } else if (y < 50 && x >= 342 && x < 442) {
      if (allDefaultsStage == 0) {
        allDefaultsStage = 1;
      } else {
        for (uint8_t car = 0; car < CAR_COUNT; car++) {
          resetCarToDefaults(car);
        }
        loadCar(selectedCar);
        currentPage = MenuPage::Config;
      }
      drawCurrentPage();
    }
    return;
  }

  if (currentPage == MenuPage::Config) {
    if (x >= 342 && x < 442 && y < 50) {
      currentPage = MenuPage::Settings;
      drawCurrentPage();
      return;
    }
    if (y >= 196 && y < 262 && x >= 20 && x < 218) {
      helpPage = 0;
      currentPage = MenuPage::Help;
      drawCurrentPage();
      return;
    }
    if (y >= 196 && y < 262 && x >= 238 && x < 436) {
      allDefaultsStage = 0;
      currentPage = MenuPage::AllDefaultsConfirm;
      drawCurrentPage();
      return;
    }
    for (uint8_t index = 0; index < CONFIG_COUNT; index++) {
      const int16_t cardX = SETTINGS_GRID_LEFT + index * SETTINGS_COLUMN_STEP;
      const int16_t cardY = 56;
      if (x >= cardX && x < cardX + SETTINGS_BUTTON_WIDTH &&
          y >= cardY && y < cardY + SETTINGS_BUTTON_HEIGHT) {
        selectedConfig = index;
        editingConfig = true;
        editOriginalValue = configItems[index].value;
        currentPage = MenuPage::Edit;
        drawCurrentPage();
        break;
      }
    }
    return;
  }

  if (currentPage == MenuPage::CarOptions) {
    if (y < 50 && x >= 342 && x < 442) {
      currentPage = MenuPage::Cars;
      drawCurrentPage();
      return;
    }
    if (y >= 116 && y < 182 && x >= 20 && x < 218) {
      openCarEdit();
      drawCurrentPage();
    } else if (y >= 116 && y < 182 && x >= 238 && x < 436) {
      openCarCopy(false);
      drawCurrentPage();
    } else if (y >= 196 && y < 262 && x >= 20 && x < 218) {
      openCarCopy(true);
      drawCurrentPage();
    } else if (y >= 196 && y < 262 && x >= 238 && x < 436) {
      currentPage = MenuPage::CarDefaultsConfirm;
      drawCurrentPage();
    }
    return;
  }

  if (currentPage == MenuPage::CarDefaultsConfirm) {
    if (y < 50 && x >= 232 && x < 332) {
      currentPage = MenuPage::CarOptions;
      drawCurrentPage();
    } else if (y < 50 && x >= 342 && x < 442) {
      // name is kept
      resetCarToDefaults(selectedCar);
      loadCar(selectedCar);
      currentPage = MenuPage::CarOptions;
      drawCurrentPage();
    }
    return;
  }

  if (currentPage == MenuPage::CarEdit) {
    if (y < 50 && x >= 232 && x < 332) {
      currentPage = MenuPage::CarOptions;
      drawCurrentPage();
    } else if (y < 50 && x >= 342 && x < 442) {
      strcpy(carNames[selectedCar], carEditOriginalName);
      currentPage = MenuPage::CarOptions;
      drawCurrentPage();
    } else if (y >= 56 && y < 92 && x >= 232 && x < 332) {
      if (carEditCursor > 0) {
        carEditCursor--;
        drawCurrentPage();
      }
    } else if (y >= 56 && y < 92 && x >= 342 && x < 442) {
      if (carEditCursor + 1 < CAR_NAME_LENGTH) {
        carEditCursor++;
        drawCurrentPage();
      }
    } else if (charRowAt(y) >= 0) {
      const uint8_t row = charRowAt(y);
      const char *chars = charRows[row];
      for (uint8_t i = 0; chars[i] != '\0'; i++) {
        const int16_t buttonX = CHAR_PICKER_LEFT + i * CHAR_PICKER_STEP - charScroll[row];
        if (x >= buttonX && x < buttonX + CHAR_PICKER_WIDTH) {
          setCarNameChar(carEditCursor, chars[i]);
          if (carEditCursor + 1 < CAR_NAME_LENGTH) {
            carEditCursor++;
          }
          drawCurrentPage();
          break;
        }
      }
    }
    return;
  }

  if (currentPage == MenuPage::CarCopy) {
    if (x >= 342 && x < 442 && y < 50) {
      currentPage = MenuPage::CarOptions;
      drawCurrentPage();
      return;
    }
    const int8_t target = carAt(x, y);
    if (target >= 0 && target != selectedCar) {
      copyTargetCar = target;
      copyWithName = false;
      copyGotoCar = false;
      currentPage = MenuPage::CarCopyConfirm;
      drawCurrentPage();
    }
    return;
  }

  if (currentPage == MenuPage::CarCopyConfirm) {
    if (y < 50 && x >= 232 && x < 332) {
      currentPage = MenuPage::CarCopy;
      drawCurrentPage();
    } else if (y < 50 && x >= 342 && x < 442) {
      storeCurrentCar();
      const uint8_t sourceCar = copyReverse ? copyTargetCar : selectedCar;
      const uint8_t destCar = copyReverse ? selectedCar : copyTargetCar;
      for (uint8_t i = 0; i < SETTING_COUNT; i++) {
        carValues[destCar][i] = carValues[sourceCar][i];
      }
      carCurveX[destCar] = carCurveX[sourceCar];
      if (copyWithName) {
        strcpy(carNames[destCar], carNames[sourceCar]);
      }
      if (copyReverse) {
        loadCar(selectedCar);
      } else if (copyGotoCar) {
        loadCar(copyTargetCar);
      }
      currentPage = MenuPage::CarOptions;
      drawCurrentPage();
    } else if (y >= 128 && y < 172 && x < 240) {
      copyWithName = !copyWithName;
      drawCurrentPage();
    } else if (y >= 128 && y < 172 && !copyReverse) {
      copyGotoCar = !copyGotoCar;
      drawCurrentPage();
    }
    return;
  }

  if (currentPage == MenuPage::Cars) {
    const MenuPage returnPage = carMenuReturnToSettings ? MenuPage::Settings : MenuPage::Status;
    if (x >= 232 && x < 332 && y < 50) {
      openCarOptions();
      drawCurrentPage();
      return;
    }
    if (x >= 342 && x < 442 && y < 50) {
      pendingCarTapIndex = -1;
      currentPage = returnPage;
      drawCurrentPage();
      return;
    }
    const int8_t tapped = carAt(x, y);
    if (tapped >= 0) {
      if (pendingCarTapIndex == tapped && millis() - pendingCarTapAt < doubleTapMs()) {
        openCarOptions();
      } else {
        selectCar(tapped);
        pendingCarTapIndex = tapped;
        pendingCarTapAt = millis();
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
        selectCar(selectedCar - 1);
      }
    } else {
      if (selectedCar + 1 < CAR_COUNT) {
        selectCar(selectedCar + 1);
      }
    }
    drawCurrentPage();
  }
}

void updateValueFromSlider(int16_t deltaX) {
  const float sliderTravel = 416.0f;
  SettingItem &item = editItem();
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

void updateSettingFromGesture(uint8_t settingIndex, int16_t delta) {
  const float sliderTravel = 416.0f;
  SettingItem &item = settings[settingIndex];
  const int16_t valueRange = item.maximum - item.minimum;
  const int8_t direction = delta > 0 ? 1 : -1;
  if (sliderDirection != 0 && direction != sliderDirection) {
    sliderRemainder = 0.0f;
  }
  sliderDirection = direction;
  sliderRemainder += ((float)delta * valueRange) / sliderTravel;
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

// swipe down = next car, swipe up = previous car, one step per CAR_SWIPE_STEP pixels
void updateCarFromSwipe(int16_t deltaY) {
  carSwipeRemainder += deltaY;
  bool changed = false;
  while (carSwipeRemainder >= CAR_SWIPE_STEP) {
    carSwipeRemainder -= CAR_SWIPE_STEP;
    if (selectedCar + 1 < CAR_COUNT) {
      selectCar(selectedCar + 1);
      changed = true;
    }
  }
  while (carSwipeRemainder <= -CAR_SWIPE_STEP) {
    carSwipeRemainder += CAR_SWIPE_STEP;
    if (selectedCar > 0) {
      selectCar(selectedCar - 1);
      changed = true;
    }
  }
  if (changed) {
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
  SettingItem &item = editItem();
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

void updateCarsScrollAbsolute(int16_t x) {
  const int16_t sliderStart = 36;
  const int16_t sliderEnd = 420;
  const int16_t scroll = map(constrain(x, sliderStart, sliderEnd), sliderStart, sliderEnd, 0, CARS_MAX_SCROLL);
  if (scroll != carsScroll) {
    carsScroll = scroll;
    drawCurrentPage();
  }
}
}

float readSupplyVoltage() {
  uint32_t sum = 0;
  for (uint8_t i = 0; i < 8; i++) {
    sum += analogReadMilliVolts(SUPPLY_ADC_PIN);
  }
  return (sum / 8) * SUPPLY_DIVIDER / 1000.0f;
}

// periodic refresh; on the race screen only the value area is redrawn
void updateSupplyVoltage() {
  if (millis() - supplySampledAt < SUPPLY_SAMPLE_MS) {
    return;
  }
  supplySampledAt = millis();
  const float measured = readSupplyVoltage();
  if (fabsf(measured - supplyVoltage) < 0.01f) {
    return;
  }
  supplyVoltage = measured;
  if (currentPage == MenuPage::Status && !welcomeVisible) {
    screen->fillRect(300, 238, 130, 28, COLOR_PANEL_ALT);
    printSupplyVoltage();
    screen->flush();
  }
}

void displayInit() {
  Wire.begin(TOUCH_SDA, TOUCH_SCL, 300000L);
  analogReadResolution(12);
  supplyVoltage = readSupplyVoltage();
  initCars();
  screen->begin();
  configurePanel();
  drawCurrentPage();
  screen->flush();
}

void displayUpdate() {
  updateSupplyVoltage();
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
    statusGestureAxis = 0;
    statusGestureStartX = x;
    statusGestureStartY = y;
    carSwipeRemainder = 0;
    settingsTouchStartScroll = settingsScroll;
    carsTouchStartScroll = carsScroll;
    for (uint8_t row = 0; row < CHAR_ROW_COUNT; row++) {
      charTouchStartScroll[row] = charScroll[row];
    }
  } else if (touchDown && touchWasDown) {
    touchLastX = x;
    touchLastY = y;
    if (abs(touchLastX - touchStartX) >= 5 || abs(touchLastY - touchStartY) >= 5) {
      touchMoved = true;
    }
    if (!welcomeVisible && currentPage == MenuPage::Status &&
        (abs(touchLastX - touchStartX) >= 5 || abs(touchLastY - touchStartY) >= 5)) {
      if (statusGestureAxis == 0) {
        statusGestureAxis = abs(touchLastX - touchStartX) >= abs(touchLastY - touchStartY) ? 1 : 2;
      }
      if (statusGestureAxis == 1) {
        // horizontal: upper half -> slot 1, lower half -> slot 2
        updateSettingFromGesture(slotSetting[statusGestureStartY < 165 ? 0 : 1], touchLastX - touchStartX);
      } else if (statusGestureStartX < 228) {
        // vertical left: slot 3 (swipe up = increase)
        updateSettingFromGesture(slotSetting[2], touchStartY - touchLastY);
      } else {
        // vertical right: change car
        updateCarFromSwipe(touchLastY - touchStartY);
      }
      touchStartX = touchLastX;
      touchStartY = touchLastY;
      touchSliderActive = true;
    } else if (!welcomeVisible && (currentPage == MenuPage::Settings || currentPage == MenuPage::AssignSlot) &&
        abs(touchLastX - touchStartX) >= abs(touchLastY - touchStartY) &&
        abs(touchLastX - touchStartX) >= 5) {
      updateSettingsScroll(touchLastX - touchStartX);
      settingsDragging = true;
      touchSliderActive = true;
    } else if (!welcomeVisible && currentPage == MenuPage::CarEdit &&
        charRowAt(touchStartY) >= 0 &&
        abs(touchLastX - touchStartX) >= abs(touchLastY - touchStartY) &&
        abs(touchLastX - touchStartX) >= 5) {
      const uint8_t row = charRowAt(touchStartY);
      charScroll[row] = constrain((int16_t)(charTouchStartScroll[row] - (touchLastX - touchStartX)), (int16_t)0, charRowMaxScroll(row));
      touchSliderActive = true;
      drawCurrentPage();
    } else if (!welcomeVisible && (currentPage == MenuPage::Cars || currentPage == MenuPage::CarCopy) &&
        abs(touchLastX - touchStartX) >= abs(touchLastY - touchStartY) &&
        abs(touchLastX - touchStartX) >= 5) {
      // upper card row: relative finger-following scroll; lower row: absolute position
      if (touchStartY < CAR_GRID_TOP + CAR_ROW_STEP) {
        updateCarsScroll(touchLastX - touchStartX);
      } else {
        updateCarsScrollAbsolute(touchLastX);
      }
      carsDragging = true;
      touchSliderActive = true;
    } else if (!welcomeVisible && currentPage == MenuPage::Edit &&
        strcmp(editItem().name, "CURVE") == 0 && touchMoved) {
      updateCurveFromGesture(touchLastX - touchStartX, touchLastY - touchStartY);
      touchStartX = touchLastX;
      touchStartY = touchLastY;
      touchSliderActive = true;
    } else if (!welcomeVisible && currentPage == MenuPage::Edit &&
        abs(touchLastX - touchStartX) >= abs(touchLastY - touchStartY) &&
        abs(touchLastX - touchStartX) >= 5) {
      if (touchStartY < EDIT_SPLIT_Y) {
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
      carsScroll = constrain((int16_t)((carsScroll + CAR_COLUMN_STEP / 2) / CAR_COLUMN_STEP) * CAR_COLUMN_STEP,
                             (int16_t)0, CARS_MAX_SCROLL);
      drawCurrentPage();
    } else if (touchMoved && !touchSliderActive) {
      handleSwipe(touchStartX, touchStartY, touchLastX, touchLastY);
    } else if (!touchMoved) {
      handleTouch(touchLastX, touchLastY);
    }
    pendingSaveAt = millis() + PERSIST_DELAY_MS;
  }
  if (pendingSaveAt != 0 && !touchDown && (int32_t)(millis() - pendingSaveAt) >= 0) {
    persistProfiles();
    pendingSaveAt = 0;
  }
  if (pendingCarTapIndex >= 0 && currentPage == MenuPage::Cars && !touchDown &&
      millis() - pendingCarTapAt >= doubleTapMs()) {
    pendingCarTapIndex = -1;
    currentPage = carMenuReturnToSettings ? MenuPage::Settings : MenuPage::Status;
    drawCurrentPage();
  }
  touchWasDown = touchDown;
}