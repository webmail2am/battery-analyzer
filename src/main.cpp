// Copyright (c) 2026 Ashoka Munasinghe, www.electronics.lk. MIT License, see LICENSE.
// SPDX-License-Identifier: MIT

// Lithium battery analyzer
//   Pro Mini 328 + AD7705 (16-bit ADC) + 0.96" I2C OLED (SSD1306)
//
//   Idle  : live battery voltage / current, OCV-based SoC
//   MODE  : cycle QUICK -> FULL -> CHG+DIS -> OUTPUT
//   START : start the selected test
//             QUICK   - drain until the voltage drops ~60 mV, rest, capacity from SoC change
//             FULL    - discharge to cutoff, count mAh / Wh
//             CHG+DIS - charge to full, rest, discharge to cutoff
//             OUTPUT  - power a device on the load terminals, log its current / power / energy
//   RINT  : internal resistance test (open-circuit vs loaded voltage)
//   Hold START or RINT during a test to abort; START or RINT leaves the result screen.
//   Serial commands (PC app, calibration): send "?" for help.

#include <Arduino.h>
#include <SPI.h>
#include "config.h"
#include "ad7705.h"
#include "button.h"
#include "battery.h"
#include "ui.h"
#include "settings.h"

enum Mode : uint8_t { MODE_QUICK, MODE_FULL, MODE_CHGDIS, MODE_OUTPUT, MODE_COUNT };
enum State : uint8_t {
  ST_IDLE,
  ST_QUICK_DRAIN,
  ST_QUICK_REST,
  ST_CHARGE,
  ST_CHG_REST,
  ST_DISCHARGE,
  ST_RESULT,
  ST_ERROR,
  ST_OUTPUT,           // device on the load terminals is powered
};

static const char *const MODE_NAME[] = {"QUICK", "FULL", "CHG+DIS", "OUTPUT"};

static AD7705 adc(PIN_ADC_CS, PIN_ADC_DRDY, PIN_ADC_RST);
static Button bStart, bMode, bRint;

static State state = ST_IDLE;
static Mode mode = MODE_FULL;

static float vBat = 0, iBat = 0;
static float iIdle = 0;   // cell current with the load off (the electronics' own draw)
static float lastRint = NAN, lastOcv = NAN;
static float vCal = V_CAL, iCal = I_CAL;   // trimmed at runtime by serial commands

static float cutoffV() { return cfg.cutoffMv * 0.001f; }
static float soc(float v) { return socFromOcv(cfg.chem, v); }

// Everything about the running / last test
static struct {
  Mode mode;
  uint32_t tPhase;     // start of the current phase (ms)
  uint32_t tLast;      // last integration step (ms)
  uint32_t disMs;      // discharge duration (ms)
  float mAh, mWh;
  float ocv1, ocv2, rint;
  float capacity;      // quick mode estimate (mAh), NAN if unreliable
  float iMin, iMax;    // output mode: device current range (A)
  float ocvTarget;     // quick: drain until the resting-voltage estimate reaches this
  float vRef;          // quick rest: voltage a minute ago (settling check)
  uint32_t tRef;       // quick rest: time of vRef
  uint32_t drainMs;    // quick: drain time of finished rounds
  uint8_t round;       // quick: drain round (1 or 2)
  uint8_t lowCnt, okCnt, faultCnt;
  bool recharging;
  char reason[17];
} T;

static uint32_t lastDraw = 0, lastLog = 0;
static uint16_t telemMs = TELEM_MS;
// button presses requested by serial commands (PC app)
static Button::Event cmdStart = Button::NONE, cmdRint = Button::NONE;

// Current shown and logged: in OUTPUT mode only the device's share (minus the analyzer's own draw)
static float iShown() { return state == ST_OUTPUT ? iBat - iIdle : iBat; }

static bool isRunning() { return state != ST_IDLE && state != ST_RESULT && state != ST_ERROR; }

// Result line for the PC app: R,mode,mAh,mWh,seconds,rint_mOhm,estimate_mAh,reason
static void printResult() {
  Serial.print(F("R,"));
  Serial.print(T.mode);            Serial.print(',');
  Serial.print(T.mAh, 1);          Serial.print(',');
  Serial.print(T.mWh, 1);          Serial.print(',');
  Serial.print(T.disMs / 1000UL);  Serial.print(',');
  Serial.print(T.rint, 1);         Serial.print(',');
  Serial.print(T.capacity, 0);     Serial.print(',');
  Serial.println(T.reason);
  if (T.mode == MODE_OUTPUT) {
    // Output summary for the PC app: O,avg_A,min_A,max_A
    float hours = T.disMs / 3600000.0f;
    Serial.print(F("O,"));
    Serial.print(hours > 0 ? T.mAh / 1000.0f / hours : 0.0f, 4); Serial.print(',');
    Serial.print(T.iMin, 4);                                   Serial.print(',');
    Serial.println(T.iMax, 4);
  }
}
static bool forceDraw = true;

