#pragma once

#include <Arduino.h>

namespace tuya_lora {

// ---------------------------------------------------------------------------
// Wire format constants
// ---------------------------------------------------------------------------
constexpr uint8_t  LORA_MAGIC   = 0xF1;
constexpr uint8_t  LORA_VERSION = 0x01;
constexpr size_t   LORA_MAX_PAYLOAD = 96;
constexpr size_t   LORA_HEADER_LEN  = 13;   // magic..boot_id
constexpr size_t   LORA_TAG_LEN     = 16;
constexpr size_t   LORA_MAX_FRAME   = LORA_HEADER_LEN + LORA_MAX_PAYLOAD + LORA_TAG_LEN;

// DP snapshot transport. The charger returns the raw decrypted Tuya DP JSON
// in bounded chunks; the base acknowledges a 32-bit bitmap and asks for any
// missing chunks. The charger stays a dumb TCP/DP proxy; the base owns the DP
// map, MQTT, and Home Assistant publication.
constexpr size_t   DP_CHUNK_DATA_MAX = 80;
constexpr size_t   DP_MAX_CHUNKS     = 32;
constexpr size_t   DP_MAX_JSON       = 1024;

enum MsgType : uint8_t {
  MSG_HELLO    = 0x01,
  MSG_ACK      = 0x02,
  MSG_LOG      = 0x03,
  MSG_DP_QUERY = 0x04,  // base -> charger: request a raw DP snapshot
  MSG_DP_CHUNK = 0x05,  // charger -> base: one chunk of the snapshot
  MSG_DP_ACK   = 0x06,  // base -> charger: bitmap of received chunks
  MSG_DP_SET   = 0x07,  // base -> charger: raw dps JSON fragment for CONTROL
};

enum Direction : uint8_t {
  DIR_REMOTE_TO_BASE = 0x01,
  DIR_BASE_TO_REMOTE = 0x02,
};

enum StatusCode : uint8_t {
  ST_UNKNOWN   = 0,
  ST_AVAILABLE = 1,
  ST_PLUGGED   = 2,
  ST_CHARGING  = 3,
  ST_WAITING   = 4,
  ST_CHARGED   = 5,
  ST_FAULT     = 6,
};

enum AckResult : uint8_t {
  ACK_OK            = 0,
  ACK_REJECTED      = 1,
  ACK_INVALID       = 2,
  ACK_BUSY          = 3,
  ACK_HARDWARE_ERR  = 4,
};

// ---------------------------------------------------------------------------
// Base-local decoded state
// ---------------------------------------------------------------------------
// This is not a LoRa wire payload. The charger sends raw Tuya DP JSON; the
// base maps it into this struct and publishes the MQTT/HA state from it.
struct StatusData {
  uint8_t  status        = ST_UNKNOWN;
  uint16_t faultFlags    = 0;
  uint8_t  currentLimit  = 0;
  uint16_t voltageL1dV   = 0;
  uint16_t voltageL2dV   = 0;
  uint16_t voltageL3dV   = 0;
  uint16_t currentL1dA   = 0;
  uint16_t currentL2dA   = 0;
  uint16_t currentL3dA   = 0;
  uint16_t powerW        = 0;
  uint32_t energyWh      = 0;
  uint32_t sessionWh     = 0;
  int16_t  tempDc        = 0;
  uint32_t chargeSeconds = 0;
  bool     rfidActive    = false;
  bool     scheduleActive = false;
  bool     chargeEnabled = false;       // raw DP 124 charge-control value
  bool     waitingForRfid = false;      // charger reports wait_rfid

  bool plugged() const {
    return status == ST_PLUGGED || status == ST_CHARGING ||
           status == ST_WAITING || status == ST_CHARGED;
  }
  bool charging() const { return status == ST_CHARGING; }

  // Session-oriented state for the HA charge switch. DP 124 can stay
  // OpenCharging from a previous session, so it is ignored while the charger
  // is waiting for RFID authorization.
  bool chargeSessionEnabled() const {
    if (status == ST_AVAILABLE || status == ST_CHARGED ||
        status == ST_FAULT || status == ST_UNKNOWN) {
      return false;
    }
    if (status == ST_WAITING && waitingForRfid) return false;
    return chargeEnabled;
  }
};

struct HelloData {
  uint8_t  role      = 0;
  uint8_t  fwVersion = 0;
  uint32_t bootId    = 0;
};

struct AckData {
  uint8_t  result   = ACK_INVALID;
  uint32_t ackedSeq = 0;
  uint16_t detail   = 0;
};

const char *statusName(uint8_t status);

bool   decodeHello(const uint8_t *payload, size_t len, HelloData &out);
size_t encodeHello(const HelloData &in, uint8_t *out, size_t maxLen);

bool   decodeAck(const uint8_t *payload, size_t len, AckData &out);
size_t encodeAck(const AckData &in, uint8_t *out, size_t maxLen);

// ---------------------------------------------------------------------------
// Secure frame codec (AES-128-GCM only)
// ---------------------------------------------------------------------------
class LoraCodec {
 public:
  bool setKeyHex(const char *hex);
  bool hasKey() const { return _hasKey; }
  bool isZeroKey() const;

  void setBootId(uint32_t id) { _bootId = id; }
  uint32_t bootId() const { return _bootId; }

  // Returns the next outgoing sequence number (starting at 0 after boot).
  uint32_t nextSeq() { return _seq++; }
  uint32_t seq() const { return _seq; }

  bool encode(uint8_t type, uint8_t flags, uint32_t seq,
              const uint8_t *payload, size_t payloadLen, uint8_t direction,
              uint8_t *out, size_t outCapacity, size_t &outLen);

  // Decodes into an internal payload buffer. The pointer stays valid until the
  // next decode() call.
  bool decode(const uint8_t *frame, size_t frameLen, uint8_t expectedDirection,
              uint8_t &type, uint8_t &flags, uint32_t &seq, uint32_t &bootId,
              const uint8_t *&payload, size_t &payloadLen);

 private:
  uint8_t  _key[16] = {0};
  bool     _hasKey = false;
  uint32_t _bootId = 0;
  uint32_t _seq = 0;
  uint8_t  _rxPayload[LORA_MAX_PAYLOAD] = {0};
};

// ---------------------------------------------------------------------------
// Replay protection
// ---------------------------------------------------------------------------
class ReplayGuard {
 public:
  // Returns true for a fresh frame. Duplicate/old sequence numbers return
  // false. A new boot_id is always treated as fresh.
  bool accept(uint32_t bootId, uint32_t seq);

 private:
  struct Entry {
    uint32_t bootId = 0;
    uint32_t highest = 0;
    uint32_t lastMs = 0;
    bool used = false;
  };
  Entry _entries[4];
};

}  // namespace tuya_lora
