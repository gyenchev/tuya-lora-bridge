#include "lora_protocol.h"

#include <mbedtls/gcm.h>
#include <string.h>

namespace tuya_lora {
namespace {

uint16_t get16(const uint8_t *p) {
  return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}

uint32_t get32(const uint8_t *p) {
  return (static_cast<uint32_t>(p[0]) << 24) |
         (static_cast<uint32_t>(p[1]) << 16) |
         (static_cast<uint32_t>(p[2]) << 8) |
         static_cast<uint32_t>(p[3]);
}

void put16(uint8_t *p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v >> 8);
  p[1] = static_cast<uint8_t>(v);
}

void put32(uint8_t *p, uint32_t v) {
  p[0] = static_cast<uint8_t>(v >> 24);
  p[1] = static_cast<uint8_t>(v >> 16);
  p[2] = static_cast<uint8_t>(v >> 8);
  p[3] = static_cast<uint8_t>(v);
}

int hexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

}  // namespace

const char *statusName(uint8_t status) {
  switch (status) {
    case ST_AVAILABLE: return "available";
    case ST_PLUGGED:   return "plugged_in";
    case ST_CHARGING:  return "charging";
    case ST_WAITING:   return "waiting";
    case ST_CHARGED:   return "charged";
    case ST_FAULT:     return "fault";
    default:           return "unknown";
  }
}

bool decodeHello(const uint8_t *payload, size_t len, HelloData &out) {
  if (!payload || len < 6) return false;
  out.role = payload[0];
  out.fwVersion = payload[1];
  out.bootId = get32(payload + 2);
  return true;
}

size_t encodeHello(const HelloData &in, uint8_t *out, size_t maxLen) {
  if (!out || maxLen < 6) return 0;
  out[0] = in.role;
  out[1] = in.fwVersion;
  put32(out + 2, in.bootId);
  return 6;
}

bool decodeAck(const uint8_t *payload, size_t len, AckData &out) {
  if (!payload || len < 7) return false;
  out.result = payload[0];
  out.ackedSeq = get32(payload + 1);
  out.detail = get16(payload + 5);
  return true;
}

size_t encodeAck(const AckData &in, uint8_t *out, size_t maxLen) {
  if (!out || maxLen < 7) return 0;
  out[0] = in.result;
  put32(out + 1, in.ackedSeq);
  put16(out + 5, in.detail);
  return 7;
}

bool decodeConfigSet(const uint8_t *payload, size_t len, uint32_t &configId,
                     LoraConfig &out) {
  if (!payload || len < 51 || payload[0] != CONFIG_PROTOCOL_VERSION) {
    return false;
  }
  configId = get32(payload + 1);
  out.frequencyMhz = get32(payload + 5) / 1000000.0f;
  out.bandwidthHz = get32(payload + 9);
  out.sf = payload[13];
  out.codingRate = payload[14];
  out.preamble = get16(payload + 15);
  out.syncWord = payload[17];
  out.txPower = static_cast<int8_t>(payload[18]);
  memcpy(out.psk, payload + 19, 32);
  out.psk[32] = '\0';
  return true;
}

size_t encodeConfigSet(uint32_t configId, const LoraConfig &in, uint8_t *out,
                       size_t maxLen) {
  if (!out || maxLen < 51) return 0;
  out[0] = CONFIG_PROTOCOL_VERSION;
  put32(out + 1, configId);
  put32(out + 5,
        static_cast<uint32_t>(in.frequencyMhz * 1000000.0f + 0.5f));
  put32(out + 9, in.bandwidthHz);
  out[13] = in.sf;
  out[14] = in.codingRate;
  put16(out + 15, in.preamble);
  out[17] = in.syncWord;
  out[18] = static_cast<uint8_t>(static_cast<int8_t>(in.txPower));
  memset(out + 19, 0, 32);
  const size_t pskLen = strlen(in.psk);
  memcpy(out + 19, in.psk, pskLen < 32 ? pskLen : 32);
  return 51;
}

bool decodeConfigAck(const uint8_t *payload, size_t len, uint32_t &configId,
                     ConfigAckData &out) {
  if (!payload || len < 8 || payload[0] != CONFIG_PROTOCOL_VERSION) {
    return false;
  }
  configId = get32(payload + 1);
  out.result = payload[5];
  out.detail = get16(payload + 6);
  return true;
}

size_t encodeConfigAck(uint32_t configId, const ConfigAckData &in, uint8_t *out,
                       size_t maxLen) {
  if (!out || maxLen < 8) return 0;
  out[0] = CONFIG_PROTOCOL_VERSION;
  put32(out + 1, configId);
  out[5] = in.result;
  put16(out + 6, in.detail);
  return 8;
}

bool decodeConfigId(const uint8_t *payload, size_t len, uint32_t &configId) {
  if (!payload || len < 5 || payload[0] != CONFIG_PROTOCOL_VERSION) {
    return false;
  }
  configId = get32(payload + 1);
  return true;
}

size_t encodeConfigId(uint32_t configId, uint8_t *out, size_t maxLen) {
  if (!out || maxLen < 5) return 0;
  out[0] = CONFIG_PROTOCOL_VERSION;
  put32(out + 1, configId);
  return 5;
}

