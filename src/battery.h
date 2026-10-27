#pragma once
#include <stdint.h>

// Supported cell chemistries. The analyzer runs from the cell under test through a boost
// converter, so only cells that stay above ~2 V can be tested (no NiMH / NiCd / LTO).
enum Chem : uint8_t { CHEM_LIION, CHEM_LIHV, CHEM_LFP, CHEM_COUNT };

const char *chemName(uint8_t chem);           // short display name
uint16_t chemDefaultCutoff(uint8_t chem);     // mV
bool chemCutoffOk(uint8_t chem, uint16_t mv); // allowed cutoff range for the chemistry
uint16_t chemMinCutoff(uint8_t chem);
uint16_t chemMaxCutoff(uint8_t chem);
bool chemCanCharge(uint8_t chem);             // TP4056 charges to 4.2 V: Li-ion / LiPo only

// State of charge (0..100 %) from a rested open-circuit voltage, for the given chemistry.
float socFromOcv(uint8_t chem, float volts);
