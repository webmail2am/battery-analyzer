#include "ad7705.h"
#include <SPI.h>

static const SPISettings kSpi(1000000, MSBFIRST, SPI_MODE3);

// Communication register: RS2..RS0 = bits 6:4, R/W = bit 3, CH1..CH0 = bits 1:0
static const uint8_t REG_COMM  = 0x00;
static const uint8_t REG_SETUP = 0x10;
static const uint8_t REG_CLOCK = 0x20;
static const uint8_t REG_DATA  = 0x30;
static const uint8_t RD        = 0x08;
static const uint8_t MD_SELFCAL = 0x40;

void AD7705::writeReg(uint8_t comm, uint8_t val) {
  SPI.beginTransaction(kSpi);
  digitalWrite(cs_, LOW);
  SPI.transfer(comm);
  SPI.transfer(val);
  digitalWrite(cs_, HIGH);
  SPI.endTransaction();
}

uint8_t AD7705::read8(uint8_t comm) {
  SPI.beginTransaction(kSpi);
  digitalWrite(cs_, LOW);
  SPI.transfer(comm);
  uint8_t v = SPI.transfer(0xFF);
  digitalWrite(cs_, HIGH);
  SPI.endTransaction();
  return v;
}

uint16_t AD7705::read16(uint8_t comm) {
  SPI.beginTransaction(kSpi);
  digitalWrite(cs_, LOW);
  SPI.transfer(comm);
  uint16_t v = (uint16_t)SPI.transfer(0xFF) << 8;
  v |= SPI.transfer(0xFF);
  digitalWrite(cs_, HIGH);
  SPI.endTransaction();
  return v;
}

bool AD7705::begin(uint8_t clockReg, float vref) {
  vref_ = vref;
  pinMode(cs_, OUTPUT);
  digitalWrite(cs_, HIGH);
  if (drdy_ >= 0) pinMode(drdy_, INPUT);
  if (rst_ >= 0) {
    pinMode(rst_, OUTPUT);
    digitalWrite(rst_, LOW);
    delay(2);
    digitalWrite(rst_, HIGH);
    delay(2);
  }
  // Serial interface reset: at least 32 clocks with DIN high
  SPI.beginTransaction(kSpi);
  digitalWrite(cs_, LOW);
  for (uint8_t i = 0; i < 5; i++) SPI.transfer(0xFF);
  digitalWrite(cs_, HIGH);
  SPI.endTransaction();
  delay(2);

  writeReg(REG_CLOCK, clockReg);
  probe_ = read8(REG_CLOCK | RD);
  return (probe_ & 0x1F) == (clockReg & 0x1F);
}

bool AD7705::configure(uint8_t ch, uint8_t gain, bool unipolar, bool buffered) {
  uint8_t g = 0;
  while ((1u << g) < gain && g < 7) g++;
  setup_[ch] = (g << 3) | (unipolar ? 0x04 : 0) | (buffered ? 0x02 : 0);
  bipolar_[ch] = !unipolar;
  lsb_[ch] = vref_ / (float)(1u << g) / (unipolar ? 65536.0f : 32768.0f);

  writeReg(REG_SETUP | ch, MD_SELFCAL | setup_[ch]);
  active_ = ch;
  if (!waitReady(ch, 1000)) return false;
  read16(REG_DATA | RD | ch);   // clear DRDY
  return true;
}

bool AD7705::waitReady(uint8_t ch, uint16_t timeoutMs) {
  uint32_t t0 = millis();
  for (;;) {
    bool ready = (drdy_ >= 0) ? (digitalRead(drdy_) == LOW)
                              : !(read8(REG_COMM | RD | ch) & 0x80);
    if (ready) return true;
    if (hook_) hook_();
    if (millis() - t0 > timeoutMs) return false;
  }
}

bool AD7705::readRaw(uint8_t ch, uint16_t &code) {
  if (active_ != ch) {
    // Rewriting the setup register switches channel and restarts the filter
    writeReg(REG_SETUP | ch, setup_[ch]);
    active_ = ch;
  }
  if (!waitReady(ch, 300)) return false;
  code = read16(REG_DATA | RD | ch);
  return true;
}

float AD7705::readVolts(uint8_t ch, uint8_t n) {
  uint16_t code;
  if (active_ != ch && !readRaw(ch, code)) return NAN;   // discard first after switch
  int32_t sum = 0;
  for (uint8_t i = 0; i < n; i++) {
    if (!readRaw(ch, code)) return NAN;
    sum += bipolar_[ch] ? (int32_t)code - 32768 : (int32_t)code;
  }
  return (float)sum / n * lsb_[ch];
}