// calibration helpers (serial)
static bool calLoad = false, calMonitor = false;
static uint32_t calLoadT0 = 0, lastMon = 0;

// ---------------------------------------------------------------- hardware --

static void loadOn()     { digitalWrite(PIN_LOAD_EN, HIGH); }
static void loadOff()    { digitalWrite(PIN_LOAD_EN, LOW); calLoad = false; }
static void chargerOn()  { digitalWrite(PIN_CHG_EN, HIGH); }
static void chargerOff() { digitalWrite(PIN_CHG_EN, LOW); }
static void allOff()     { loadOff(); chargerOff(); }

static bool chgFull()   { return digitalRead(PIN_CHG_STDBY) == LOW; }
static bool chgActive() { return digitalRead(PIN_CHG_CHRG) == LOW; }

static void pollInputs() {
  bStart.update();
  bMode.update();
  bRint.update();
}

// Cell voltage and current (positive = discharge, negative = charging).
// The shunt is in the cell minus lead and the divider is referenced to GND, which sits
// I x Rs above the cell minus, so the shunt drop is added back to the voltage.
static bool readVI(uint8_t n, float &v, float &i) {
  float vd = adc.readVolts(CH_VBAT, n);
  i = SHUNT_SIGN * adc.readVolts(CH_SHUNT, n) * SHUNT_DIV / SHUNT_OHMS * iCal;
  if (isnan(vd) || isnan(i)) return false;
  v = vd * VDIV_RATIO * vCal + i * SHUNT_OHMS;
  return true;
}

static void waitMs(uint32_t ms) {
  uint32_t t0 = millis();
  while (millis() - t0 < ms) pollInputs();
}

// Internal resistance: voltage step / current step when the load switches on.
// The electronics' own draw is in both readings and cancels out.
// Returns false on ADC failure; rint is NAN if the load added no current.
static bool measureIR(float &voc, float &rint) {
  allOff();
  waitMs(IR_REST_MS);
  float i0, vl, il;
  if (!readVI(4, voc, i0)) return false;
  iIdle = i0;
  loadOn();
  waitMs(IR_LOAD_MS);
  bool ok = readVI(2, vl, il);
  loadOff();
  if (!ok) return false;
  float di = il - i0;
  rint = (di < MIN_LOAD_A) ? NAN : (voc - vl) / di * 1000.0f;
  return true;
}

// --------------------------------------------------------------- formatting --

#define LINE(i, fmt, ...) snprintf_P(s.line[i], sizeof(s.line[i]), PSTR(fmt), ##__VA_ARGS__)

static char *f2s(char *buf, float v, uint8_t dec) {
  dtostrf(v, 0, dec, buf);
  return buf;
}

static char *fmtTime(char *buf, uint32_t sec) {
  uint16_t h = sec / 3600;
  uint8_t m = (sec / 60) % 60, s = sec % 60;
  if (h) sprintf_P(buf, PSTR("%u:%02u:%02u"), h, m, s);
  else sprintf_P(buf, PSTR("%u:%02u"), m, s);
  return buf;
}

static char *fmtRint(char *buf, float r) {
  if (isnan(r)) strcpy_P(buf, PSTR("--"));
  else strcat_P(f2s(buf, r, r < 100 ? 1 : 0), PSTR("mOhm"));
  return buf;
}

static void setReason(const char *pstr) {
  strncpy_P(T.reason, pstr, sizeof(T.reason) - 1);
  T.reason[sizeof(T.reason) - 1] = 0;
}

// ------------------------------------------------------------- transitions --

static void enter(State s) {
  state = s;
  T.tPhase = millis();
  forceDraw = true;
}

static void fail(const char *pstrReason) {
  allOff();
  setReason(pstrReason);
  T.recharging = false;
  Serial.print(F("# ERROR: "));
  Serial.println(T.reason);
  enter(ST_ERROR);
}

static void startDischarge() {
  T.mAh = T.mWh = 0;
  T.lowCnt = 0;
  T.round = 1;
  T.ocvTarget = T.ocv1 - QUICK_TARGET_MV * 0.001f;
  loadOn();
  enter(T.mode == MODE_QUICK ? ST_QUICK_DRAIN : ST_DISCHARGE);
  T.tLast = T.tPhase;
}

static void finishDischarge(const char *pstrReason) {
  loadOff();
  T.disMs = T.drainMs + (millis() - T.tPhase);
  setReason(pstrReason);
  T.recharging = (T.mode == MODE_CHGDIS) && RECHARGE_AFTER_TEST;
  T.okCnt = 0;
  if (T.recharging) chargerOn();
  enter(ST_RESULT);
  printResult();
}

