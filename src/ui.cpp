// Copyright (c) 2026 Ashoka Munasinghe, www.electronics.lk. MIT License, see LICENSE.
// SPDX-License-Identifier: MIT

#include <Wire.h>
#include <SSD1306Ascii.h>
#include <SSD1306AsciiWire.h>
#include "config.h"
#include "ui.h"

// Text-only driver: writes straight to the display, no frame buffer in RAM
static SSD1306AsciiWire oled;

static uint32_t lastHash = 0;
static int8_t dimmed = -1;

static uint32_t hashScreen(const Screen &s) {
  const uint8_t *p = (const uint8_t *)&s;
  uint32_t h = 2166136261UL;
  for (uint16_t i = 0; i < sizeof(Screen); i++) h = (h ^ p[i]) * 16777619UL;
  return h;
}

// Prints text at (col, row) and blanks the rest of the row(s)
static void printAt(uint8_t col, uint8_t row, const char *text) {
  if (col > 0) oled.clear(0, col - 1, row, row + oled.fontRows() - 1);
  oled.setCursor(col, row);
  oled.print(text);
  oled.clearToEOL();
}

static bool i2cPresent(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

void uiBegin() {
  if (PIN_OLED_RST >= 0) {
    pinMode(PIN_OLED_RST, OUTPUT);
    digitalWrite(PIN_OLED_RST, LOW);
    delay(10);
    digitalWrite(PIN_OLED_RST, HIGH);
    delay(10);
  }
  Wire.begin();
  Wire.setClock(OLED_I2C_HZ);

  uint8_t addr = OLED_I2C_ADDR;
  if (!addr) addr = i2cPresent(0x3C) ? 0x3C : i2cPresent(0x3D) ? 0x3D : 0;
  if (addr) {
    Serial.print(F("# OLED at 0x"));
    Serial.println(addr, HEX);
  } else {
    Serial.println(F("# OLED not found on I2C (check SDA=A4, SCL=A5, pull-ups)"));
    addr = 0x3C;
  }
  oled.begin(&OLED_TYPE, addr);
  oled.clear();
  uiDim(false);
}

void uiDim(bool dim) {
  if (dimmed == (int8_t)dim) return;
  dimmed = dim;
  oled.setContrast(dim ? OLED_DIM_CONTRAST : OLED_CONTRAST);
}

bool uiShow(const Screen &s, bool force) {
  uint32_t h = hashScreen(s);
  if (!force && h == lastHash) return false;
  lastHash = h;
  const uint8_t w = oled.displayWidth();

  // rows 0-1 (yellow): title, centred; double size when it fits (text never wraps)
  oled.setFont(System5x7);
  uint8_t len = strlen(s.title);
  int16_t x;
  if (len <= 10) {
    oled.set2X();
    x = (w - (int16_t)len * 12) / 2;
    printAt(x < 0 ? 0 : x, 0, s.title);
  } else {
    oled.set1X();
    x = (w - (int16_t)len * 6) / 2;
    oled.clear(0, w - 1, 0, 0);
    printAt(x < 0 ? 0 : x, 1, s.title);
  }

  // rows 2-3: big value, centred
  oled.set2X();
  x = (w - (int16_t)strlen(s.big) * 12) / 2;
  printAt(x < 0 ? 0 : x, 2, s.big);

  // rows 4-7: text lines
  oled.set1X();
  for (uint8_t i = 0; i < 4; i++) printAt(0, 4 + i, s.line[i]);
  return true;
}
