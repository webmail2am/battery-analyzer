// Copyright (c) 2026 Ashoka Munasinghe, www.electronics.lk. MIT License, see LICENSE.
// SPDX-License-Identifier: MIT

#pragma once
#include <Arduino.h>

// ============================================================================
//  Pins  (hardware SPI is fixed: D13 = SCK, D11 = MOSI, D12 = MISO)
// ============================================================================
// OLED on I2C: SDA = A4, SCL = A5 (fixed). D9 is free; D10 stays an SPI output (no connection).
#define PIN_OLED_RST    -1   // 4-pin module (own reset); 8 for a converted 7-pin module with RES on D8

#define PIN_ADC_CS       6
#define PIN_ADC_DRDY     5   // set to -1 to poll the DRDY bit over SPI instead
#define PIN_ADC_RST     -1   // AD7705 RST tied to 3.3 V; chip is reset over SPI instead

// Three push buttons, each to GND (internal pull-ups)
#define PIN_BTN_START    2   // start selected test / back; hold = abort
#define PIN_BTN_MODE     3   // cycle QUICK -> FULL -> CHG+DIS -> OUTPUT
#define PIN_BTN_RINT     4   // internal-resistance test / back; hold = abort

#define PIN_LOAD_EN     A1   // HIGH = load MOSFET on
#define PIN_CHG_EN      A2   // HIGH = TP4056 supply switched on
#define PIN_CHG_STDBY   A3   // TP4056 STDBY pin (LOW = charge complete)
#define PIN_CHG_CHRG     7   // TP4056 CHRG pin  (LOW = charging); A4 is I2C SDA

// ============================================================================
//  OLED 0.96" 128x64 I2C (SSD1306; top 16 rows yellow, rest blue)
//    Adafruit128x64 : SSD1306 0.96"      SH1106_128x64 : SH1106 1.3"
// ============================================================================
#define OLED_TYPE           Adafruit128x64
#define OLED_I2C_ADDR       0        // 0 = auto-detect 0x3C / 0x3D
#define OLED_I2C_HZ         100000L  // 400 kHz isn't reachable with a 4 MHz CPU
#define OLED_CONTRAST       0xCF
#define OLED_DIM_CONTRAST   0x08     // during long tests, against burn-in
#define OLED_DIM_AFTER_MS   60000UL  // dim this long after the test phase starts
#define DISP_REFRESH_MS     500      // min time between redraws
#define SPLASH_MS           2000     // power-up screen with battery type and cutoff

// ============================================================================
//  AD7705
// ============================================================================
#define ADC_VREF        2.500f   // reference on the module (REF pins)
#define ADC_CLOCK_REG   0x0C     // 4.9152 MHz crystal, /2, 50 Hz output (0x0D = 60 Hz)
                                 // use 0x04 if your module has a 2.4576 MHz crystal
// The AD7705 module is used as it is: each input has a 10k/10k divider (/2) and AIN1-/AIN2-
// are tied to GND on the module.
#define CH_SHUNT        0        // AIN1+ = cell minus (shunt low end), AIN1- = GND (bipolar)
#define CH_VBAT         1        // AIN2+ = BAT+ sense wire, AIN2- = GND
#define SHUNT_GAIN      64       // +-2.5 V / 64 = +-39 mV at the chip = +-78 mV at the shunt
#define SHUNT_DIV       2.000f   // module divider on AIN1
#define SHUNT_SIGN      -1       // cell minus is below GND while discharging
#define VBAT_GAIN       1

// ============================================================================
//  Analog front end & calibration
// ============================================================================
#define SHUNT_OHMS      0.0200f  // one 0.02 ohm in the cell minus lead: load + electronics + charging
#define VDIV_RATIO      2.000f   // module's 10k/10k divider on AIN2
#define V_CAL           1.000f   // trim against a good multimeter: serial command "v <volts>"
#define I_CAL           1.000f   // serial commands "l 1" then "i <amps>" (meter in the cell lead)
#define CAL_LOAD_MAX_MS 60000UL  // load switched on by "l 1" turns off after this

// ============================================================================
//  Test parameters
// ============================================================================
// The end-of-discharge (cutoff) voltage and the battery chemistry are settings stored on
// the chip: change them from the PC app or with the "c" / "u" serial commands.
#define CUTOFF_COUNT        5        // consecutive samples below cutoff
#define NO_BATT_V           1.00f    // below this: no battery connected
#define MIN_LOAD_A          0.050f   // load adds less than this -> load fault
#define MAX_CURRENT_A       2.50f    // -50 mV at the shunt = -25 mV at the chip (limit -30 mV)

#define IR_REST_MS          1000     // rest before open-circuit reading
#define IR_LOAD_MS          1000     // load-on time before loaded reading

// QUICK: drain until the estimated resting voltage (V + I x Rint) has dropped by
// QUICK_TARGET_MV, rest until the voltage settles, then estimate capacity from the SoC change.
#define QUICK_TARGET_MV     60       // voltage drop to aim for
#define QUICK_MIN_DV_MV     40       // rested drop below this -> one more drain round
#define QUICK_MIN_S         60UL     // shortest drain (polarization alone can't end it)
#define QUICK_MAX_S         1800UL   // longest drain per round (flat part of the curve)
#define QUICK_REST_MIN_S    180UL    // rest at least this long
#define QUICK_REST_MAX_S    900UL    // ... and at most this long
#define QUICK_SETTLE_MV     1        // settled when the voltage rises less than this per minute
#define QUICK_MIN_DSOC      3.0f     // below this SoC change the estimate is unreliable

#define CHG_REST_S          600UL    // rest between charge and discharge
#define CHG_TIMEOUT_S       (6UL * 3600UL)
#define DIS_TIMEOUT_S       (20UL * 3600UL)
#define RECHARGE_AFTER_TEST 1        // CHG+DIS mode: recharge after discharge

// ============================================================================
//  UI / logging
// ============================================================================
#define LONG_PRESS_MS       1500
#define DEBOUNCE_MS         30
#if F_CPU <= 4000000L
#define SERIAL_BAUD         38400    // 57600 is 3.5 % off at 4 MHz
#else
#define SERIAL_BAUD         57600
#endif
#define TELEM_MS            1000     // "D," telemetry line interval for the PC app (0 = off)