static void startTest(Mode m) {
  memset(&T, 0, sizeof(T));
  T.mode = m;
  T.rint = T.ocv1 = T.ocv2 = T.capacity = NAN;
  if (vBat < NO_BATT_V) return fail(PSTR("No battery"));

  if (m == MODE_OUTPUT) {
    if (vBat < cutoffV() + 0.05f) return fail(PSTR("Battery low"));
    // baseline: the analyzer's own current with the output off
    allOff();
    waitMs(IR_REST_MS);
    float v;
    if (!readVI(4, v, iIdle)) return fail(PSTR("ADC timeout"));
    T.iMin = 1e9f;
    T.iMax = -1e9f;
    loadOn();
    enter(ST_OUTPUT);
    T.tLast = T.tPhase;
    return;
  }
  if (m == MODE_CHGDIS) {
    if (!chemCanCharge(cfg.chem)) return fail(PSTR("TP4056: Li-ion"));
    chargerOn();
    enter(ST_CHARGE);
    return;
  }
  if (vBat < cutoffV() + 0.05f) return fail(PSTR("Already empty"));
  if (!measureIR(T.ocv1, T.rint)) return fail(PSTR("ADC timeout"));
  if (isnan(T.rint)) return fail(PSTR("No load current"));
  lastRint = T.rint;
  startDischarge();
}

// Common discharge step. Returns true when the discharge must stop.
static bool dischargeStep(uint32_t now) {
  float dtH = (now - T.tLast) / 3600000.0f;
  T.tLast = now;
  T.mAh += iBat * 1000.0f * dtH;
  T.mWh += iBat * vBat * 1000.0f * dtH;

  if (vBat < NO_BATT_V) { finishDischarge(PSTR("Batt removed")); return true; }
  if (iBat > MAX_CURRENT_A) { finishDischarge(PSTR("Overcurrent")); return true; }
  if (now - T.tPhase > 3000 && iBat - iIdle < MIN_LOAD_A) {
    finishDischarge(PSTR("No load current"));
    return true;
  }
  T.lowCnt = (vBat < cutoffV()) ? T.lowCnt + 1 : 0;
  if (T.lowCnt >= CUTOFF_COUNT) { finishDischarge(PSTR("Cutoff reached")); return true; }
  if (now - T.tPhase > DIS_TIMEOUT_S * 1000UL) { finishDischarge(PSTR("Timeout")); return true; }
  return false;
}

// ------------------------------------------------------------------- states --

static void runRintTest() {
  Screen s;
  memset(&s, 0, sizeof(s));
  strcpy_P(s.title, PSTR("RINT TEST"));
  strcpy_P(s.big, PSTR("..."));
  LINE(0, "Measuring...");
  uiShow(s, true);
  if (!measureIR(lastOcv, lastRint)) return fail(PSTR("ADC timeout"));
  if (isnan(lastRint)) return fail(PSTR("No load current"));
  // Rint line for the PC app: K,rint_mOhm,ocv
  Serial.print(F("K,"));
  Serial.print(lastRint, 1);
  Serial.print(',');
  Serial.println(lastOcv, 4);
  forceDraw = true;
}

// Resting-voltage estimate while the load is on: add back the drop over Rint.
// Slow polarization isn't included, so the drain may end a little early.
static float restEstimate() { return vBat + (iBat - iIdle) * T.rint * 0.001f; }

// End of a quick rest: another drain round if the voltage moved too little, else the result.
static void quickRested() {
  float v, i;
  if (!readVI(4, v, i)) return fail(PSTR("ADC timeout"));
  float dv = (T.ocv1 - v) * 1000.0f;   // mV
  if (dv < QUICK_MIN_DV_MV && T.round < 2 && v > cutoffV() + 0.1f) {
    T.round++;
    T.ocvTarget = v - (QUICK_TARGET_MV - dv) * 0.001f;
    loadOn();
    enter(ST_QUICK_DRAIN);
    T.tLast = T.tPhase;
    return;
  }
  T.ocv2 = v;
  T.disMs = T.drainMs;
  float d = soc(T.ocv1) - soc(T.ocv2);
  T.capacity = (d >= QUICK_MIN_DSOC) ? T.mAh / (d / 100.0f) : NAN;
  setReason(isnan(T.capacity) ? PSTR("dSoC too small")
            : dv >= 50 ? PSTR("Approx +-5%")
            : dv >= 30 ? PSTR("Approx +-10%") : PSTR("Low confidence"));
  enter(ST_RESULT);
  printResult();
}

