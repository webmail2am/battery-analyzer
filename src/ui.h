#pragma once
#include <Arduino.h>

// One full OLED screen (128x64): title in the yellow band, one big value, four text lines.
struct Screen {
  char title[17];
  char big[11];
  char line[4][22];
};

void uiBegin();
// Draws the screen if its content changed (or force). Returns true if drawn.
bool uiShow(const Screen &s, bool force);
// Lower brightness during long tests (OLED burn-in).
void uiDim(bool dim);
