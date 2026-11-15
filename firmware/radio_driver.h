#pragma once

#include <Arduino.h>
#include <SPI.h>

// Keep the original Sandeep Mistry SX1276 backend and add RadioLib-backed
// SX1261 support behind one small polling interface.  The two node sketches
// each include a copy of this file because Arduino only compiles sketch-local
// sources.
namespace tuya_lora {

enum LoraModule : uint8_t {
  LORA_MODULE_SX1276 = 0,
  LORA_MODULE_SX1261 = 1,
};

struct LoraPins {
  int16_t sck = 5;
  int16_t miso = 19;
  int16_t mosi = 27;
  int16_t nss = 18;
  int16_t nrst = 23;
  int16_t dio0 = 26;  // SX1276
  int16_t dio1 = 26;  // SX1261
  int16_t busy = 32;  // SX1261
  int16_t rxen = 25;  // SX1261 external RF switch
  int16_t txen = 33;  // SX1261 external RF switch
};

class RadioDriver {
 public:
  bool begin(uint8_t module, const LoraPins &pins, float frequencyMhz,
             int8_t txPower, uint8_t sf, uint32_t bandwidthHz,
             uint8_t codingRate, uint16_t preamble, uint8_t syncWord);
  void end();
  void receive();
  bool available();
  size_t packetLength();
  bool read(uint8_t *buffer, size_t length);
  int packetRssi();
  float packetSnr();
  bool transmit(const uint8_t *buffer, size_t length);
  const char *moduleName() const;
  bool isReady() const { return _ready; }
  int16_t lastError() const { return _lastError; }

 private:
  uint8_t _module = LORA_MODULE_SX1276;
  bool _ready = false;
  int16_t _lastError = 0;
  int16_t _irqPin = -1;
};

}  // namespace tuya_lora