static void runState(Button::Event evStart, Button::Event evMode, Button::Event evRint, uint32_t now) {
  uint32_t elapsed = now - T.tPhase;
  bool running = state != ST_IDLE && state != ST_RESULT && state != ST_ERROR;
  bool back = evStart == Button::SHORT || evRint == Button::SHORT;

  // OUTPUT toggles with a short press; tests need a hold to abort (no accidental stops)
  if (state == ST_OUTPUT && back) {
    finishDischarge(PSTR("Stopped"));
    return;
  }
  if (running && (evStart == Button::LONG || evRint == Button::LONG)) {
    if (state == ST_OUTPUT) finishDischarge(PSTR("Stopped"));
    else if (state == ST_QUICK_DRAIN || state == ST_DISCHARGE) finishDischarge(PSTR("Aborted"));
    else fail(PSTR("Aborted"));
    return;
  }

  switch (state) {
    case ST_IDLE:
      if (calLoad && now - calLoadT0 > CAL_LOAD_MAX_MS) {
        loadOff();
        Serial.println(F("# load off (timeout)"));
      }
      if (evMode == Button::SHORT) {
        mode = (Mode)((mode + 1) % MODE_COUNT);
        forceDraw = true;
      } else if (evRint == Button::SHORT && vBat >= NO_BATT_V) {
        runRintTest();
      } else if (evStart == Button::SHORT) {
        startTest(mode);
      }
      break;

    case ST_QUICK_DRAIN:
      if (dischargeStep(now)) {
        // Cutoff hit during the quick drain: the cell was nearly empty
        T.ocv2 = NAN;
        return;
      }
      if ((elapsed >= QUICK_MIN_S * 1000UL && restEstimate() <= T.ocvTarget) ||
          elapsed >= QUICK_MAX_S * 1000UL) {
        loadOff();
        T.drainMs += elapsed;
        T.vRef = NAN;
        enter(ST_QUICK_REST);
      }
      break;

    case ST_QUICK_REST:
      // settled once the voltage rises less than QUICK_SETTLE_MV per minute
      if (isnan(T.vRef)) {
        T.vRef = vBat;
        T.tRef = now;
      } else if (now - T.tRef >= 60000UL) {
        bool settled = vBat - T.vRef < QUICK_SETTLE_MV * 0.001f;
        T.vRef = vBat;
        T.tRef = now;
        if ((settled && elapsed >= QUICK_REST_MIN_S * 1000UL) || elapsed >= QUICK_REST_MAX_S * 1000UL)
          quickRested();
      }
      break;

    case ST_CHARGE: {
      bool full = chgFull(), active = chgActive();
      T.okCnt = (full && !active) ? T.okCnt + 1 : 0;
      T.faultCnt = (!full && !active) ? T.faultCnt + 1 : 0;
      if (elapsed > 20000UL && T.okCnt >= 10) {
        chargerOff();
        enter(ST_CHG_REST);
      } else if (T.faultCnt >= 50) {
        fail(PSTR("Charger fault"));
      } else if (elapsed >= CHG_TIMEOUT_S * 1000UL) {
        fail(PSTR("Charge timeout"));
      }
      break;
    }

    case ST_CHG_REST:
      if (elapsed >= CHG_REST_S * 1000UL) {
        if (!measureIR(T.ocv1, T.rint)) return fail(PSTR("ADC timeout"));
        if (isnan(T.rint)) return fail(PSTR("No load current"));
        lastRint = T.rint;
        startDischarge();
      }
      break;

    case ST_DISCHARGE:
      dischargeStep(now);
      break;

    case ST_RESULT:
      if (T.recharging) {
        T.okCnt = (chgFull() && !chgActive()) ? T.okCnt + 1 : 0;
        if ((elapsed > 20000UL && T.okCnt >= 10) || elapsed >= CHG_TIMEOUT_S * 1000UL) {
          chargerOff();
          T.recharging = false;
          forceDraw = true;
        }
      }
      if (back) {
        allOff();
        T.recharging = false;
        enter(ST_IDLE);
      }
      break;

    case ST_OUTPUT: {
      float i = iBat - iIdle;
      float dtH = (now - T.tLast) / 3600000.0f;
      T.tLast = now;
      T.mAh += i * 1000.0f * dtH;
      T.mWh += i * vBat * 1000.0f * dtH;
      if (i < T.iMin) T.iMin = i;
      if (i > T.iMax) T.iMax = i;
      T.lowCnt = (vBat < cutoffV()) ? T.lowCnt + 1 : 0;
      if (iBat > MAX_CURRENT_A) finishDischarge(PSTR("Overcurrent"));
      else if (T.lowCnt >= CUTOFF_COUNT) finishDischarge(PSTR("Cutoff reached"));
      else if (elapsed > DIS_TIMEOUT_S * 1000UL) finishDischarge(PSTR("Timeout"));
      break;
    }

    case ST_ERROR:
      if (back) enter(ST_IDLE);
      break;
  }
}

