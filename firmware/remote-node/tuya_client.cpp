#include "tuya_client.h"

#include "config_store.h"

#include <ArduinoJson.h>
#include <esp_system.h>
#include <mbedtls/aes.h>
#include <mbedtls/base64.h>
#include <mbedtls/gcm.h>
#include <mbedtls/md.h>
#include <string.h>

namespace {

constexpr uint32_t TUYA_PREFIX = 0x000055AA;
constexpr uint32_t TUYA_SUFFIX = 0x0000AA55;
constexpr uint32_t TUYA_PREFIX_35 = 0x00006699;
constexpr uint32_t TUYA_SUFFIX_35 = 0x00009966;

constexpr uint32_t CMD_SESS_KEY_NEG_START = 3;
constexpr uint32_t CMD_SESS_KEY_NEG_RESP = 4;
constexpr uint32_t CMD_SESS_KEY_NEG_FINISH = 5;
constexpr uint32_t CMD_CONTROL = 7;
constexpr uint32_t CMD_STATUS = 8;
constexpr uint32_t CMD_HEART_BEAT = 9;
constexpr uint32_t CMD_DP_QUERY = 10;
constexpr uint32_t CMD_CONTROL_NEW = 13;
constexpr uint32_t CMD_DP_QUERY_NEW = 16;

constexpr size_t VERSION_HEADER_LEN = 15;  // "3.x" + 12 zero bytes
constexpr size_t MAX_JSON = 1400;

uint32_t be32(const uint8_t *p) {
  return (static_cast<uint32_t>(p[0]) << 24) |
         (static_cast<uint32_t>(p[1]) << 16) |
         (static_cast<uint32_t>(p[2]) << 8) |
         static_cast<uint32_t>(p[3]);
}

void put32(uint8_t *p, uint32_t v) {
  p[0] = static_cast<uint8_t>(v >> 24);
  p[1] = static_cast<uint8_t>(v >> 16);
  p[2] = static_cast<uint8_t>(v >> 8);
  p[3] = static_cast<uint8_t>(v);
}

uint32_t crc32_ieee(const uint8_t *data, size_t len) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (uint8_t b = 0; b < 8; ++b) {
      crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
  }
  return ~crc;
}

bool aesEcbEncrypt(const uint8_t *key, const uint8_t *in, size_t len,
                   uint8_t *out, size_t &outLen, bool pad) {
  size_t blocks = (len + 15) / 16;
  if (pad && (len % 16) == 0) blocks = len / 16 + 1;
  if (blocks == 0) {
    outLen = 0;
    return true;
  }
  if (blocks * 16 > MAX_JSON) return false;

  uint8_t padded[MAX_JSON];
  memcpy(padded, in, len);
  if (pad) {
    uint8_t p = static_cast<uint8_t>(16 - (len % 16));
    if (p == 0) p = 16;
    memset(padded + len, p, p);
    outLen = len + p;
  } else {
    outLen = blocks * 16;
    if (len % 16) {
      uint8_t p = static_cast<uint8_t>(16 - (len % 16));
      memset(padded + len, 0, p);
    }
  }

  mbedtls_aes_context ctx;
  mbedtls_aes_init(&ctx);
  if (mbedtls_aes_setkey_enc(&ctx, key, 128) != 0) {
    mbedtls_aes_free(&ctx);
    return false;
  }
  for (size_t i = 0; i < outLen; i += 16) {
    if (mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_ENCRYPT, padded + i, out + i) != 0) {
      mbedtls_aes_free(&ctx);
      return false;
    }
  }
  mbedtls_aes_free(&ctx);
  return true;
}

bool aesEcbDecrypt(const uint8_t *key, const uint8_t *in, size_t len,
                   uint8_t *out, size_t &outLen) {
  if (len == 0 || (len % 16) != 0 || len > MAX_JSON) return false;

  mbedtls_aes_context ctx;
  mbedtls_aes_init(&ctx);
  if (mbedtls_aes_setkey_dec(&ctx, key, 128) != 0) {
    mbedtls_aes_free(&ctx);
    return false;
  }
  for (size_t i = 0; i < len; i += 16) {
    if (mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_DECRYPT, in + i, out + i) != 0) {
      mbedtls_aes_free(&ctx);
      return false;
    }
  }
  mbedtls_aes_free(&ctx);

  // PKCS#7 unpad. Be lenient: Tuya devices are not always consistent.
  uint8_t p = out[len - 1];
  if (p >= 1 && p <= 16 && p <= len) {
    outLen = len - p;
  } else {
    outLen = len;
  }
  return true;
}

