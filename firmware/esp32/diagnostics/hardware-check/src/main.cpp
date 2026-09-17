#include <Arduino.h>
#include <Keypad.h>
#include <LiquidCrystal_I2C.h>
#include <Wire.h>

#include "../../../hardware_profile.h"

constexpr byte ROWS = 4;
constexpr byte COLS = 4;
char keyMap[ROWS][COLS] = {
    {'1', '2', '3', 'A'},
    {'4', '5', '6', 'B'},
    {'7', '8', '9', 'C'},
    {'*', '0', '#', 'D'},
};
byte rowPins[ROWS] = {
    TMBOX_ROW_PINS[0], TMBOX_ROW_PINS[1], TMBOX_ROW_PINS[2], TMBOX_ROW_PINS[3]};
byte colPins[COLS] = {
    TMBOX_COL_PINS[0], TMBOX_COL_PINS[1], TMBOX_COL_PINS[2], TMBOX_COL_PINS[3]};
Keypad keypad = Keypad(makeKeymap(keyMap), rowPins, colPins, ROWS, COLS);
LiquidCrystal_I2C* lcd = nullptr;
uint8_t activeLCDAddress = 0;

bool addressResponds(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

void printPinMap() {
  Serial.println();
  Serial.println("TrainMeet TMBox hardware check");
  Serial.printf("Profile: %s\n", TMBOX_MODEL_NAME);
  Serial.printf("LCD SDA=GPIO%u SCL=GPIO%u configured=0x%02X\n",
                TMBOX_LCD_SDA, TMBOX_LCD_SCL, TMBOX_LCD_ADDRESS);
  for (byte index = 0; index < ROWS; ++index) {
    Serial.printf("R%u=GPIO%u%s", index + 1, rowPins[index], index == ROWS - 1 ? "\n" : "  ");
  }
  for (byte index = 0; index < COLS; ++index) {
    Serial.printf("C%u=GPIO%u%s", index + 1, colPins[index], index == COLS - 1 ? "\n" : "  ");
  }
}

void scanI2C() {
  Serial.println("Scanning I2C addresses...");
  byte found = 0;
  for (uint8_t address = 1; address < 127; ++address) {
    if (addressResponds(address)) {
      Serial.printf("  Found device at 0x%02X\n", address);
      found++;
    }
  }
  if (found == 0) {
    Serial.println("  No I2C device found. Check power, GND, SDA, SCL and level shifter.");
  }
}

uint8_t chooseLCDAddress() {
  if (addressResponds(TMBOX_LCD_ADDRESS)) return TMBOX_LCD_ADDRESS;
  if (addressResponds(0x27)) return 0x27;
  if (addressResponds(0x3F)) return 0x3F;
  return 0;
}

void showLines(const String& first, const String& second) {
  if (lcd == nullptr) return;
  String line1 = first.substring(0, 16);
  String line2 = second.substring(0, 16);
  while (line1.length() < 16) line1 += ' ';
  while (line2.length() < 16) line2 += ' ';
  lcd->setCursor(0, 0);
  lcd->print(line1);
  lcd->setCursor(0, 1);
  lcd->print(line2);
}

bool keyPosition(char key, byte& row, byte& column) {
  for (byte rowIndex = 0; rowIndex < ROWS; ++rowIndex) {
    for (byte columnIndex = 0; columnIndex < COLS; ++columnIndex) {
      if (keyMap[rowIndex][columnIndex] == key) {
        row = rowIndex + 1;
        column = columnIndex + 1;
        return true;
      }
    }
  }
  return false;
}

void setup() {
  Serial.begin(115200);
  delay(500);
  printPinMap();
  Wire.begin(TMBOX_LCD_SDA, TMBOX_LCD_SCL);
  scanI2C();

  activeLCDAddress = chooseLCDAddress();
  if (activeLCDAddress != 0) {
    lcd = new LiquidCrystal_I2C(activeLCDAddress, 16, 2);
    lcd->init();
    lcd->backlight();
    char addressText[17];
    snprintf(addressText, sizeof(addressText), "LCD OK 0x%02X", activeLCDAddress);
    showLines(addressText, "TRYCK ALLA KNAPP");
    Serial.printf("LCD initialized at 0x%02X. Adjust the contrast potentiometer if needed.\n",
                  activeLCDAddress);
  } else {
    Serial.println("LCD was not initialized because neither 0x27 nor 0x3F responded.");
  }

  Serial.println("Press all 16 keys. The detected key, matrix row and column are printed here.");
}

void loop() {
  const char key = keypad.getKey();
  if (!key) {
    delay(5);
    return;
  }

  byte row = 0;
  byte column = 0;
  keyPosition(key, row, column);
  Serial.printf("Key '%c'  R%u/C%u  GPIO row=%u column=%u\n",
                key, row, column, rowPins[row - 1], colPins[column - 1]);

  String first = "KNAPP: ";
  first += key;
  String second = "R" + String(row) + " C" + String(column);
  showLines(first, second);
}
