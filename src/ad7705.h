// Copyright (c) 2026 Ashoka Munasinghe, www.electronics.lk. MIT License, see LICENSE.
// SPDX-License-Identifier: MIT

#pragma once
#include <Arduino.h>

// Minimal AD7705 driver: two channels, self-calibration, averaged reads.
class AD7705 {
public:
  typedef void (*Hook)();

  AD7705(uint8_t cs, int8_t drdy, int8_t rst) : cs_(cs), drdy_(drdy), rst_(rst) {}

  // Resets the chip and writes the clock register. False if the chip does not answer.
  bool begin(uint8_t clockReg, float vref);
  // Sets gain/polarity for a channel and runs a self-calibration.
  bool configure(uint8_t ch, uint8_t gain, bool unipolar, bool buffered);
  // Averaged input voltage (at the ADC pins) of n conversions, NAN on timeout.
  float readVolts(uint8_t ch, uint8_t n);
  // Called while waiting for a conversion (used to poll the button).
  void setHook(Hook h) { hook_ = h; }
  // Clock register value read back by begin() (diagnostics).
  uint8_t probe() const { return probe_; }

private:
  bool readRaw(uint8_t ch, uint16_t &code);
  bool waitReady(uint8_t ch, uint16_t timeoutMs);
  void writeReg(uint8_t comm, uint8_t val);
  uint8_t read8(uint8_t comm);
  uint16_t read16(uint8_t comm);

  uint8_t cs_;
  int8_t drdy_, rst_;
  float vref_ = 2.5f;
  uint8_t setup_[2] = {0, 0};
  float lsb_[2] = {0, 0};
  bool bipolar_[2] = {false, false};
  int8_t active_ = -1;
  uint8_t probe_ = 0;
  Hook hook_ = nullptr;
};