// ------------------------------------------------------------------ screens --
// 128x64 OLED: title (yellow band), big value, 4 lines of up to 21 characters.

static void buildScreen(Screen &s, uint32_t now) {
  char a[12], b[12];
  memset(&s, 0, sizeof(s));
  uint32_t el = (now - T.tPhase) / 1000UL;

  if (state != ST_RESULT && state != ST_ERROR) {
    strcat_P(f2s(s.big, vBat, 3), PSTR("V"));
  }

  switch (state) {
    case ST_IDLE:
      if (vBat < NO_BATT_V) {
        strcpy_P(s.title, PSTR("NO BATTERY"));
        strcpy_P(s.big, PSTR("-.---V"));
        LINE(0, "Insert cell");
      } else {
        snprintf_P(s.title, sizeof(s.title), PSTR("READY  %s"), MODE_NAME[mode]);
        LINE(0, "I %sA  SoC~%d%%", f2s(a, iBat, 3), (int)(soc(vBat) + 0.5f));
        LINE(1, "Rint %s", fmtRint(a, lastRint));
        if (calLoad) LINE(2, "CAL: load ON");
        else LINE(2, "%s %sV", chemName(cfg.chem), f2s(b, cutoffV(), 2));
      }
      break;

    case ST_QUICK_DRAIN:
    case ST_DISCHARGE:
      if (state == ST_QUICK_DRAIN) strcpy_P(s.title, T.round > 1 ? PSTR("QUICK DRAIN 2") : PSTR("QUICK DRAIN"));
      else if (T.mode == MODE_CHGDIS) strcpy_P(s.title, PSTR("CHG+DIS: DISCH"));
      else strcpy_P(s.title, PSTR("FULL DISCHARGE"));
      LINE(0, "I %sA  %smAh", f2s(a, iBat, 3), f2s(b, T.mAh, 1));
      LINE(1, "%sWh  R %s", f2s(a, T.mWh / 1000.0f, 2), fmtRint(b, T.rint));
      if (state == ST_QUICK_DRAIN)
        LINE(2, "t %s  dV %d/%dmV", fmtTime(a, el), (int)((T.ocv1 - restEstimate()) * 1000.0f),
             (int)((T.ocv1 - T.ocvTarget) * 1000.0f + 0.5f));
      else LINE(2, "t %s", fmtTime(a, el));
      break;

    case ST_QUICK_REST:
      strcpy_P(s.title, PSTR("QUICK REST"));
      LINE(0, "Rest %s  settling", fmtTime(a, el));
      LINE(1, "Out %smAh  dV %dmV", f2s(a, T.mAh, 1), (int)((T.ocv1 - vBat) * 1000.0f));
      LINE(2, "OCV0 %sV SoC~%d%%", f2s(a, T.ocv1, 3), (int)(soc(T.ocv1) + 0.5f));
      break;

    case ST_CHARGE:
      strcpy_P(s.title, PSTR("CHARGING"));
      if (chgActive()) LINE(0, "Charging...");
      else if (chgFull()) LINE(0, "Charge done");
      else LINE(0, "No charger?");
      LINE(1, "I %sA  t %s", f2s(a, iBat, 3), fmtTime(b, el));
      LINE(2, "Then rest %umin", (unsigned)(CHG_REST_S / 60));
      break;

    case ST_CHG_REST:
      strcpy_P(s.title, PSTR("REST"));
      LINE(0, "Rest %s / %s", fmtTime(a, el), fmtTime(b, CHG_REST_S));
      LINE(1, "Charged full");
      LINE(2, "Then discharge");
      break;

    case ST_RESULT:
      if (T.mode == MODE_QUICK) {
        strcpy_P(s.title, PSTR("QUICK RESULT"));
        if (isnan(T.ocv2)) {
          strcpy_P(s.big, PSTR("EMPTY"));
        } else if (isnan(T.capacity)) {
          strcpy_P(s.big, PSTR("N/A"));
        } else {
          s.big[0] = '~';
          strcat_P(f2s(s.big + 1, T.capacity, 0), PSTR("mAh"));
        }
        LINE(0, "Out %smAh", f2s(a, T.mAh, 1));
        if (!isnan(T.ocv2)) {
          if (!isnan(T.capacity))
            LINE(1, "SoC %d->%d%% Rem~%s", (int)(soc(T.ocv1) + 0.5f),
                 (int)(soc(T.ocv2) + 0.5f), f2s(a, T.capacity * soc(T.ocv2) / 100.0f, 0));
          else
            LINE(1, "SoC %d->%d%%", (int)(soc(T.ocv1) + 0.5f), (int)(soc(T.ocv2) + 0.5f));
        }
        LINE(2, "Rint %s", fmtRint(a, T.rint));
        LINE(3, "%s", T.reason);
      } else if (T.mode == MODE_OUTPUT) {
        strcpy_P(s.title, PSTR("OUTPUT RESULT"));
        strcat_P(f2s(s.big, T.mAh, 1), PSTR("mAh"));
        float hours = T.disMs / 3600000.0f;
        LINE(0, "%sWh  t %s", f2s(a, T.mWh / 1000.0f, 3), fmtTime(b, T.disMs / 1000UL));
        LINE(1, "avg %sA max %sA", f2s(a, hours > 0 ? T.mAh / 1000.0f / hours : 0, 3), f2s(b, T.iMax, 3));
        LINE(2, "%s", T.reason);
      } else {
        strcpy_P(s.title, T.mode == MODE_CHGDIS ? PSTR("CHG+DIS RESULT") : PSTR("FULL RESULT"));
        strcat_P(f2s(s.big, T.mAh, 0), PSTR("mAh"));
        float hours = T.disMs / 3600000.0f;
        LINE(0, "%sWh  t %s", f2s(a, T.mWh / 1000.0f, 2), fmtTime(b, T.disMs / 1000UL));
        LINE(1, "Iavg %sA R%s", f2s(a, hours > 0 ? T.mAh / 1000.0f / hours : 0, 3), fmtRint(b, T.rint));
        LINE(2, "%s", T.reason);
        if (T.recharging) LINE(3, "Recharging...");
      }
      break;

    case ST_OUTPUT: {
      float i = iShown();
      strcpy_P(s.title, PSTR("OUTPUT ON"));
      LINE(0, "I %sA  P %sW", f2s(a, i, 3), f2s(b, i * vBat, 2));
      LINE(1, "%smAh  %sWh", f2s(a, T.mAh, 1), f2s(b, T.mWh / 1000.0f, 3));
      LINE(2, "t %s", fmtTime(a, el));
      LINE(3, "min %s max %sA", f2s(a, T.iMin, 3), f2s(b, T.iMax, 3));
      break;
    }

    case ST_ERROR:
      strcpy_P(s.title, PSTR("ERROR"));
      strcpy_P(s.big, PSTR("!"));
      LINE(0, "%s", T.reason);
      break;
  }
}