bool aesGcmEncrypt(const uint8_t *key, const uint8_t *iv, size_t ivLen,
                   const uint8_t *aad, size_t aadLen, const uint8_t *in,
                   size_t len, uint8_t *out, size_t &outLen) {
  outLen = ivLen + len + 16;
  if (outLen > MAX_JSON) return false;
  memcpy(out, iv, ivLen);

  mbedtls_gcm_context ctx;
  mbedtls_gcm_init(&ctx);
  if (mbedtls_gcm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, key, 128) != 0) {
    mbedtls_gcm_free(&ctx);
    return false;
  }
  int rc = mbedtls_gcm_crypt_and_tag(
      &ctx, MBEDTLS_GCM_ENCRYPT, len, iv, ivLen, aad, aadLen, in, out + ivLen,
      16, out + ivLen + len);
  mbedtls_gcm_free(&ctx);
  return rc == 0;
}

bool aesGcmDecrypt(const uint8_t *key, const uint8_t *iv, size_t ivLen,
                   const uint8_t *aad, size_t aadLen, const uint8_t *in,
                   size_t len, const uint8_t *tag, uint8_t *out) {
  mbedtls_gcm_context ctx;
  mbedtls_gcm_init(&ctx);
  if (mbedtls_gcm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, key, 128) != 0) {
    mbedtls_gcm_free(&ctx);
    return false;
  }
  int rc = mbedtls_gcm_auth_decrypt(&ctx, len, iv, ivLen, aad, aadLen, tag, 16,
                                    in, out);
  mbedtls_gcm_free(&ctx);
  return rc == 0;
}

void hmacSha256(const uint8_t *key, size_t keyLen, const uint8_t *data,
                size_t len, uint8_t out[32]) {
  mbedtls_md_context_t ctx;
  mbedtls_md_init(&ctx);
  const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (info && mbedtls_md_setup(&ctx, info, 1) == 0) {
    mbedtls_md_hmac_starts(&ctx, key, keyLen);
    mbedtls_md_hmac_update(&ctx, data, len);
    mbedtls_md_hmac_finish(&ctx, out);
  }
  mbedtls_md_free(&ctx);
}

void fillRandom(uint8_t *dst, size_t len) {
  for (size_t i = 0; i < len; i += 4) {
    uint32_t r = esp_random();
    size_t n = (len - i < 4) ? (len - i) : 4;
    memcpy(dst + i, &r, n);
  }
}

bool looksLikeJson(const uint8_t *data, size_t len) {
  return len > 0 && data[0] == '{';
}

}  // namespace

TuyaClient::TuyaClient() {
  memset(_rxBuf, 0, sizeof(_rxBuf));
  memset(_gcmBuf, 0, sizeof(_gcmBuf));
  memset(_decBuf, 0, sizeof(_decBuf));
}

void TuyaClient::setCredentials(const char *deviceId, const char *localKey,
                                float version) {
  _deviceId = deviceId ? deviceId : "";
  _localKey = localKey ? localKey : "";
  if (_deviceId.startsWith("replace")) _deviceId = "";
  if (_localKey.startsWith("replace")) _localKey = "";
  _keyLen = _localKey.length() == 16 ? 16 : 0;
  if (_keyLen) memcpy(_key, _localKey.c_str(), 16);
  _autoDetect = version <= 0.0f;
  _version = _autoDetect ? 3.3f : version;
  _lastError = "";
}

bool TuyaClient::canQuery() const {
  if (_deviceId.length() == 0) return false;
  if (_autoDetect) return true;
  if (_version < 3.2f) return true;  // 3.1 DP_QUERY is plaintext
  return _keyLen == 16;
}

String TuyaClient::versionText() const {
  if (_version >= 3.45f) return "3.5";
  if (_version >= 3.35f) return "3.4";
  if (_version >= 3.15f) return "3.3";
  return "3.1";
}

bool TuyaClient::connect(IPAddress ip, uint16_t port) {
  disconnect();
  _ip = ip;
  if (!canQuery()) {
    _setError("device ID/local key missing");
    return false;
  }

  if (!_autoDetect) {
    return _connectOnce(ip, port);
  }

  // Try the most common version first. Each attempt gets its own TCP session.
  const float candidates[] = {3.3f, 3.4f, 3.5f, 3.1f};
  for (float v : candidates) {
    if (!_tryVersion(ip, port, v)) continue;
    // _tryVersion leaves the socket open on success.
    return true;
  }
  _setError("no Tuya version worked");
  return false;
}

