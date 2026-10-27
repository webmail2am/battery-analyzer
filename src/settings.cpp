// Copyright (c) 2026 Ashoka Munasinghe. MIT License, see LICENSE.
// SPDX-License-Identifier: MIT

#include "settings.h"
#include "battery.h"

Settings cfg;

static const uint8_t MAGIC = 0xBA;

#if defined(LGT8F)
// LGT8F328P: no real EEPROM; the chip emulates it in flash. 1 KB emulation lives at
// 0x7800-0x7FFF, above the LGT bootloader (0x7400-0x77FF): the program must stay below
// 0x7400 (platformio.ini sets that limit). Register sequences follow the lgt8fx core.
#define ECCR _SFR_IO8(0x36)

static void e2pInit() {
  ECCR = 0x80;   // unlock
  ECCR = 0x4C;   // enable, 1 KB
}

static uint8_t e2pRead(uint16_t a) {
  EEARL = a & 0xff;
  EEARH = a >> 8;
  EECR |= _BV(EERE);
  __asm__ __volatile__("nop\n nop");
  return EEDR;
}

static void e2pWrite(uint16_t a, uint8_t v) {
  uint8_t sreg = SREG;
  EEARL = a & 0xff;
  EEARH = a >> 8;
  EEDR = v;
  cli();
  EECR = 0x04;
  EECR = 0x02;
  SREG = sreg;
}
#else
#include <avr/eeprom.h>
static void e2pInit() {}
static uint8_t e2pRead(uint16_t a) { return eeprom_read_byte((const uint8_t *)a); }
static void e2pWrite(uint16_t a, uint8_t v) { eeprom_update_byte((uint8_t *)a, v); }
#endif

// Stored image: magic, chem, cutoff lo, cutoff hi, checksum
static void toBytes(uint8_t b[5]) {
  b[0] = MAGIC;
  b[1] = cfg.chem;
  b[2] = cfg.cutoffMv & 0xff;
  b[3] = cfg.cutoffMv >> 8;
  b[4] = b[0] ^ b[1] ^ b[2] ^ b[3] ^ 0x5A;
}

void settingsLoad() {
  e2pInit();
  uint8_t b[5];
  for (uint8_t k = 0; k < 5; k++) b[k] = e2pRead(k);
  cfg.chem = b[1];
  cfg.cutoffMv = b[2] | (b[3] << 8);
  bool ok = b[0] == MAGIC && b[4] == (uint8_t)(b[0] ^ b[1] ^ b[2] ^ b[3] ^ 0x5A) &&
            chemCutoffOk(cfg.chem, cfg.cutoffMv);
  if (!ok) {
    cfg.chem = CHEM_LIION;
    cfg.cutoffMv = chemDefaultCutoff(CHEM_LIION);
  }
}

bool settingsSave() {
  uint8_t b[5];
  toBytes(b);
  for (uint8_t k = 0; k < 5; k++)
    if (e2pRead(k) != b[k]) e2pWrite(k, b[k]);   // write only what changed
  for (uint8_t k = 0; k < 5; k++)
    if (e2pRead(k) != b[k]) return false;
  return true;
}