// Power-up screen: the stored battery type and cutoff voltage
static void showSettingsScreen() {
  Screen s;
  char a[12];
  memset(&s, 0, sizeof(s));
  strcpy_P(s.title, PSTR("SETTINGS"));
  strcat_P(f2s(s.big, cutoffV(), 2), PSTR("V"));
  LINE(0, "Type   %s", chemName(cfg.chem));
  LINE(1, "Cutoff %sV", f2s(a, cutoffV(), 2));
  LINE(3, "Change in PC app");
  uiShow(s, true);
}

static void refreshDisplay(uint32_t now) {
  if (!forceDraw && now - lastDraw < DISP_REFRESH_MS) return;
  bool running = state != ST_IDLE && state != ST_RESULT && state != ST_ERROR;
  uiDim(running && now - T.tPhase > OLED_DIM_AFTER_MS);
  Screen s;
  buildScreen(s, now);
  uiShow(s, forceDraw);
  lastDraw = millis();
  forceDraw = false;
}

// Telemetry for the PC app: D,ms,state,mode,V,A,mAh,mWh,rint_mOhm
static void logSample(uint32_t now) {
  if (!telemMs || now - lastLog < telemMs) return;
  lastLog = now;
  Serial.print(F("D,"));
  Serial.print(now);                          Serial.print(',');
  Serial.print(state);                        Serial.print(',');
  Serial.print(isRunning() || state == ST_RESULT ? T.mode : mode);
  Serial.print(',');
  Serial.print(vBat, 4);                      Serial.print(',');
  Serial.print(iShown(), 4);                  Serial.print(',');
  Serial.print(T.mAh, 2);                     Serial.print(',');
  Serial.print(T.mWh, 1);                     Serial.print(',');
  Serial.println(isRunning() ? T.rint : lastRint, 1);
}