bool TuyaClient::_tryVersion(IPAddress ip, uint16_t port, float version) {
  _version = version;
  if (!_connectOnce(ip, port)) return false;

  if (_queryOnce()) {
    _lastError = "";
    return true;
  }
  disconnect();
  return false;
}

bool TuyaClient::_connectOnce(IPAddress ip, uint16_t port) {
  disconnect();
  _ip = ip;
  _seqno = 1;
  _sessionKey[0] = 0;  // mark "not negotiated"

  _tcp.setTimeout(tuya_lora::settings.tuyaConnectTimeoutMs);
  if (!_tcp.connect(ip, port, tuya_lora::settings.tuyaConnectTimeoutMs)) {
    _setError("TCP connect failed");
    return false;
  }
  _tcp.setNoDelay(true);
  _tcp.setTimeout(tuya_lora::settings.tuyaReadTimeoutMs);

  if (_version >= 3.4f) {
    if (!_negotiate()) {
      disconnect();
      return false;
    }
  }
  return true;
}

void TuyaClient::disconnect() {
  if (_tcp.connected()) _tcp.stop();
  _tcp.stop();
}

bool TuyaClient::query() {
  if (!_tcp.connected()) {
    _setError("not connected");
    return false;
  }
  if (!_queryOnce()) return false;
  _lastError = "";
  return true;
}

bool TuyaClient::setDps(const String &dpsJson) {
  if (!_tcp.connected()) {
    _setError("not connected");
    return false;
  }
  if (dpsJson.length() < 2 || dpsJson[0] != '{' ||
      dpsJson[dpsJson.length() - 1] != '}') {
    _setError("invalid dps json");
    return false;
  }

  uint32_t now = static_cast<uint32_t>(time(nullptr));
  if (now < 1600000000u) now = static_cast<uint32_t>(millis() / 1000);

  String json;
  uint32_t cmd;
  bool useSession = false;

  if (_version >= 3.4f) {
    cmd = CMD_CONTROL_NEW;
    useSession = true;
    json.reserve(160 + dpsJson.length());
    json = "{\"protocol\":5,\"t\":";
    json += String(now);
    json += ",\"data\":{\"dps\":";
    json += dpsJson;
    json += "}}";
  } else {
    cmd = CMD_CONTROL;
    json.reserve(200 + dpsJson.length());
    json = "{\"devId\":\"";
    json += _deviceId;
    json += "\",\"uid\":\"";
    json += _deviceId;
    json += "\",\"t\":\"";
    json += String(now);
    json += "\",\"dps\":";
    json += dpsJson;
    json += "}";
  }

  if (_version < 3.2f) {
    // Protocol 3.1 CONTROL is special: AES-ECB + base64, prefixed with "3.1"
    // and a 16-character MD5 slice. DP_QUERY is plaintext, which is why the
    // existing query path does not need this.
    if (_keyLen != 16) {
      _setError("local key must be 16 chars");
      return false;
    }

    uint8_t enc[512];
    size_t encLen = 0;
    if (!aesEcbEncrypt(_key,
                       reinterpret_cast<const uint8_t *>(json.c_str()),
                       json.length(), enc, encLen, true)) {
      _setError("AES encrypt failed");
      return false;
    }

    uint8_t b64[768];
    size_t b64Len = 0;
    if (mbedtls_base64_encode(b64, sizeof(b64), &b64Len, enc, encLen) != 0) {
      _setError("base64 encode failed");
      return false;
    }

    String md5Input;
    md5Input.reserve(32 + b64Len + _keyLen);
    md5Input = "data=";
    md5Input.concat(reinterpret_cast<const char *>(b64), b64Len);
    md5Input += "||lpv=3.1||";
    md5Input.concat(reinterpret_cast<const char *>(_key), _keyLen);

    uint8_t digest[16];
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_MD5);
    if (!info ||
        mbedtls_md(info,
                   reinterpret_cast<const unsigned char *>(md5Input.c_str()),
                   md5Input.length(), digest) != 0) {
      _setError("MD5 failed");
      return false;
    }

    char hex[33];
    for (size_t i = 0; i < 16; ++i) {
      snprintf(hex + i * 2, 3, "%02x", digest[i]);
    }
    hex[32] = '\0';

    String payload;
    payload.reserve(3 + 16 + b64Len);
    payload = "3.1";
    payload.concat(hex + 8, 16);
    payload.concat(reinterpret_cast<const char *>(b64), b64Len);

    if (!_sendFrame(CMD_CONTROL,
                    reinterpret_cast<const uint8_t *>(payload.c_str()),
                    payload.length(), false, false)) {
      return false;
    }
  } else {
    // CONTROL/CONTROL_NEW carries the 3.x version header inside the encrypted
    // plaintext (3.4/3.5) or in clear before the ciphertext (3.2/3.3).
    if (!_sendFrame(cmd, reinterpret_cast<const uint8_t *>(json.c_str()),
                    json.length(), useSession, true)) {
      return false;
    }
  }

  uint32_t deadline = millis() + tuya_lora::settings.tuyaReadTimeoutMs;
  while (static_cast<int32_t>(deadline - millis()) > 0) {
    Frame frame;
    uint32_t remaining = deadline - millis();
    if (!_readFrame(frame, remaining)) return false;

    // Ignore unrelated frames (heartbeat replies, unsolicited telemetry) until
    // the control ACK or a status echo arrives.
    if (frame.cmd != cmd && frame.cmd != CMD_STATUS &&
        frame.cmd != CMD_CONTROL && frame.cmd != CMD_CONTROL_NEW) {
      continue;
    }
    if (frame.retcode != 0) {
      _setError(String("control retcode ") + String(frame.retcode));
      return false;
    }
    // An empty frame is the normal control ACK. Some devices echo the updated
    // dps; parse it if present so the cached snapshot stays fresh.
    if (frame.payloadLen > 0) _parseFrame(frame);
    _lastError = "";
    return true;
  }

  _setError("no control response");
  return false;
}

