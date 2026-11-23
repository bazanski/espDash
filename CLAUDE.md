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

- **Run Gateway Host Unit Tests (CAN Decode & Protocol):**
  ```bash
  ~/.platformio/penv/bin/pio test -e native -d firmware/esp32-gateway
  ```
- **Flash Display Nodes over USB:**
  ```bash
  # Example: ES3C35P 3.5" Touch + IMU + PFD
  ~/.platformio/penv/bin/pio run -t upload --upload-port /dev/cu.usbmodem1101 -d firmware/display-nodes/esp-touch-350
  # Example: CrowPanel 3.7" E-Paper (CH340 UART0 @ 460800)
  ~/.platformio/penv/bin/pio run -t upload --upload-port /dev/cu.usbserial-210 -d firmware/display-nodes/crowpanel-epaper-370
  ```

## Code Guidelines

- **Safety First:** Central Gateway MUST maintain `TWAI_MODE_LISTEN_ONLY` when connected to vehicle CAN bus.
- **Wireless Latency & Wire Protocol:** Use the shared `EspDashProto.h` v2.4 header (`8-byte EspDashHeader + 38-byte EspDashTelemetry = 46 bytes`) and `EspDashSignals.h` catalog in `firmware/shared/EspDashProto/`. Do not copy packet structs into nodes.
- **Zero Battery Drain:** Keep ESP32-S3 gateway 2-tier Light/Deep Sleep enabled on CAN inactivity.
