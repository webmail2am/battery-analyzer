// Copyright (c) 2026 Ashoka Munasinghe. MIT License, see LICENSE.
// SPDX-License-Identifier: MIT

#pragma once
#include <Arduino.h>

// User settings kept on the chip across power-off (set from the PC app or serial commands).
struct Settings {
  uint8_t chem;        // Chem (battery.h)
  uint16_t cutoffMv;   // end-of-discharge voltage
};

extern Settings cfg;

void settingsLoad();   // defaults if nothing valid is stored
bool settingsSave();   // false if the stored copy doesn't read back correctly