void TuyaClient::poll() {
  if (!_tcp.connected()) return;

  // Read at most a few frames per call so a chatty device cannot monopolise
  // the main loop.
  for (uint8_t i = 0; i < 4; ++i) {
    if (_tcp.available() <= 0) break;
    Frame frame;
    if (!_readFrame(frame, 100)) {
      disconnect();
      return;
    }
    if (frame.payloadLen > 0) _parseFrame(frame);
  }
}

const uint8_t *TuyaClient::_frameKey() const {
  if (_version >= 3.4f && !_negotiating) return _sessionKey;
  return _key;
}

bool TuyaClient::_sendFrame(uint32_t cmd, const uint8_t *plain, size_t plainLen,
                            bool useSessionKey, bool addVersionHeader) {
  if (plainLen > 1200) return false;

  uint8_t work[1400];
  size_t workLen = 0;
  const uint8_t *key = (useSessionKey && !_negotiating) ? _sessionKey : _key;

  if (_version >= 3.5f) {
    if (addVersionHeader) {
      work[0] = '3';
      work[1] = '.';
      work[2] = '5';
      memset(work + 3, 0, 12);
      memcpy(work + VERSION_HEADER_LEN, plain, plainLen);
      workLen = VERSION_HEADER_LEN + plainLen;
    } else {
      memcpy(work, plain, plainLen);
      workLen = plainLen;
    }

    uint8_t enc[1400];
    size_t encLen = 0;
    uint8_t iv[12];
    fillRandom(iv, sizeof(iv));

    uint8_t frame[1500];
    uint32_t msgLen = static_cast<uint32_t>(workLen + 28);
    put32(frame + 0, TUYA_PREFIX_35);
    frame[4] = 0;
    frame[5] = 0;
    put32(frame + 6, _seqno++);
    put32(frame + 10, cmd);
    put32(frame + 14, msgLen);

    // 3.5 authenticates the header tail (unknown, seq, cmd, length) as AAD.
    if (!aesGcmEncrypt(key, iv, sizeof(iv), frame + 4, 14, work, workLen, enc,
                       encLen)) {
      _setError("GCM encrypt failed");
      return false;
    }

    memcpy(frame + 18, enc, encLen);
    put32(frame + 18 + encLen, TUYA_SUFFIX_35);
    return _tcp.write(frame, 18 + encLen + 4) == (18 + encLen + 4);
  }

  if (_version >= 3.4f) {
    if (addVersionHeader) {
      work[0] = '3';
      work[1] = '.';
      work[2] = '4';
      memset(work + 3, 0, 12);
      memcpy(work + VERSION_HEADER_LEN, plain, plainLen);
      workLen = VERSION_HEADER_LEN + plainLen;
    } else {
      memcpy(work, plain, plainLen);
      workLen = plainLen;
    }

    uint8_t enc[1400];
    size_t encLen = 0;
    if (!aesEcbEncrypt(key, work, workLen, enc, encLen, true)) {
      _setError("AES encrypt failed");
      return false;
    }

    uint8_t frame[1500];
    uint32_t msgLen = static_cast<uint32_t>(encLen + 36);
    put32(frame + 0, TUYA_PREFIX);
    put32(frame + 4, _seqno++);
    put32(frame + 8, cmd);
    put32(frame + 12, msgLen);
    memcpy(frame + 16, enc, encLen);

    uint8_t mac[32];
    hmacSha256(key, 16, frame, 16 + encLen, mac);
    memcpy(frame + 16 + encLen, mac, 32);
    put32(frame + 16 + encLen + 32, TUYA_SUFFIX);
    return _tcp.write(frame, 16 + encLen + 36) == (16 + encLen + 36);
  }

  if (_version >= 3.2f) {
    uint8_t enc[1400];
    size_t encLen = 0;
    if (!aesEcbEncrypt(key, plain, plainLen, enc, encLen, true)) {
      _setError("AES encrypt failed");
      return false;
    }

    uint8_t frame[1500];
    const size_t headerLen = addVersionHeader ? VERSION_HEADER_LEN : 0;
    put32(frame + 0, TUYA_PREFIX);
    put32(frame + 4, _seqno++);
    put32(frame + 8, cmd);
    put32(frame + 12, static_cast<uint32_t>(headerLen + encLen + 8));
    if (addVersionHeader) {
      frame[16] = '3';
      frame[17] = '.';
      frame[18] = (_version >= 3.3f) ? '3' : '2';
      memset(frame + 19, 0, 12);
    }
    memcpy(frame + 16 + headerLen, enc, encLen);
    uint32_t crc = crc32_ieee(frame, 16 + headerLen + encLen);
    put32(frame + 16 + headerLen + encLen, crc);
    put32(frame + 16 + headerLen + encLen + 4, TUYA_SUFFIX);
    size_t total = 16 + headerLen + encLen + 8;
    return _tcp.write(frame, total) == total;
  }

  // 3.1
  uint8_t frame[1500];
  put32(frame + 0, TUYA_PREFIX);
  put32(frame + 4, _seqno++);
  put32(frame + 8, cmd);
  put32(frame + 12, static_cast<uint32_t>(plainLen + 8));
  memcpy(frame + 16, plain, plainLen);
  uint32_t crc = crc32_ieee(frame, 16 + plainLen);
  put32(frame + 16 + plainLen, crc);
  put32(frame + 16 + plainLen + 4, TUYA_SUFFIX);
  size_t total = 16 + plainLen + 8;
  return _tcp.write(frame, total) == total;
}

