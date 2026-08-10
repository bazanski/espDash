# CLAUDE.md - espDash Project Guidelines

## Quick Commands

- **Build Central Gateway Firmware:**
  ```bash
  ~/.platformio/penv/bin/pio run -d firmware/esp32-gateway
  ```
- **Flash Gateway Wirelessly (OTA):** requires building with `-DESPDASH_GATEWAY_WIFI=1` in
  `firmware/esp32-gateway/platformio.ini` first — the gateway's Wi-Fi station is disabled by default
  (see `docs/ARCHITECTURE.md` S5.2), so OTA won't be reachable otherwise.
  ```bash
  ~/.platformio/penv/bin/pio run -t upload --upload-port esp32-gateway.local -d firmware/esp32-gateway
  ```
- **Flash Gateway over USB** (works regardless of the flag — the normal path now):
  ```bash
  ~/.platformio/penv/bin/pio run -t upload --upload-port /dev/cu.usbmodem101 -d firmware/esp32-gateway
  ```

## Code Guidelines

- **Safety First:** Central Gateway MUST maintain `TWAI_MODE_LISTEN_ONLY` when connected to vehicle CAN bus.
- **Wireless Latency:** Use ESP-NOW binary multicast struct (`TelemetryPacket`, 32 bytes) for <3ms display update speeds.
- **Zero Battery Drain:** Keep ESP32 deep sleep enabled on ignition OFF.
