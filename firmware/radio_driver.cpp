#include "radio_driver.h"

#include <LoRa.h>
#include <RadioLib.h>

namespace tuya_lora {

namespace {
Module *sx1261Module = nullptr;
volatile bool sx1261PacketReady = false;

void onSx1261Receive() {
  sx1261PacketReady = true;
}

// The DX-LR20 identifies its compatible silicon as LLCC68 even though the
// working radio configuration is the SX1261 profile. Keep the public driver
// explicitly SX1261, but retain the same version-string compatibility retry as
// the proven DX-LR20 experiment. This does not perform module auto-discovery.
class DxLr20Sx1261 : public SX1261 {
 public:
  explicit DxLr20Sx1261(Module *mod) : SX1261(mod) {
    // The working DX-LR20 experiment identifies this module as XTAL-backed;
    // sending the TCXO command causes SPI command failure (-707).
    tcxoVoltage = VoltageNone;
  }

  int16_t begin(float freq, float bw, uint8_t sf, uint8_t cr,
                uint8_t syncWord, int8_t power, uint16_t preambleLength,
                float requestedTcxoVoltage, bool useRegulatorLDO) {
    chipType = "LLCC68";
    this->tcxoVoltage = requestedTcxoVoltage == 0.0f
                            ? VoltageNone
                            : static_cast<RadioLibTCXOVoltage_t>(
                                  requestedTcxoVoltage);
    this->useRegulatorLDO = useRegulatorLDO;
    int16_t state = SX126x::begin(cr, syncWord, preambleLength);
    if (state == RADIOLIB_ERR_CHIP_NOT_FOUND) {
      chipType = RADIOLIB_SX1261_CHIP_TYPE;
      state = SX126x::begin(cr, syncWord, preambleLength);
      Serial.printf("[radio] DX-LR20 identity fallback to SX1261 state=%d\n",
                    state);
    }
    if (state != RADIOLIB_ERR_NONE) return state;
    if ((state = setSpreadingFactor(sf)) != RADIOLIB_ERR_NONE) return state;
    if ((state = setBandwidth(bw)) != RADIOLIB_ERR_NONE) return state;
    if ((state = setFrequency(freq)) != RADIOLIB_ERR_NONE) return state;
    if ((state = SX126x::fixPaClamping()) != RADIOLIB_ERR_NONE) return state;
    return setOutputPower(power);
  }
};

DxLr20Sx1261 *sx1261 = nullptr;

void destroySx1261() {
  delete sx1261;
  sx1261 = nullptr;
  delete sx1261Module;
  sx1261Module = nullptr;
}
}

bool RadioDriver::begin(uint8_t module, const LoraPins &pins,
                        float frequencyMhz, int8_t txPower, uint8_t sf,
                        uint32_t bandwidthHz, uint8_t codingRate,
                        uint16_t preamble, uint8_t syncWord) {
  end();
  _module = module;
  _lastError = RADIOLIB_ERR_NONE;
  _irqPin = module == LORA_MODULE_SX1261 ? pins.dio1 : -1;
  Serial.printf("[radio] driver=%s SPI=%d/%d/%d NSS=%d NRST=%d DIO0=%d DIO1=%d BUSY=%d RXEN=%d TXEN=%d\n",
                module == LORA_MODULE_SX1261 ? "SX1261" : "SX1276",
                pins.sck, pins.miso, pins.mosi, pins.nss, pins.nrst, pins.dio0,
                pins.dio1, pins.busy, pins.rxen, pins.txen);
  if (pins.sck < 0 || pins.miso < 0 || pins.mosi < 0 || pins.nss < 0 ||
      pins.nrst < 0) {
    _lastError = -1000;
    return false;
  }
  SPI.begin(pins.sck, pins.miso, pins.mosi, pins.nss);
  // Match the proven DX-LR20 startup sequence: establish inactive CS and
  // released reset before RadioLib performs its first command.
  pinMode(pins.nss, OUTPUT);
  digitalWrite(pins.nss, HIGH);
  pinMode(pins.nrst, OUTPUT);
  digitalWrite(pins.nrst, HIGH);
  delay(2);

  if (module == LORA_MODULE_SX1276) {
    if (pins.dio0 < 0) { _lastError = -1001; return false; }
    LoRa.setPins(pins.nss, pins.nrst, pins.dio0);
    if (!LoRa.begin(frequencyMhz * 1000000.0)) { _lastError = -1002; return false; }
    LoRa.setTxPower(txPower);
    LoRa.setSpreadingFactor(sf);
    LoRa.setSignalBandwidth(bandwidthHz);
    LoRa.setCodingRate4(codingRate);
    LoRa.setPreambleLength(preamble);
    LoRa.setSyncWord(syncWord);
    LoRa.enableCrc();
    receive();
    _ready = true;
    return true;
  }

  if (module != LORA_MODULE_SX1261) { _lastError = -1003; return false; }
  if (pins.dio1 < 0 || pins.busy < 0) { _lastError = -1004; return false; }
  sx1261Module = new Module(pins.nss, pins.dio1, pins.nrst, pins.busy, SPI);
  sx1261 = new DxLr20Sx1261(sx1261Module);
  if (pins.rxen >= 0 && pins.txen >= 0) {
    sx1261->setRfSwitchPins(pins.rxen, pins.txen);
  }

  const int8_t effectivePower = txPower > 15 ? 15 : txPower;
  if (effectivePower != txPower) {
    Serial.printf("[radio] SX1261 limits TX power %d dBm to %d dBm\n",
                  txPower, effectivePower);
  }
  int16_t state = sx1261->begin(frequencyMhz, bandwidthHz / 1000.0f, sf,
                                codingRate, syncWord, effectivePower, preamble,
                                0.0f, false);
  Serial.printf("[radio] SX1261 begin state=%d\n", state);
  if (state != RADIOLIB_ERR_NONE) {
    _lastError = state;
    destroySx1261();
    return false;
  }
  _lastError = sx1261->setCRC(2);
  if (_lastError != RADIOLIB_ERR_NONE) {
    Serial.printf("[radio] SX1261 CRC setup state=%d\n", _lastError);
    destroySx1261();
    return false;
  }
  sx1261->setDio1Action(onSx1261Receive);
  receive();
  _ready = true;
  return true;
}

void RadioDriver::end() {
  _ready = false;
  if (_module == LORA_MODULE_SX1261 && sx1261) {
    sx1261->sleep();
    destroySx1261();
  } else {
    LoRa.end();
  }
}

void RadioDriver::receive() {
  if (_module == LORA_MODULE_SX1261) {
    sx1261PacketReady = false;
    if (sx1261) sx1261->startReceive();
  } else {
    LoRa.receive();
  }
}

bool RadioDriver::available() {
  if (!_ready) return false;
  if (_module == LORA_MODULE_SX1261) {
    return sx1261PacketReady;
  }
  return LoRa.parsePacket() > 0;
}

size_t RadioDriver::packetLength() {
  if (_module == LORA_MODULE_SX1261) {
    return sx1261 ? sx1261->getPacketLength() : 0;
  }
  return static_cast<size_t>(LoRa.parsePacket());
}

bool RadioDriver::read(uint8_t *buffer, size_t length) {
  if (_module == LORA_MODULE_SX1261) {
    bool ok = sx1261 && sx1261->readData(buffer, length) == RADIOLIB_ERR_NONE;
    sx1261PacketReady = false;
    return ok;
  }
  size_t n = 0;
  while (LoRa.available() && n < length) buffer[n++] = static_cast<uint8_t>(LoRa.read());
  return n == length;
}

int RadioDriver::packetRssi() {
  if (_module == LORA_MODULE_SX1261) return sx1261 ? sx1261->getRSSI() : 0;
  return LoRa.packetRssi();
}

float RadioDriver::packetSnr() {
  if (_module == LORA_MODULE_SX1261) return sx1261 ? sx1261->getSNR() : 0.0f;
  return LoRa.packetSnr();
}

bool RadioDriver::transmit(const uint8_t *buffer, size_t length) {
  if (!_ready) return false;
  if (_module == LORA_MODULE_SX1261) {
    bool ok = sx1261 && sx1261->transmit(buffer, length) == RADIOLIB_ERR_NONE;
    receive();
    return ok;
  }
  LoRa.beginPacket();
  LoRa.write(buffer, length);
  bool ok = LoRa.endPacket();
  receive();
  return ok;
}

const char *RadioDriver::moduleName() const {
  return _module == LORA_MODULE_SX1261 ? "SX1261" : "SX1276";
}

}  // namespace tuya_lora