bool TuyaClient::_readExact(uint8_t *dst, size_t len, uint32_t timeoutMs) {
  size_t got = 0;
  uint32_t start = millis();
  while (got < len) {
    if (millis() - start > timeoutMs) return false;
    int n = _tcp.read(dst + got, len - got);
    if (n > 0) {
      got += n;
      start = millis();
    } else if (n < 0) {
      return false;
    } else {
      delay(1);
    }
  }
  return true;
}

bool TuyaClient::_readFrame(Frame &frame, uint32_t timeoutMs) {
  uint8_t *buf = _rxBuf;
  if (!_readExact(buf, 4, timeoutMs)) return false;

  uint32_t prefix = be32(buf);
  size_t headerLen = 16;
  size_t total = 0;

  if (prefix == TUYA_PREFIX) {
    if (!_readExact(buf + 4, 12, timeoutMs)) return false;
    uint32_t len = be32(buf + 12);
    total = 16 + len;
  } else if (prefix == TUYA_PREFIX_35) {
    headerLen = 18;
    if (!_readExact(buf + 4, 14, timeoutMs)) return false;
    uint32_t len = be32(buf + 14);
    total = 18 + len + 4;
  } else {
    _setError("bad Tuya frame prefix");
    return false;
  }

  if (total < headerLen + 4 || total > sizeof(_rxBuf)) {
    _setError("bad Tuya frame length");
    return false;
  }
  if (!_readExact(buf + headerLen, total - headerLen, timeoutMs)) return false;

  frame.seq = be32(buf + (prefix == TUYA_PREFIX ? 4 : 6));
  frame.cmd = be32(buf + (prefix == TUYA_PREFIX ? 8 : 10));
  frame.payload = nullptr;
  frame.payloadLen = 0;
  frame.retcode = 0;

  if (prefix == TUYA_PREFIX) {
    if (be32(buf + total - 4) != TUYA_SUFFIX) {
      _setError("bad Tuya suffix");
      return false;
    }

    if (_version >= 3.4f) {
      const size_t endLen = 36;
      if (total < headerLen + endLen + 4) {
        _setError("short HMAC frame");
        return false;
      }
      const size_t payloadArea = total - headerLen - endLen;
      uint8_t mac[32];
      hmacSha256(_frameKey(), 16, buf, total - endLen, mac);
      if (memcmp(mac, buf + total - endLen, 32) != 0) {
        _setError("HMAC mismatch");
        return false;
      }
      frame.retcode = be32(buf + headerLen);
      frame.payload = buf + headerLen + 4;
      frame.payloadLen = payloadArea - 4;
    } else {
      const size_t endLen = 8;
      if (total < headerLen + endLen + 4) {
        _setError("short CRC frame");
        return false;
      }
      const size_t payloadArea = total - headerLen - endLen;
      uint32_t crc = crc32_ieee(buf, total - endLen);
      if (crc != be32(buf + total - endLen)) {
        _setError("CRC mismatch");
        return false;
      }
      frame.retcode = be32(buf + headerLen);
      frame.payload = buf + headerLen + 4;
      frame.payloadLen = payloadArea - 4;
    }
  } else {
    if (be32(buf + total - 4) != TUYA_SUFFIX_35) {
      _setError("bad Tuya suffix");
      return false;
    }
    if (total < headerLen + 12 + 16 + 4) {
      _setError("short GCM frame");
      return false;
    }

    const uint8_t *body = buf + headerLen;
    size_t bodyLen = total - headerLen - 4;
    const uint8_t *iv = body;
    size_t ctLen = bodyLen - 12 - 16;
    const uint8_t *ct = body + 12;
    const uint8_t *tag = body + 12 + ctLen;
    const uint8_t *aad = buf + 4;
    size_t aadLen = headerLen - 4;

    if (ctLen > sizeof(_gcmBuf)) {
      _setError("GCM payload too large");
      return false;
    }
    if (!aesGcmDecrypt(_frameKey(), iv, 12, aad, aadLen, ct, ctLen, tag,
                       _gcmBuf)) {
      _setError("GCM tag mismatch");
      return false;
    }
    if (ctLen < 4) {
      _setError("short GCM payload");
      return false;
    }
    frame.retcode = be32(_gcmBuf);
    frame.payload = _gcmBuf + 4;
    frame.payloadLen = ctLen - 4;
  }

  _lastRxMs = millis();
  return true;
}