bool LoraCodec::setKeyHex(const char *hex) {
  if (!hex || strlen(hex) != 32) {
    _hasKey = false;
    return false;
  }

  uint8_t key[16];
  for (size_t i = 0; i < 16; ++i) {
    int hi = hexNibble(hex[i * 2]);
    int lo = hexNibble(hex[i * 2 + 1]);
    if (hi < 0 || lo < 0) {
      _hasKey = false;
      return false;
    }
    key[i] = static_cast<uint8_t>((hi << 4) | lo);
  }
  memcpy(_key, key, sizeof(_key));
  _hasKey = true;
  return true;
}

bool LoraCodec::isZeroKey() const {
  if (!_hasKey) return true;
  for (size_t i = 0; i < sizeof(_key); ++i) {
    if (_key[i] != 0) return false;
  }
  return true;
}

bool LoraCodec::encode(uint8_t type, uint8_t flags, uint32_t seq,
                       const uint8_t *payload, size_t payloadLen,
                       uint8_t direction, uint8_t *out, size_t outCapacity,
                       size_t &outLen) {
  if (!out || payloadLen > LORA_MAX_PAYLOAD) return false;

  const size_t total = LORA_HEADER_LEN + payloadLen + LORA_TAG_LEN;
  if (outCapacity < total) return false;

  out[0] = LORA_MAGIC;
  out[1] = LORA_VERSION;
  out[2] = type;
  out[3] = flags;
  put32(out + 4, seq);
  out[8] = static_cast<uint8_t>(payloadLen);
  put32(out + 9, _bootId);

  uint8_t aad[LORA_HEADER_LEN];
  memcpy(aad, out, sizeof(aad));

  uint8_t nonce[12] = {0};
  put32(nonce, _bootId);
  put32(nonce + 4, seq);
  nonce[8] = direction;

  mbedtls_gcm_context ctx;
  mbedtls_gcm_init(&ctx);
  if (mbedtls_gcm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, _key, 128) != 0) {
    mbedtls_gcm_free(&ctx);
    return false;
  }

  int rc = mbedtls_gcm_crypt_and_tag(
      &ctx, MBEDTLS_GCM_ENCRYPT, payloadLen, nonce, sizeof(nonce), aad,
      sizeof(aad), payload, out + LORA_HEADER_LEN, LORA_TAG_LEN,
      out + LORA_HEADER_LEN + payloadLen);
  mbedtls_gcm_free(&ctx);
  if (rc != 0) return false;

  outLen = total;
  return true;
}

bool LoraCodec::decode(const uint8_t *frame, size_t frameLen,
                       uint8_t expectedDirection, uint8_t &type,
                       uint8_t &flags, uint32_t &seq, uint32_t &bootId,
                       const uint8_t *&payload, size_t &payloadLen) {
  if (!frame || frameLen < LORA_HEADER_LEN + LORA_TAG_LEN) return false;
  if (frame[0] != LORA_MAGIC || frame[1] != LORA_VERSION) return false;

  const size_t n = frame[8];
  if (n > LORA_MAX_PAYLOAD) return false;
  if (frameLen != LORA_HEADER_LEN + n + LORA_TAG_LEN) return false;

  type   = frame[2];
  flags  = frame[3];
  seq    = get32(frame + 4);
  bootId = get32(frame + 9);

  uint8_t aad[LORA_HEADER_LEN];
  memcpy(aad, frame, sizeof(aad));

  uint8_t nonce[12] = {0};
  put32(nonce, bootId);
  put32(nonce + 4, seq);
  nonce[8] = expectedDirection;

  mbedtls_gcm_context ctx;
  mbedtls_gcm_init(&ctx);
  if (mbedtls_gcm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, _key, 128) != 0) {
    mbedtls_gcm_free(&ctx);
    return false;
  }

  int rc = mbedtls_gcm_auth_decrypt(
      &ctx, n, nonce, sizeof(nonce), aad, sizeof(aad),
      frame + LORA_HEADER_LEN + n, LORA_TAG_LEN,
      frame + LORA_HEADER_LEN, _rxPayload);
  mbedtls_gcm_free(&ctx);
  if (rc != 0) return false;

  payload = _rxPayload;
  payloadLen = n;
  return true;
}

bool ReplayGuard::accept(uint32_t bootId, uint32_t seq) {
  const uint32_t now = millis();

  for (size_t i = 0; i < 4; ++i) {
    if (_entries[i].used && _entries[i].bootId == bootId) {
      if (seq <= _entries[i].highest) return false;
      _entries[i].highest = seq;
      _entries[i].lastMs = now;
      return true;
    }
  }

  for (size_t i = 0; i < 4; ++i) {
    if (!_entries[i].used) {
      _entries[i].used = true;
      _entries[i].bootId = bootId;
      _entries[i].highest = seq;
      _entries[i].lastMs = now;
      return true;
    }
  }

  size_t oldest = 0;
  for (size_t i = 1; i < 4; ++i) {
    if (_entries[i].lastMs < _entries[oldest].lastMs) oldest = i;
  }
  _entries[oldest].bootId = bootId;
  _entries[oldest].highest = seq;
  _entries[oldest].lastMs = now;
  return true;
}

}  // namespace tuya_lora
