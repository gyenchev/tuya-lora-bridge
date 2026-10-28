#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

namespace tuya_lora {

// Local-only LoRa link counters. These values are intentionally not published
// to MQTT or Home Assistant; they are exposed through the base web UI only.
class LoraStats {
 public:
  void configure(uint8_t spreadingFactor, uint32_t bandwidthHz,
                 uint8_t codingRate, uint16_t preamble, int8_t txPowerDbm,
                 float frequencyMhz);
  void reset(uint32_t nowMs);

  // frameBytes is the complete encrypted frame on air; payloadBytes is the
  // decoded application payload. RX is split because the frame is counted
  // before authentication succeeds.
  void noteRx(size_t frameBytes, int rssi, float snr, uint32_t nowMs);
  void noteRxPayload(size_t payloadBytes);
  void noteRxType(uint8_t type);
  void noteRxDecodeError();
  void noteRxReplayDrop();
  void noteTx(size_t frameBytes, size_t payloadBytes, uint32_t nowMs);
  void noteTxType(uint8_t type);
  void noteTxFailed();

  // One DP snapshot is a burst of DP_CHUNK frames. Bytes here are the unique
  // DP payload bytes, not the LoRa wire bytes (those are in the RX totals).
  void noteDpStart(uint32_t nowMs);
  void noteDpChunk(size_t payloadBytes);
  void noteDpComplete(bool ok, uint32_t nowMs);

  // Time on air for a frame of frameBytes using the current PHY settings.
  float airtimeMs(size_t frameBytes) const;

  void toJson(JsonObject out, uint32_t nowMs) const;

 private:
  static constexpr uint32_t kBurstGapMs = 3000;
  static constexpr uint8_t kDutyBucketCount = 60;
  static constexpr uint32_t kDutyWindowMs = 3600000;

  struct Burst {
    uint32_t startMs = 0;
    uint32_t lastMs = 0;
    uint32_t wireBytes = 0;
    uint32_t payloadBytes = 0;
    uint32_t frames = 0;
    float firstAirtimeMs = 0.0f;
  };

  struct DutyBucket {
    uint32_t minute = 0;
    uint32_t rxAirtimeMs = 0;
    uint32_t txAirtimeMs = 0;
    bool used = false;
  };

  void addToBurst(Burst &burst, uint32_t nowMs, size_t wireBytes,
                  size_t payloadBytes, float airtimeMs);
  void addDutyAirtime(uint32_t nowMs, float airtimeMs, bool tx);

  bool _configured = false;
  uint8_t _sf = 7;
  uint32_t _bandwidthHz = 125000;
  uint8_t _codingRate = 5;
  uint16_t _preamble = 8;
  int8_t _txPowerDbm = 17;
  float _frequencyMhz = 868.1f;

  bool _started = false;
  uint32_t _startedMs = 0;

  uint32_t _rxFrames = 0;
  uint32_t _txFrames = 0;
  uint32_t _rxBytes = 0;
  uint32_t _txBytes = 0;
  uint32_t _rxPayloadBytes = 0;
  uint32_t _txPayloadBytes = 0;
  float _rxAirtimeMs = 0.0f;
  float _txAirtimeMs = 0.0f;

  bool _haveRx = false;
  uint32_t _lastRxMs = 0;
  uint32_t _lastRxFrameBytes = 0;
  uint32_t _lastRxPayloadBytes = 0;
  float _lastRxAirtimeMs = 0.0f;
  int _lastRssi = 0;
  float _lastSnr = 0.0f;

  bool _haveTx = false;
  uint32_t _lastTxMs = 0;
  uint32_t _lastTxFrameBytes = 0;
  uint32_t _lastTxPayloadBytes = 0;
  float _lastTxAirtimeMs = 0.0f;

  uint32_t _rxDecodeErrors = 0;
  uint32_t _rxReplayDrops = 0;
  uint32_t _txFailures = 0;
  uint32_t _rxTypes[9] = {0};
  uint32_t _txTypes[9] = {0};

  Burst _rxBurst;
  Burst _txBurst;
  DutyBucket _duty[kDutyBucketCount];

  bool _dpActive = false;
  uint32_t _dpStartMs = 0;
  uint32_t _dpBytes = 0;
  uint16_t _dpChunks = 0;
  uint32_t _dpSnapshots = 0;
  uint32_t _dpFailures = 0;
  bool _haveDp = false;
  uint32_t _lastDpMs = 0;
  uint32_t _lastDpBytes = 0;
  uint16_t _lastDpChunks = 0;
  uint32_t _lastDpAtMs = 0;
};

}  // namespace tuya_lora