bool TuyaClient::_queryOnce() {
  String json;
  uint32_t cmd;
  bool useSession = false;
  bool addHeader = false;

  if (_version >= 3.4f) {
    json = "{}";
    cmd = CMD_DP_QUERY_NEW;
    useSession = true;
  } else {
    json = _queryJson();
    cmd = CMD_DP_QUERY;
  }

  if (!_sendFrame(cmd, reinterpret_cast<const uint8_t *>(json.c_str()),
                  json.length(), useSession, addHeader)) {
    return false;
  }

  uint32_t deadline = millis() + tuya_lora::settings.tuyaReadTimeoutMs;
  while (static_cast<int32_t>(deadline - millis()) > 0) {
    Frame frame;
    uint32_t remaining = deadline - millis();
    if (!_readFrame(frame, remaining)) return false;
    if (frame.payloadLen == 0) continue;  // null ACK
    if (_parseFrame(frame)) {
      _queryCount++;
      return true;
    }
    // A valid but unrelated frame (e.g. heartbeat reply): keep reading.
  }
  _setError("no DP response");
  return false;
}

String TuyaClient::_queryJson() const {
  // Tuya expects a JSON object. The timestamp is not used for authorization;
  // use a stable value if the ESP32 has no wall clock yet.
  uint32_t t = static_cast<uint32_t>(time(nullptr));
  if (t < 1600000000u) t = static_cast<uint32_t>(millis() / 1000);
  String json = "{\"gwId\":\"";
  json += _deviceId;
  json += "\",\"devId\":\"";
  json += _deviceId;
  json += "\",\"uid\":\"";
  json += _deviceId;
  json += "\",\"t\":\"";
  json += String(t);
  json += "\"}";
  return json;
}

