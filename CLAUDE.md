# CLAUDE.md - espDash Project Guidelines

## Quick Commands

- **Build Central Gateway Firmware:**
  ```bash
  ~/.platformio/penv/bin/pio run -d firmware/esp32-gateway
  ```
- **Flash Gateway Wirelessly (OTA):**
  ```bash
  ~/.platformio/penv/bin/pio run -t upload --upload-port esp32-gateway.local -d firmware/esp32-gateway
  ```
- **Flash Gateway over USB:**
  ```bash
  ~/.platformio/penv/bin/pio run -t upload --upload-port /dev/cu.usbmodem101 -d firmware/esp32-gateway
  ```

## Code Guidelines

- **Safety First:** Central Gateway MUST maintain `TWAI_MODE_LISTEN_ONLY` when connected to vehicle CAN bus.
- **Wireless Latency:** Use ESP-NOW binary multicast struct (`TelemetryPacket`, 32 bytes) for <3ms display update speeds.
- **Zero Battery Drain:** Keep ESP32 deep sleep enabled on ignition OFF.
