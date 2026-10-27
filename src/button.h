// Copyright (c) 2026 Ashoka Munasinghe, www.electronics.lk. MIT License, see LICENSE.
// SPDX-License-Identifier: MIT

#pragma once
#include <Arduino.h>
#include "config.h"

// Debounced push button (active LOW) with short / long press events.
// update() must be called often; it is also called while waiting on the ADC and display.
class Button {
public:
  enum Event : uint8_t { NONE, SHORT, LONG };

  void begin(uint8_t pin) {
    pin_ = pin;
    pinMode(pin_, INPUT_PULLUP);
  }

  void update() {
    bool raw = digitalRead(pin_) == LOW;
    uint32_t now = millis();
    if (raw != lastRaw_) {
      lastRaw_ = raw;
      tChange_ = now;
    }
    if (now - tChange_ >= DEBOUNCE_MS && raw != pressed_) {
      pressed_ = raw;
      if (pressed_) {
        tPress_ = now;
        longFired_ = false;
      } else if (!longFired_) {
        event_ = SHORT;
      }
    }
    if (pressed_ && !longFired_ && now - tPress_ >= LONG_PRESS_MS) {
      longFired_ = true;
      event_ = LONG;
    }
  }

  Event take() {
    Event e = event_;
    event_ = NONE;
    return e;
  }

private:
  uint8_t pin_ = 0;
  bool lastRaw_ = false, pressed_ = false, longFired_ = false;
  uint32_t tChange_ = 0, tPress_ = 0;
  Event event_ = NONE;
};