bool TuyaClient::_negotiate() {
  if (_keyLen != 16) {
    _setError("local key must be 16 chars");
    return false;
  }
  _negotiating = true;
  bool ok = _version >= 3.5f ? _negotiate35() : _negotiate34();
  _negotiating = false;
  return ok;
}

bool TuyaClient::_negotiate34() {
  fillRandom(_nonceLocal, sizeof(_nonceLocal));
  if (!_sendFrame(CMD_SESS_KEY_NEG_START, _nonceLocal, sizeof(_nonceLocal),
                  false, false)) {
    return false;
  }

  Frame frame;
  if (!_readFrame(frame, tuya_lora::settings.tuyaReadTimeoutMs)) return false;
  if (frame.cmd != CMD_SESS_KEY_NEG_RESP || frame.payloadLen < 16) {
    _setError("session key step 1 failed");
    return false;
  }

  uint8_t dec[512];
  size_t decLen = 0;
  if (!aesEcbDecrypt(_key, frame.payload, frame.payloadLen, dec, decLen)) {
    _setError("session key decrypt failed");
    return false;
  }
  if (decLen < 48) {
    _setError("session key response too short");
    return false;
  }

  memcpy(_nonceRemote, dec, 16);
  uint8_t mac[32];
  hmacSha256(_key, 16, _nonceLocal, sizeof(_nonceLocal), mac);
  if (memcmp(mac, dec + 16, 32) != 0) {
    _setError("session key HMAC mismatch");
    return false;
  }

  hmacSha256(_key, 16, _nonceRemote, sizeof(_nonceRemote), mac);
  if (!_sendFrame(CMD_SESS_KEY_NEG_FINISH, mac, 32, false, false)) return false;

  uint8_t x[16];
  for (size_t i = 0; i < 16; ++i) x[i] = _nonceLocal[i] ^ _nonceRemote[i];
  uint8_t sess[32];
  size_t sessLen = 0;
  if (!aesEcbEncrypt(_key, x, 16, sess, sessLen, false)) return false;
  memcpy(_sessionKey, sess, 16);
  return true;
}

bool TuyaClient::_negotiate35() {
  fillRandom(_nonceLocal, sizeof(_nonceLocal));
  if (!_sendFrame(CMD_SESS_KEY_NEG_START, _nonceLocal, sizeof(_nonceLocal),
                  false, false)) {
    return false;
  }

  Frame frame;
  if (!_readFrame(frame, tuya_lora::settings.tuyaReadTimeoutMs)) return false;
  if (frame.cmd != CMD_SESS_KEY_NEG_RESP || frame.payloadLen < 48) {
    _setError("session key step 1 failed");
    return false;
  }

  memcpy(_nonceRemote, frame.payload, 16);
  uint8_t mac[32];
  hmacSha256(_key, 16, _nonceLocal, sizeof(_nonceLocal), mac);
  if (memcmp(mac, frame.payload + 16, 32) != 0) {
    _setError("session key HMAC mismatch");
    return false;
  }

  hmacSha256(_key, 16, _nonceRemote, sizeof(_nonceRemote), mac);
  if (!_sendFrame(CMD_SESS_KEY_NEG_FINISH, mac, 32, false, false)) return false;

  uint8_t x[16];
  for (size_t i = 0; i < 16; ++i) x[i] = _nonceLocal[i] ^ _nonceRemote[i];
  uint8_t gcm[64];
  size_t gcmLen = 0;
  if (!aesGcmEncrypt(_key, _nonceLocal, 12, nullptr, 0, x, 16, gcm, gcmLen)) {
    return false;
  }
  if (gcmLen < 28) return false;
  memcpy(_sessionKey, gcm + 12, 16);
  return true;
}

bool TuyaClient::_parseFrame(const Frame &frame) {
  uint8_t *out = _decBuf;
  size_t outLen = 0;
  if (!_decodePayload(frame, out, outLen)) return false;

  _lastJson = "";
  for (size_t i = 0; i < outLen && i < 1024; ++i) {
    char c = static_cast<char>(out[i]);
    _lastJson += (c >= 32 && c < 127) ? c : '.';
  }

  return _parseJson(out, outLen);
}

