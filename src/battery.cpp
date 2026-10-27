#include <Arduino.h>
#include "battery.h"

static const char *const NAMES[CHEM_COUNT] = {"Li-ion/LiPo", "LiHV 4.35V", "LiFePO4"};

// cutoff: default, minimum, maximum (mV)
static const uint16_t CUTOFF[CHEM_COUNT][3] PROGMEM = {
  {3000, 2500, 3600},   // Li-ion / LiPo (LCO, NMC), 4.20 V full
  {3000, 2500, 3600},   // LiHV, 4.35 V full
  {2500, 2000, 3000},   // LiFePO4, 3.65 V full
};

// Rested open-circuit voltage (mV) for SoC = 0, 5, 10 ... 100 %, about 25 C
static const uint16_t OCV_MV[CHEM_COUNT][21] PROGMEM = {
  {3270, 3610, 3690, 3710, 3730, 3750, 3770, 3790, 3800, 3820, 3840,
   3850, 3870, 3910, 3950, 3980, 4020, 4080, 4110, 4150, 4200},
  {3270, 3610, 3690, 3710, 3730, 3750, 3770, 3790, 3800, 3820, 3840,
   3860, 3890, 3940, 3990, 4030, 4080, 4150, 4210, 4280, 4350},
  // LiFePO4 is very flat between 20 and 90 %: SoC from voltage is only a rough guide there
  {2500, 2900, 3150, 3200, 3230, 3250, 3260, 3270, 3280, 3285, 3290,
   3295, 3300, 3305, 3310, 3320, 3330, 3340, 3350, 3400, 3600},
};

const char *chemName(uint8_t chem) { return NAMES[chem < CHEM_COUNT ? chem : 0]; }
uint16_t chemDefaultCutoff(uint8_t chem) { return pgm_read_word(&CUTOFF[chem][0]); }
uint16_t chemMinCutoff(uint8_t chem) { return pgm_read_word(&CUTOFF[chem][1]); }
uint16_t chemMaxCutoff(uint8_t chem) { return pgm_read_word(&CUTOFF[chem][2]); }
bool chemCanCharge(uint8_t chem) { return chem == CHEM_LIION; }

bool chemCutoffOk(uint8_t chem, uint16_t mv) {
  return chem < CHEM_COUNT && mv >= chemMinCutoff(chem) && mv <= chemMaxCutoff(chem);
}

float socFromOcv(uint8_t chem, float volts) {
  const uint16_t *t = OCV_MV[chem < CHEM_COUNT ? chem : 0];
  float mv = volts * 1000.0f;
  if (mv <= pgm_read_word(&t[0])) return 0.0f;
  if (mv >= pgm_read_word(&t[20])) return 100.0f;
  for (uint8_t i = 0; i < 20; i++) {
    float lo = pgm_read_word(&t[i]);
    float hi = pgm_read_word(&t[i + 1]);
    if (mv < hi) return (i + (mv - lo) / (hi - lo)) * 5.0f;
  }
  return 100.0f;
}