// ------------------------------------------------------------- calibration --
// Serial commands, one per line (also used by the PC app in tools/):
//   ?          help
//   s [n]      start a test (n: 0 QUICK, 1 FULL, 2 CHG+DIS; default: selected mode)   [idle]
//   x          abort a running test / leave the result or error screen
//   r          internal resistance test                                                [idle]
//   p <n>      select mode n                                                           [idle]
//   t <ms>     telemetry interval (0 = off)
//   m          toggle live readings every second (V, I and raw ADC inputs)
//   v <volts>  set voltage factor from a multimeter reading at the cell (load off)      [idle]
//   l 1 / l 0  switch the load on / off (on turns off by itself after 60 s)             [idle]
//   i <amps>   set current factor from a multimeter in the cell lead (load on)         [idle]
//   c <n>      battery chemistry 0 Li-ion/LiPo, 1 LiHV, 2 LiFePO4 (sets its default cutoff) [idle]
//   u <volts>  end-of-discharge (cutoff) voltage                                        [idle]
//   g          print the settings: S,chem,cutoff_mV,min_mV,max_mV,can_charge
// Chemistry and cutoff are saved on the chip at once.
// New factors are used at once and printed; copy them into config.h to keep them.

static void printCal() {
  Serial.print(F("#define V_CAL "));
  Serial.print(vCal, 5);
  Serial.println('f');
  Serial.print(F("#define I_CAL "));
  Serial.print(iCal, 5);
  Serial.println('f');
}

static void printMonitor() {
  float ain2 = adc.readVolts(CH_VBAT, 2);
  float ain1 = adc.readVolts(CH_SHUNT, 2);
  Serial.print(F("# V="));
  Serial.print(vBat, 4);
  Serial.print(F(" I="));
  Serial.print(iBat, 4);
  Serial.print(F(" AIN2="));
  Serial.print(ain2 * 1000.0f, 2);
  Serial.print(F("mV AIN1="));
  Serial.print(ain1 * 1000.0f, 3);
  Serial.print(F("mV load="));
  Serial.println(digitalRead(PIN_LOAD_EN));
}

static void printSettings() {
  Serial.print(F("S,"));
  Serial.print(cfg.chem);                    Serial.print(',');
  Serial.print(cfg.cutoffMv);                Serial.print(',');
  Serial.print(chemMinCutoff(cfg.chem));     Serial.print(',');
  Serial.print(chemMaxCutoff(cfg.chem));     Serial.print(',');
  Serial.println(chemCanCharge(cfg.chem));
}

static void saveSettings() {
  if (!settingsSave()) Serial.println(F("# ERROR: settings not saved"));
  printSettings();
  forceDraw = true;
}

// Small replacement for atof (saves ~0.6 KB of flash): [-]digits[.digits]
static float parseNum(const char *p) {
  while (*p == ' ') p++;
  bool neg = *p == '-';
  if (neg || *p == '+') p++;
  float v = 0, scale = 0;
  for (; *p; p++) {
    if (*p == '.' && !scale) scale = 1;
    else if (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); if (scale) scale *= 10; }
    else break;
  }
  if (scale) v /= scale;
  return neg ? -v : v;
}

static void handleCommand(char *cmd) {
  char c = tolower(cmd[0]);
  float arg = parseNum(cmd + 1);
  bool hasArg = strpbrk(cmd + 1, "0123456789") != nullptr;
  if (state != ST_IDLE && strchr("sprvlicu", c)) {
    Serial.println(F("# only on the idle screen"));
    return;
  }
  switch (c) {
    case 'm':
      calMonitor = !calMonitor;
      break;
    case 't':
      telemMs = arg < 0 ? 0 : (uint16_t)arg;
      break;
    case 'p':
    case 's':
      if (hasArg) {
        mode = (Mode)constrain((int)arg, 0, MODE_COUNT - 1);
        forceDraw = true;
      }
      if (c == 's') cmdStart = Button::SHORT;
      break;
    case 'r':
      cmdRint = Button::SHORT;
      break;
    case 'x':
      cmdStart = isRunning() ? Button::LONG : Button::SHORT;
      break;
    case 'v': {
      if (calLoad) { Serial.println(F("# switch the load off first: l 0")); return; }
      float v, i;
      if (arg < 1.0f || !readVI(8, v, i) || v < 0.5f) { Serial.println(F("# usage: v 3.987")); return; }
      // only the divider part scales with V_CAL; the shunt-drop term stays as it is
      vCal *= (arg - i * SHUNT_OHMS) / (v - i * SHUNT_OHMS);
      printCal();
      forceDraw = true;
      break;
    }
    case 'l':
      if (arg > 0.5f) {
        loadOn();
        calLoad = true;
        calLoadT0 = millis();
        Serial.println(F("# load ON (60 s)"));
      } else {
        loadOff();
        Serial.println(F("# load off"));
      }
      forceDraw = true;
      break;
    case 'i': {
      if (!calLoad) { Serial.println(F("# switch the load on first: l 1")); return; }
      float v, i;
      if (arg < 0.05f || !readVI(8, v, i) || i < MIN_LOAD_A) { Serial.println(F("# usage: i 1.234")); return; }
      iCal *= arg / i;
      printCal();
      break;
    }
    case 'c':
      if (!hasArg || arg >= CHEM_COUNT) { Serial.println(F("# usage: c 0|1|2")); return; }
      cfg.chem = (uint8_t)arg;
      cfg.cutoffMv = chemDefaultCutoff(cfg.chem);
      saveSettings();
      break;
    case 'u': {
      uint16_t mv = (uint16_t)(arg * 1000.0f + 0.5f);
      if (!chemCutoffOk(cfg.chem, mv)) Serial.println(F("# cutoff out of range for this chemistry"));
      else {
        cfg.cutoffMv = mv;
        saveSettings();
      }
      break;
    }
    case 'g':
      printSettings();
      break;
    default:
      Serial.println(F("# s [n]=start  x=abort/back  r=Rint  p <n>=mode  t <ms>=telemetry"));
      Serial.println(F("# c <n>=chemistry  u <V>=cutoff  g=settings (saved on the chip)"));
      Serial.println(F("# m=monitor  v <V>=cal volts  l 1/0=load  i <A>=cal amps (meter in cell lead)"));
      printCal();
  }
}