bool TuyaClient::_decodePayload(const Frame &frame, uint8_t *out,
                                size_t &outLen) {
  if (frame.payload == nullptr || frame.payloadLen == 0) return false;

  if (_version >= 3.5f) {
    const uint8_t *p = frame.payload;
    size_t len = frame.payloadLen;
    if (len >= VERSION_HEADER_LEN && memcmp(p, "3.5", 3) == 0) {
      p += VERSION_HEADER_LEN;
      len -= VERSION_HEADER_LEN;
    }
    if (len > MAX_JSON) len = MAX_JSON;
    memcpy(out, p, len);
    outLen = len;
    return true;
  }

  if (_version >= 3.4f) {
    size_t decLen = 0;
    if (!aesEcbDecrypt(_sessionKey, frame.payload, frame.payloadLen, out,
                       decLen)) {
      _setError("AES decrypt failed");
      return false;
    }
    if (decLen >= VERSION_HEADER_LEN && memcmp(out, "3.4", 3) == 0) {
      memmove(out, out + VERSION_HEADER_LEN, decLen - VERSION_HEADER_LEN);
      decLen -= VERSION_HEADER_LEN;
    }
    outLen = decLen;
    return true;
  }

  if (_version >= 3.2f) {
    const uint8_t *p = frame.payload;
    size_t len = frame.payloadLen;
    if (len >= VERSION_HEADER_LEN && memcmp(p, "3.3", 3) == 0) {
      p += VERSION_HEADER_LEN;
      len -= VERSION_HEADER_LEN;
    }
    if (looksLikeJson(p, len)) {
      memcpy(out, p, len);
      outLen = len;
      return true;
    }
    size_t decLen = 0;
    if (!aesEcbDecrypt(_key, p, len, out, decLen)) {
      _setError("AES decrypt failed");
      return false;
    }
    outLen = decLen;
    return true;
  }

  // 3.1
  const uint8_t *p = frame.payload;
  size_t len = frame.payloadLen;
  if (len >= 19 && memcmp(p, "3.1", 3) == 0) {
    size_t decLen = 0;
    if (!aesEcbDecrypt(_key, p + 19, len - 19, out, decLen)) {
      _setError("AES decrypt failed");
      return false;
    }
    outLen = decLen;
    return true;
  }
  memcpy(out, p, len);
  outLen = len;
  return true;
}

bool TuyaClient::_parseJson(const uint8_t *json, size_t len) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, json, len);
  if (err) {
    _setError(String("JSON: ") + err.c_str());
    return false;
  }

  JsonObject dps;
  if (doc["dps"].is<JsonObject>()) {
    dps = doc["dps"].as<JsonObject>();
  } else if (doc["data"]["dps"].is<JsonObject>()) {
    dps = doc["data"]["dps"].as<JsonObject>();
  } else {
    _setError("response has no dps");
    return false;
  }

  for (JsonPair kv : dps) {
    String id = kv.key().c_str();
    String value;
    JsonVariant v = kv.value();
    if (v.is<const char *>()) {
      value = v.as<const char *>();
    } else if (v.is<bool>()) {
      value = v.as<bool>() ? "true" : "false";
    } else if (v.is<long>()) {
      value = String(v.as<long>());
    } else if (v.is<float>()) {
      value = String(v.as<float>(), 2);
    } else {
      serializeJson(v, value);
    }
    _mergeDp(id, value);
  }

  _sortDps();
  return true;
}

void TuyaClient::_mergeDp(const String &id, const String &value) {
  for (uint8_t i = 0; i < _dpCount; ++i) {
    if (_dps[i].id == id) {
      _dps[i].value = value;
      return;
    }
  }
  if (_dpCount >= DP_MAX) return;
  _dps[_dpCount].id = id;
  _dps[_dpCount].value = value;
  _dpCount++;
}

void TuyaClient::_sortDps() {
  for (uint8_t i = 1; i < _dpCount; ++i) {
    TuyaDp key = _dps[i];
    int keyNum = key.id.toInt();
    int j = i - 1;
    while (j >= 0 && _dps[j].id.toInt() > keyNum) {
      _dps[j + 1] = _dps[j];
      --j;
    }
    _dps[j + 1] = key;
  }
}

void TuyaClient::_setError(const String &msg) {
  _lastError = msg;
  Serial.print("[tuya] ");
  Serial.println(msg);
}
