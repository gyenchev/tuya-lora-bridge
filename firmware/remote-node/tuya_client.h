#pragma once

#include <Arduino.h>
#include <WiFi.h>

#include "config.h"

// One Tuya data point as read from the device.
struct TuyaDp {
  String id;
  String value;
};

// Minimal Tuya local (TCP :6668) client for the remote node.
//
// It supports protocol versions 3.1, 3.3, 3.4 and 3.5. DP_QUERY/DP_QUERY_NEW
// read the current data points; setDps() sends a CONTROL/CONTROL_NEW frame with
// a caller-supplied raw `dps` fragment. The remote node does not interpret the
// DP numbers.
class TuyaClient {
 public:
  TuyaClient();

  // version <= 0 means "try 3.3, 3.4, 3.5, 3.1 automatically".
  void setCredentials(const char *deviceId, const char *localKey, float version);
  bool hasDeviceId() const { return _deviceId.length() > 0; }
  bool hasLocalKey() const { return _keyLen == 16; }
  bool canQuery() const;

  float version() const { return _version; }
  String versionText() const;

  // Connect and (in auto mode) probe until one protocol version works.
  bool connect(IPAddress ip, uint16_t port = TUYA_PORT);
  void disconnect();
  bool connected() { return _tcp.connected(); }

  // Send a DP query and read the response. Leaves the socket open.
  bool query();

  // Send a CONTROL/CONTROL_NEW frame. dpsJson is the *inside* of the Tuya
  // "dps" object, for example: {"112":true} or {"124":"OpenCharging"}.
  // The caller is responsible for using the correct DP IDs for its device.
  bool setDps(const String &dpsJson);

  // Read unsolicited frames without blocking. Call from loop().
  void poll();

  const TuyaDp *dps() const { return _dps; }
  uint8_t dpCount() const { return _dpCount; }
  const String &lastJson() const { return _lastJson; }
  const String &lastError() const { return _lastError; }
  uint32_t lastRxMs() const { return _lastRxMs; }
  uint32_t queryCount() const { return _queryCount; }

  void clearError() { _lastError = ""; }

 private:
  struct Frame {
    uint32_t seq = 0;
    uint32_t cmd = 0;
    uint32_t retcode = 0;
    uint8_t *payload = nullptr;
    size_t payloadLen = 0;
  };

  bool _connectOnce(IPAddress ip, uint16_t port);
  bool _tryVersion(IPAddress ip, uint16_t port, float version);
  bool _negotiate();
  bool _negotiate34();
  bool _negotiate35();

  bool _sendFrame(uint32_t cmd, const uint8_t *plain, size_t plainLen,
                  bool useSessionKey, bool addVersionHeader);
  bool _readFrame(Frame &frame, uint32_t timeoutMs);
  bool _readExact(uint8_t *dst, size_t len, uint32_t timeoutMs);

  bool _queryOnce();
  bool _parseFrame(const Frame &frame);
  bool _decodePayload(const Frame &frame, uint8_t *out, size_t &outLen);
  bool _parseJson(const uint8_t *json, size_t len);
  void _mergeDp(const String &id, const String &value);
  void _sortDps();

  void _setError(const String &msg);
  String _queryJson() const;
  const uint8_t *_frameKey() const;

  WiFiClient _tcp;
  IPAddress _ip;

  String _deviceId;
  String _localKey;
  uint8_t _key[16] = {0};
  uint8_t _keyLen = 0;
  float _version = 3.3f;
  bool _autoDetect = false;
  bool _negotiating = false;

  uint8_t _sessionKey[16] = {0};
  uint8_t _nonceLocal[16] = {0};
  uint8_t _nonceRemote[16] = {0};

  uint32_t _seqno = 1;
  uint32_t _lastRxMs = 0;
  uint32_t _queryCount = 0;

  TuyaDp _dps[DP_MAX];
  uint8_t _dpCount = 0;
  String _lastJson;
  String _lastError;

  // Receive and crypto scratch space. Kept as members so loop() stack usage
  // stays small.
  uint8_t _rxBuf[2048];
  uint8_t _gcmBuf[1536];
  uint8_t _decBuf[1536];
};
