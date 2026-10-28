# Hardware, installation and safety

## Bill of materials

| Item | Quantity | Purpose |
|---|---:|---|
| TTGO LoRa32 V2.1 / T3 V1.6.1 | 2 | ESP32 + SX1276 + SSD1306 OLED + microSD |
| 868 MHz antenna | 2 | LoRa link |
| 5 V 1 A USB power supply | 1 | Remote node |
| 5 V USB power supply | 1 | Base node |
| IP65 enclosure, approximately 100 × 68 × 50 mm | 1 | Remote node |
| Cable gland and strain relief | 1 set | Remote power cable |
| Short USB cable | 2 | Power |
| FAT32 microSD card | 1 | Optional firmware updates |
| External 868 MHz antenna | optional | Range improvement |
| USB power meter | optional | Diagnostics |

No relays, contactors, CT clamps or mains wiring are part of this project.

## Power

### Remote node

- Use a separate 5 V USB adapter plugged into a protected socket.
- Do not tap power from the device's internal electronics.
- Do not put the adapter inside the device enclosure.
- Use an adapter with over-current, over-temperature and short-circuit
  protection.
- Check the adapter's temperature rating for outdoor or hot locations.
- Add a fuse appropriate to the adapter and cable if you build a custom DC
  cable.
- Use a cable gland and a drip loop so water cannot enter the enclosure.
- The ESP32, WiFi, LoRa and OLED typically draw 100–250 mA; a 1 A adapter has
  headroom.

### Base node

- Use any reliable USB port or 5 V adapter near the home router and MQTT
  broker.
- It needs good WiFi coverage and, ideally, a clear path to the remote antenna.

## Enclosure and antenna placement

- Mount the remote node outside the device in its own IP65 enclosure.
- Keep the 868 MHz antenna vertical and clear of metal, mains cables, inverters,
  RCD/RCBOs and the device's electronics.
- Do not coil the antenna against the enclosure. Use an external antenna or a
  plastic enclosure with the antenna standing upright.
- Separate the WiFi and LoRa antennas as much as the board allows.
- Avoid direct sunlight on the enclosure if the adapter is inside.
- Keep the enclosure accessible for reset and reflashing, but not easy for
  strangers to unplug.
- Place the base node high enough for a clear path to the remote node. Test
  several positions before mounting.

## Bench setup

Before outdoor installation:

1. Power both nodes on a desk.
2. Verify the remote AP and the device association.
3. Verify Tuya TCP and DP reads.
4. Verify LoRa status and ACK at short range.
5. Verify MQTT discovery in Home Assistant.
6. Confirm that no command starts charging unexpectedly.
7. Install only after the bench checks pass.

## EVSE safety

The device is mains-powered equipment. This project does not require opening it.

- Do not open the device, remove covers or touch internal wiring unless you are
  qualified and the unit is fully isolated.
- Do not bypass or modify the RCD/RCBO, protective earth, control pilot (CP) or
  proximity pilot (PP) circuits.
- Do not use a DIY relay or contactor to interrupt mains to the device. EV
  charging has a controlled shutdown sequence; cutting mains under load is not a
  safe control method.
- Do not connect the ESP32 to the device's internal UART, display, buttons or
  RFID reader.
- Use only the device's network API.
- The device's local protections, RCD/RCBO and control pilot remain
  authoritative. The bridge must never be the only thing preventing an unsafe
  condition.
- Have a qualified electrician inspect the final installation if you are not
  qualified to work on EVSE equipment.

## Remote-control safety rules

The firmware and Home Assistant configuration must preserve these rules:

- **No auto-start on boot.** After a power cut, reboot or link recovery, the
  device stays in its local state. Only an explicit command starts a session.
- **No auto-start on Home Assistant restart.** The base does not replay old
  commands.
- **Validate current** against the configured model limit, for example 6–32 A
  or 6–16 A for `Max16A` models. Reject out-of-range values.
- **Do not start** when the device reports a fault, no vehicle, or waiting for
  authorization.
- **Do not stop** when already stopped; avoid needless relay cycles.
- **Rate-limit** current changes and start/stop commands.
- **Link loss does nothing.** Do not issue an automatic stop or start when LoRa
  disappears. The device continues its local behavior.
- **Only accept authenticated commands** with a valid AES-GCM tag and a fresh
  sequence number.
- **Publish failures.** Do not silently ignore a command.
- **No physical arm switch is included.** Treat MQTT command topics as
  privileged and restrict broker access. If a physical inhibit is required, use
  the device's own local controls or add an external interlock that is
  independent of the bridge.

## Legal and radio notes

- **868 MHz EU:** the 868.0–868.6 MHz band has a 1% duty-cycle limit. Keep the
  snapshot interval at 60 seconds or slower and verify the airtime budget in
  [`lora-protocol.md`](lora-protocol.md). The base **LoRa** tab shows the rolling
  60-minute TX duty for the base, the observed remote TX duty and the remaining
  budget against the 1% reference; use it when changing the snapshot interval,
  spreading factor or retry settings.
- **ERP:** 14 dBm conducted is 25 mW. With a 2 dBi antenna, EIRP is 16 dBm and
  ERP is about 14 dBm (25 mW). Verify your antenna gain; a higher-gain antenna
  can exceed the limit.
- **WiFi:** use legal 2.4 GHz channels and WPA2. The AP is low-power and belongs
  indoors or in a weatherproof enclosure.
- **Certification:** do not modify the device's certified hardware. The bridge
  is a separate device.
- **Insurance and rental:** check whether adding external equipment is allowed.

## Security

- Use a strong, unique WPA2 password for the device AP.
- Restrict the AP to the device's MAC address if the firmware is extended to
  support it.
- Use a random 16-byte LoRa PSK. Do not reuse a WiFi or MQTT password.
- Use a dedicated MQTT user scoped to the bridge topics.
- Do not expose MQTT or Home Assistant to the internet without TLS/VPN.
- Keep the Tuya local key and LoRa PSK out of Git and serial logs. They live in
  ESP32 NVS and are entered through the node web UIs.
- The web UI is unauthenticated. Keep it on a trusted network.
- Treat the remote node as physically accessible: someone could unplug it, but
  they should not be able to join the AP or send valid LoRa commands.

## Maintenance

- Check the enclosure seal, cable gland and antenna after storms.
- Check the adapter and cable for heat or discoloration.
- Monitor LoRa RSSI, SNR and packet loss in Home Assistant.
- After a device firmware update, re-check the Tuya protocol version and DP map.
- If the device is re-paired or factory-reset, its local key changes; update it
  in the remote node's web UI.
- Record the final AP channel, IP, protocol version and DP map with the
  installation.

## What not to do

- Do not put the ESP32 inside the device.
- Do not power the ESP32 from the device's control board.
- Do not use the device's RFID, buttons or CP lines as a control interface.
- Do not use a relay to cut mains.
- Do not expose the ESP32 AP to the internet.
- Do not rely on the bridge for fault protection or emergency stop.
- Do not install outdoors without a proper enclosure, cable gland and strain
  relief.
