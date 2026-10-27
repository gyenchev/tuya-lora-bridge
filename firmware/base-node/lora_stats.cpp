#include "lora_stats.h"

#include <math.h>

namespace tuya_lora {

namespace {
float roundedAirtimeMs(float tsymMs, uint16_t preamble, size_t frameBytes,
                       uint8_t sf, uint8_t codingRate) {
  if (tsymMs <= 0.0f) return 0.0f;

  // Semtech LoRa time-on-air formula, explicit header, CRC enabled.
  // DE (low data rate optimisation) is set when a symbol is longer than 16 ms.
  const bool lowDataRate = tsymMs > 16.0f;
  const int cr = static_cast<int>(codingRate) - 4;  // 1..4
  const int numerator =
      static_cast<int>(8 * frameBytes) - 4 * static_cast<int>(sf) + 28 + 16;
  const int denominator = 4 * (static_cast<int>(sf) - (lowDataRate ? 2 : 0));
  int payloadSymbols = 0;
  if (denominator > 0) {
    payloadSymbols = static_cast<int>(
        ceil(static_cast<float>(numerator) /
             static_cast<float>(denominator))) *
        (cr + 4);
    if (payloadSymbols < 0) payloadSymbols = 0;
  }

  return (preamble + 4.25f + static_cast<float>(payloadSymbols)) * tsymMs;
}
}  // namespace

void LoraStats::configure(uint8_t spreadingFactor, uint32_t bandwidthHz,
                          uint8_t codingRate, uint16_t preamble,
                          int8_t txPowerDbm, float frequencyMhz) {
  _sf = spreadingFactor;
  _bandwidthHz = bandwidthHz;
  _codingRate = codingRate;
  _preamble = preamble;
  _txPowerDbm = txPowerDbm;
  _frequencyMhz = frequencyMhz;
  _configured = true;
}

void LoraStats::reset(uint32_t nowMs) {
  _started = true;
  _startedMs = nowMs;
  _rxFrames = 0;
  _txFrames = 0;
  _rxBytes = 0;
  _txBytes = 0;
  _rxPayloadBytes = 0;
  _txPayloadBytes = 0;
  _rxAirtimeMs = 0.0f;
  _txAirtimeMs = 0.0f;
  _haveRx = false;
  _lastRxMs = 0;
  _lastRxFrameBytes = 0;
  _lastRxPayloadBytes = 0;
  _lastRxAirtimeMs = 0.0f;
  _lastRssi = 0;
  _lastSnr = 0.0f;
  _haveTx = false;
  _lastTxMs = 0;
  _lastTxFrameBytes = 0;
  _lastTxPayloadBytes = 0;
  _lastTxAirtimeMs = 0.0f;
  _rxDecodeErrors = 0;
  _rxReplayDrops = 0;
  _txFailures = 0;
  for (uint8_t i = 0; i < 9; ++i) {
    _rxTypes[i] = 0;
    _txTypes[i] = 0;
  }
  _rxBurst = Burst{};
  _txBurst = Burst{};
  for (uint8_t i = 0; i < kDutyBucketCount; ++i) _duty[i] = DutyBucket{};
  _dpActive = false;
  _dpStartMs = 0;
  _dpBytes = 0;
  _dpChunks = 0;
  _dpSnapshots = 0;
  _dpFailures = 0;
  _haveDp = false;
  _lastDpMs = 0;
  _lastDpBytes = 0;
  _lastDpChunks = 0;
  _lastDpAtMs = 0;
}

float LoraStats::airtimeMs(size_t frameBytes) const {
  if (!_configured || _bandwidthHz == 0 || _sf == 0) return 0.0f;
  const float tsymMs =
      1000.0f * static_cast<float>(1UL << _sf) /
      static_cast<float>(_bandwidthHz);
  return roundedAirtimeMs(tsymMs, _preamble, frameBytes, _sf, _codingRate);
}

void LoraStats::addToBurst(Burst &burst, uint32_t nowMs, size_t wireBytes,
                           size_t payloadBytes, float airtimeMs) {
  if (burst.frames == 0 || (nowMs - burst.lastMs) > kBurstGapMs) {
    burst = Burst{};
    burst.startMs = nowMs;
    burst.firstAirtimeMs = airtimeMs;
  }
  burst.lastMs = nowMs;
  burst.wireBytes += wireBytes;
  burst.payloadBytes += payloadBytes;
  burst.frames++;
}

void LoraStats::addDutyAirtime(uint32_t nowMs, float airtimeMs, bool tx) {
  if (!_started || airtimeMs <= 0.0f) return;
  const uint32_t minute = nowMs / 60000;
  DutyBucket &bucket = _duty[minute % kDutyBucketCount];
  if (!bucket.used || bucket.minute != minute) {
    bucket = DutyBucket{};
    bucket.minute = minute;
    bucket.used = true;
  }
  if (tx) {
    bucket.txAirtimeMs += static_cast<uint32_t>(airtimeMs + 0.5f);
  } else {
    bucket.rxAirtimeMs += static_cast<uint32_t>(airtimeMs + 0.5f);
  }
}

void LoraStats::noteRx(size_t frameBytes, int rssi, float snr,
                       uint32_t nowMs) {
  if (!_started) reset(nowMs);
  const float airtime = airtimeMs(frameBytes);
  _rxFrames++;
  _rxBytes += frameBytes;
  _rxAirtimeMs += airtime;
  _haveRx = true;
  _lastRxMs = nowMs;
  _lastRxFrameBytes = frameBytes;
  _lastRxPayloadBytes = 0;
  _lastRxAirtimeMs = airtime;
  _lastRssi = rssi;
  _lastSnr = snr;
  addToBurst(_rxBurst, nowMs, frameBytes, 0, airtime);
  addDutyAirtime(nowMs, airtime, false);
}

void LoraStats::noteRxPayload(size_t payloadBytes) {
  _rxPayloadBytes += payloadBytes;
  _lastRxPayloadBytes = payloadBytes;
  _rxBurst.payloadBytes += payloadBytes;
}

void LoraStats::noteRxType(uint8_t type) {
  _rxTypes[type < 8 ? type : 8]++;
}

void LoraStats::noteRxDecodeError() { _rxDecodeErrors++; }

void LoraStats::noteRxReplayDrop() { _rxReplayDrops++; }

void LoraStats::noteTx(size_t frameBytes, size_t payloadBytes,
                       uint32_t nowMs) {
  if (!_started) reset(nowMs);
  const float airtime = airtimeMs(frameBytes);
  _txFrames++;
  _txBytes += frameBytes;
  _txPayloadBytes += payloadBytes;
  _txAirtimeMs += airtime;
  _haveTx = true;
  _lastTxMs = nowMs;
  _lastTxFrameBytes = frameBytes;
  _lastTxPayloadBytes = payloadBytes;
  _lastTxAirtimeMs = airtime;
  addToBurst(_txBurst, nowMs, frameBytes, payloadBytes, airtime);
  addDutyAirtime(nowMs, airtime, true);
}

void LoraStats::noteTxType(uint8_t type) {
  _txTypes[type < 8 ? type : 8]++;
}

void LoraStats::noteTxFailed() { _txFailures++; }

void LoraStats::noteDpStart(uint32_t nowMs) {
  if (!_started) reset(nowMs);
  _dpActive = true;
  _dpStartMs = nowMs;
  _dpBytes = 0;
  _dpChunks = 0;
}

void LoraStats::noteDpChunk(size_t payloadBytes) {
  if (!_dpActive) return;
  _dpBytes += payloadBytes;
  _dpChunks++;
}

void LoraStats::noteDpComplete(bool ok, uint32_t nowMs) {
  if (!_dpActive) return;
  _dpActive = false;
  uint32_t elapsed = nowMs - _dpStartMs;
  if (elapsed == 0) elapsed = 1;
  if (ok) {
    _dpSnapshots++;
    _lastDpMs = elapsed;
    _lastDpBytes = _dpBytes;
    _lastDpChunks = _dpChunks;
    _lastDpAtMs = nowMs;
    _haveDp = true;
  } else {
    _dpFailures++;
  }
}

void LoraStats::toJson(JsonObject out, uint32_t nowMs) const {
  const uint32_t elapsedMs =
      _started && nowMs >= _startedMs ? (nowMs - _startedMs) : 1;
  const uint32_t uptimeMs = elapsedMs ? elapsedMs : 1;
  const float uptimeSec = uptimeMs / 1000.0f;

  JsonObject phy = out["phy"].to<JsonObject>();
  phy["frequency_mhz"] = _frequencyMhz;
  phy["spreading_factor"] = _sf;
  phy["bandwidth_hz"] = _bandwidthHz;
  phy["bandwidth_khz"] = _bandwidthHz / 1000.0f;
  phy["coding_rate"] = _codingRate;
  phy["preamble"] = _preamble;
  phy["tx_power_dbm"] = _txPowerDbm;
  const float symbolRate = _bandwidthHz / static_cast<float>(1UL << _sf);
  phy["symbol_ms"] = 1000.0f / symbolRate;
  phy["bitrate_bps"] =
      symbolRate * _sf * (4.0f / static_cast<float>(_codingRate));

  out["uptime_s"] = uptimeSec;

  JsonObject totals = out["totals"].to<JsonObject>();
  totals["rx_frames"] = _rxFrames;
  totals["tx_frames"] = _txFrames;
  totals["rx_bytes"] = _rxBytes;
  totals["tx_bytes"] = _txBytes;
  totals["rx_payload_bytes"] = _rxPayloadBytes;
  totals["tx_payload_bytes"] = _txPayloadBytes;
  totals["rx_airtime_ms"] = _rxAirtimeMs;
  totals["tx_airtime_ms"] = _txAirtimeMs;
  totals["rx_bps"] = _rxBytes * 1000.0f / uptimeMs;
  totals["tx_bps"] = _txBytes * 1000.0f / uptimeMs;
  totals["rx_payload_bps"] = _rxPayloadBytes * 1000.0f / uptimeMs;
  totals["tx_payload_bps"] = _txPayloadBytes * 1000.0f / uptimeMs;
  totals["airtime_percent"] =
      (_rxAirtimeMs + _txAirtimeMs) / uptimeMs * 100.0f;

  const float dutyWindowMs = static_cast<float>(kDutyWindowMs);
  const uint32_t nowMinute = nowMs / 60000;
  uint32_t dutyRxMs = 0;
  uint32_t dutyTxMs = 0;
  for (uint8_t i = 0; i < kDutyBucketCount; ++i) {
    const DutyBucket &bucket = _duty[i];
    if (!bucket.used || bucket.minute > nowMinute) continue;
    if ((nowMinute - bucket.minute) >= kDutyBucketCount) continue;
    dutyRxMs += bucket.rxAirtimeMs;
    dutyTxMs += bucket.txAirtimeMs;
  }
  const float dutyLimitPercent = 1.0f;  // EU 868.0-868.6 MHz reference
  const float dutyRxPercent = dutyRxMs * 100.0f / dutyWindowMs;
  const float dutyTxPercent = dutyTxMs * 100.0f / dutyWindowMs;
  const float dutyTotalPercent =
      (dutyRxMs + dutyTxMs) * 100.0f / dutyWindowMs;
  const float dutyBudgetMs =
      dutyWindowMs * dutyLimitPercent / 100.0f;
  JsonObject duty = out["duty"].to<JsonObject>();
  duty["window_s"] = kDutyWindowMs / 1000;
  duty["observed_s"] = elapsedMs < kDutyWindowMs ? elapsedMs / 1000.0f
                                                 : kDutyWindowMs / 1000.0f;
  duty["limit_percent"] = dutyLimitPercent;
  duty["rx_airtime_ms"] = dutyRxMs;
  duty["tx_airtime_ms"] = dutyTxMs;
  duty["total_airtime_ms"] = dutyRxMs + dutyTxMs;
  duty["rx_percent"] = dutyRxPercent;
  duty["tx_percent"] = dutyTxPercent;
  duty["total_percent"] = dutyTotalPercent;
  duty["budget_ms"] = dutyBudgetMs;
  duty["rx_remaining_ms"] =
      dutyRxMs < dutyBudgetMs ? dutyBudgetMs - dutyRxMs : 0.0f;
  duty["tx_remaining_ms"] =
      dutyTxMs < dutyBudgetMs ? dutyBudgetMs - dutyTxMs : 0.0f;
  duty["ok"] = dutyRxPercent <= dutyLimitPercent &&
               dutyTxPercent <= dutyLimitPercent;

  auto fillBurst = [&](JsonObject target, const Burst &burst) {
    const bool active =
        burst.frames > 0 && (nowMs - burst.lastMs) <= kBurstGapMs;
    uint32_t durationMs = 0;
    if (burst.frames > 0) {
      durationMs = burst.lastMs - burst.startMs;
      durationMs += static_cast<uint32_t>(burst.firstAirtimeMs + 0.5f);
    }
    if (durationMs == 0) durationMs = 1;
    target["active"] = active;
    target["age_ms"] =
        burst.frames > 0 ? static_cast<int32_t>(nowMs - burst.lastMs) : -1;
    target["frames"] = burst.frames;
    target["wire_bytes"] = burst.wireBytes;
    target["payload_bytes"] = burst.payloadBytes;
    target["duration_ms"] = burst.frames > 0 ? durationMs : 0;
    target["wire_bps"] =
        burst.frames > 0 ? burst.wireBytes * 1000.0f / durationMs : 0.0f;
    target["payload_bps"] =
        burst.frames > 0 ? burst.payloadBytes * 1000.0f / durationMs : 0.0f;
  };
  JsonObject rxBurst = out["rx_burst"].to<JsonObject>();
  fillBurst(rxBurst, _rxBurst);
  JsonObject txBurst = out["tx_burst"].to<JsonObject>();
  fillBurst(txBurst, _txBurst);

  JsonObject last = out["last"].to<JsonObject>();
  JsonObject lastRx = last["rx"].to<JsonObject>();
  lastRx["age_ms"] = _haveRx ? static_cast<int32_t>(nowMs - _lastRxMs) : -1;
  lastRx["frame_bytes"] = _lastRxFrameBytes;
  lastRx["payload_bytes"] = _lastRxPayloadBytes;
  lastRx["airtime_ms"] = _lastRxAirtimeMs;
  lastRx["wire_bps"] = _lastRxAirtimeMs > 0.0f
                           ? _lastRxFrameBytes * 1000.0f / _lastRxAirtimeMs
                           : 0.0f;
  lastRx["payload_bps"] = _lastRxAirtimeMs > 0.0f
                              ? _lastRxPayloadBytes * 1000.0f /
                                    _lastRxAirtimeMs
                              : 0.0f;
  lastRx["rssi"] = _lastRssi;
  lastRx["snr"] = _lastSnr;
  JsonObject lastTx = last["tx"].to<JsonObject>();
  lastTx["age_ms"] = _haveTx ? static_cast<int32_t>(nowMs - _lastTxMs) : -1;
  lastTx["frame_bytes"] = _lastTxFrameBytes;
  lastTx["payload_bytes"] = _lastTxPayloadBytes;
  lastTx["airtime_ms"] = _lastTxAirtimeMs;
  lastTx["wire_bps"] = _lastTxAirtimeMs > 0.0f
                           ? _lastTxFrameBytes * 1000.0f / _lastTxAirtimeMs
                           : 0.0f;
  lastTx["payload_bps"] = _lastTxAirtimeMs > 0.0f
                              ? _lastTxPayloadBytes * 1000.0f /
                                    _lastTxAirtimeMs
                              : 0.0f;

  JsonObject dp = out["dp"].to<JsonObject>();
  dp["active"] = _dpActive;
  dp["current_bytes"] = _dpActive ? _dpBytes : 0;
  dp["current_chunks"] = _dpActive ? _dpChunks : 0;
  dp["snapshots"] = _dpSnapshots;
  dp["failures"] = _dpFailures;
  dp["last_bytes"] = _lastDpBytes;
  dp["last_chunks"] = _lastDpChunks;
  dp["last_ms"] = _lastDpMs;
  dp["last_bps"] =
      _lastDpMs ? _lastDpBytes * 1000.0f / _lastDpMs : 0.0f;
  dp["last_age_ms"] =
      _haveDp ? static_cast<int32_t>(nowMs - _lastDpAtMs) : -1;

  JsonObject errors = out["errors"].to<JsonObject>();
  errors["rx_decode"] = _rxDecodeErrors;
  errors["rx_replay"] = _rxReplayDrops;
  errors["tx"] = _txFailures;

  static const char *kTypeNames[9] = {"none", "hello", "ack", "log",
                                      "dp_query", "dp_chunk", "dp_ack",
                                      "dp_set", "other"};
  JsonObject types = out["types"].to<JsonObject>();
  JsonObject rxTypes = types["rx"].to<JsonObject>();
  JsonObject txTypes = types["tx"].to<JsonObject>();
  for (uint8_t i = 0; i < 9; ++i) {
    if (_rxTypes[i] > 0) rxTypes[kTypeNames[i]] = _rxTypes[i];
    if (_txTypes[i] > 0) txTypes[kTypeNames[i]] = _txTypes[i];
  }
}

}  // namespace tuya_lora