static void pollSerial() {
  static char buf[24];
  static uint8_t n = 0;
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == '\r' || ch == '\n') {
      if (n) {
        buf[n] = 0;
        handleCommand(buf);
        n = 0;
      }
    } else if (n < sizeof(buf) - 1) {
      buf[n++] = ch;
    }
  }
}

// ------------------------------------------------------------- setup / loop --

void setup() {
  pinMode(PIN_LOAD_EN, OUTPUT);
  pinMode(PIN_CHG_EN, OUTPUT);
  allOff();
  pinMode(PIN_ADC_CS, OUTPUT);
  digitalWrite(PIN_ADC_CS, HIGH);
  pinMode(PIN_CHG_STDBY, INPUT_PULLUP);
  pinMode(PIN_CHG_CHRG, INPUT_PULLUP);

  Serial.begin(SERIAL_BAUD);
  Serial.println(F("# Li-ion analyzer (c) 2026 Ashoka Munasinghe, www.electronics.lk, MIT License  (send ? for help)"));
  // Clock diagnostics: F_CPU must match the real clock or all timing (and mAh) is off
  Serial.print(F("# F_CPU="));
  Serial.print(F_CPU);
  Serial.print(F(" CLKPR=0x"));
  Serial.println(CLKPR, HEX);
  settingsLoad();
  printSettings();
  Serial.println(F("# D,ms,state,mode,V,A,mAh,mWh,rint_mOhm"));

  SPI.begin();
  bStart.begin(PIN_BTN_START);
  bMode.begin(PIN_BTN_MODE);
  bRint.begin(PIN_BTN_RINT);
  adc.setHook(pollInputs);
  uiBegin();
  showSettingsScreen();
  delay(SPLASH_MS);

  memset(&T, 0, sizeof(T));
  bool found = adc.begin(ADC_CLOCK_REG, ADC_VREF);
  Serial.print(F("# AD7705 clock reg wrote 0x"));
  Serial.print(ADC_CLOCK_REG, HEX);
  Serial.print(F(" read 0x"));
  Serial.println(adc.probe(), HEX);
  if (!found) {
    fail(PSTR("AD7705 missing"));
    return;
  }
  // shunt: bipolar (charging reads negative); divider: unipolar
  if (!adc.configure(CH_SHUNT, SHUNT_GAIN, false, false) ||
      !adc.configure(CH_VBAT, VBAT_GAIN, true, false)) {
    fail(PSTR("AD7705 no DRDY"));
    return;
  }
  float v;
  if (readVI(4, v, iIdle)) vBat = v;
}

void loop() {
  pollInputs();
  float v = NAN, i = NAN;
  if (!readVI(2, v, i)) {
    if (state != ST_ERROR) fail(PSTR("ADC timeout"));
  } else {
    vBat = v;
    iBat = i;
  }

  uint32_t now = millis();
  Button::Event eStart = bStart.take(), eMode = bMode.take(), eRint = bRint.take();
  if (cmdStart != Button::NONE) { eStart = cmdStart; cmdStart = Button::NONE; }
  if (cmdRint != Button::NONE) { eRint = cmdRint; cmdRint = Button::NONE; }
  if (state == ST_ERROR && isnan(v)) {
    // ADC down: only the buttons and the display work
    if (eStart == Button::SHORT || eRint == Button::SHORT) enter(ST_IDLE);
  } else {
    runState(eStart, eMode, eRint, now);
  }

  pollSerial();
  now = millis();
  if (calMonitor && now - lastMon >= 1000) {
    lastMon = now;
    printMonitor();
  }
  refreshDisplay(now);
  logSample(now);
}
