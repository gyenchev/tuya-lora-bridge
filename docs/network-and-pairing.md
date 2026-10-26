# Network and pairing

## Overview

The remote node hosts a private 2.4 GHz WiFi AP for the Tuya device. The device
joins that AP instead of the home network. The remote node discovers the device
on the AP and connects to its Tuya local API on TCP port `6668`.

```text
Home WiFi ~~~~~~~~ (out of range) ~~~~~~~~ Tuya device
                                                │
                                                │ 2.4 GHz, short distance
                                                ▼
                                         Remote node AP
                                                │
                                         LoRa 868 MHz
                                                │
                                         Base node at home
```

The remote node does not need internet access. It only needs to reach the
device and the base node.

## AP settings

| Setting | Recommendation |
|---|---|
| SSID | Any private name, for example `TuyaLoraRemote` |
| Password | Strong WPA2 password, 12+ characters |
| Band | 2.4 GHz |
| Channel | 1, 6 or 11; use a fixed channel |
| Security | WPA2-PSK |
| Hidden | No; some devices refuse hidden APs |
| Max clients | 4 |
| AP IP | `192.168.4.1` |
| DHCP pool | `192.168.4.2`–`192.168.4.20` when configurable |
| Internet | None |

Do not enable AP client isolation. The remote node must be able to reach the
device.

Set the AP SSID, password and channel in the remote node's **Access point** tab.

## Pairing procedure

The Tuya app requires a 2.4 GHz network with internet to provision the device.
The remote AP has no internet. The device therefore needs to be paired while it
is connected to a temporary hotspot with the same credentials that the remote
node will use later.

1. Choose the final AP SSID and password.
2. Create a phone hotspot with exactly those credentials.
   - On iPhone, enable **Maximize Compatibility** to force 2.4 GHz.
   - On Android, select a 2.4 GHz hotspot band when available.
3. Connect the phone running the Tuya app to the hotspot if needed.
4. Pair and activate the device in Tuya Smart or Smart Life.
5. Retrieve the device ID, local key and protocol version as described in
   [`tuya-device.md`](tuya-device.md).
6. Turn the hotspot off.
7. Power the remote node and confirm that its AP uses the same SSID and
   password.
8. The device reconnects to the remote node because, from its point of view, it
   is the same network.

A small travel router is a practical alternative. Configure it with the final
SSID/password, pair the device through it, and run `tinytuya` from a laptop on
the same router. Replace the router with the remote node afterward.

## Finding the device on the AP

The ESP32 Arduino core 3.1.0 exposes the soft-AP station list with DHCP
addresses. The remote node uses this first:

```cpp
wifi_sta_list_t list;
wifi_sta_mac_ip_list_t ipList;
esp_wifi_ap_get_sta_list(&list);
esp_wifi_ap_get_sta_list_with_ip(&list, &ipList);
// ipList.sta[i].mac, ipList.sta[i].ip
```

It probes TCP port `6668` on the client addresses. If the API is unavailable on
another core version, the remote node falls back to a subnet scan.

### Static IP option

A static device IP can skip discovery entirely. Set it in the remote node's
**Tuya** tab. The address must be on the AP subnet, for example
`192.168.4.50`.

### Fallback discovery methods

- **TCP scan:** scan `192.168.4.2`–`192.168.4.254` for TCP `6668` with a short
  timeout.
- **Tuya UDP discovery:** Tuya devices broadcast on UDP ports `6666`, `6667`
  and `7000`. The payload contains the device ID, product ID and protocol
  version; newer firmware may encrypt parts of it.
- **Static DHCP lease:** bind the device MAC to a fixed IP if using a custom
  DHCP server.

## Connectivity checks

1. Confirm the device appears as an AP station.
2. Confirm it received an IP on `192.168.4.0/24`.
3. Test TCP:

   ```bash
   nc -vz DEVICE_IP 6668
   ```

4. Query the DPs with `tinytuya` and the local key.
5. Disconnect internet from the test network and repeat the query. Local control
   must still work.
6. Power-cycle the device and confirm it rejoins the AP and the TCP connection
   recovers.

## Tuya TCP notes

- The device is the TCP server; the remote node is the client.
- Most Tuya devices allow one local TCP client at a time. Close the Tuya app and
  any laptop `tinytuya` session before connecting from the remote node.
- Keep the Tuya connection alive with a heartbeat every 10–15 seconds.
- Reconnect with backoff after a socket failure.
- A dropped WiFi association usually means a dropped TCP connection; handle
  both.
- Some devices send unsolicited DP updates. The bridge primarily uses polling,
  so a missed unsolicited update is corrected by the next snapshot.
- Protocol versions 3.4 and 3.5 can use session-key negotiation. See
  [`tuya-device.md`](tuya-device.md).

## Failure modes

| Symptom | Likely cause | Action |
|---|---|---|
| Device never joins AP | Wrong credentials, WPA3/hidden AP, 5 GHz hotspot | Match SSID/password exactly; force 2.4 GHz; use WPA2; use a visible SSID |
| Device joins then leaves | Requires internet or cloud connectivity | Test local TCP quickly; use a conventional WiFi bridge if it leaves |
| No IP found | DHCP failed or scan too slow | Check the station list, extend the scan, or use the static IP option |
| TCP `6668` refuses | Wrong IP, device asleep, app holding the connection | Re-scan, close the app, retry |
| TCP connects but decrypt fails | Wrong local key or protocol version | Re-fetch the key and try 3.3/3.4/3.5 |
| Works on the bench, not installed | Range, antenna or enclosure | Move the AP/antenna, check RSSI, try SF9 |
| Stops after hours | Watchdog, heap or TCP leak | Check serial logs, reconnect behavior and heap headroom |

## Security

- Use WPA2 with a strong, unique password. Do not reuse the home WiFi password.
- Limit the AP to the device's MAC address if the firmware is extended to
  support it.
- The AP has no internet, so a compromised device cannot reach the internet
  through it.
- Keep the Tuya local key and LoRa PSK in NVS and enter them through the web UI.
  Never commit them or print them in logs.
- The configuration web UI and status page are unauthenticated. Access is
  limited to the WPA2 AP. Change the default setup AP password after first
  configuration.
- Treat the remote node as physically accessible. Use a closed enclosure and a
  strong AP password.
