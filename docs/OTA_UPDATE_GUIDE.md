# 📡 ArduinoOTA Wireless Firmware Deployment Guide

> **Wi-Fi / OTA is disabled by default on both Gateway and Display Nodes (`ESPDASH_GATEWAY_WIFI=0`, `ESPDASH_NODE_WIFI=0`).**
> Associating to a router AP moves the ESP32 radio onto the AP's channel and causes missed ESP-NOW broadcasts when it differs from `ESPDASH_ESPNOW_CHANNEL` (`1`). By default all boards run in fixed-channel USB + ESP-NOW mode (`docs/ARCHITECTURE.md` §5.2).
> To enable OTA on a board temporarily for bench work, set `-DESPDASH_GATEWAY_WIFI=1` or `-DESPDASH_NODE_WIFI=1` in that project's `platformio.ini` and flash once over USB first.

When `ESPDASH_GATEWAY_WIFI=1` / `ESPDASH_NODE_WIFI=1` is enabled, nodes connect to any configured Wi-Fi network (`Complex_parking`, `Bazanski_ph`, `Bazanski_IS`, or `IOT-monday`) for **ArduinoOTA / WebOTA** wireless firmware updates.

---

## 📶 Configured Wi-Fi Networks & Passwords

| SSID | Password | Priority / Use Case |
| :--- | :--- | :--- |
| **`Complex_parking`** | `12345678` | Primary Garage / Parking Lot Network |
| **`Bazanski_ph`** | `52288488` | Mobile Phone Hotspot |
| **`Bazanski_IS`** | `52288488` | Home Network |
| **`IOT-monday`** | `fsdL2Dp*KBU0y#9F&c!Zbq853axj` | Office / Fallback Network |

---

## 🌐 mDNS Hostname & Device Map

| Device Node | Hardware Board | Project Directory | Target mDNS / Port |
| :--- | :--- | :--- | :--- |
| **Central Gateway** | Waveshare ESP32-S3-RS485-CAN (+ INA228) | `firmware/esp32-gateway` | `esp32-gateway.local` / USB CDC |
| **Display Node 1** | Waveshare ESP32-S3-LCD-3.16 (+ SD Rec) | `firmware/display-nodes/esp-rectangular-314` | USB CDC |
| **Display Node 2** | XIAO ESP32-S3 / S3-Zero + GC9A01 Round | `firmware/display-nodes/xiao-round-gauge` | `esp32-gauge-round.local` / USB CDC |
| **Display Node 3** | Waveshare ESP32-S3-Touch-AMOLED-1.32 | `firmware/display-nodes/esp-round-amoled-touch` | USB CDC |
| **Display Node 4** | XIAO / S3-Zero + Dual GC9A01 Round | `firmware/display-nodes/xiao-dual-round` | USB CDC |
| **Display Node 5** | LCD Wiki ES3C35P 3.5" Touch + IMU | `firmware/display-nodes/esp-touch-350` | USB CDC |
| **Display Node 6** | Elecrow CrowPanel 3.7" E-Paper (UC8253) | `firmware/display-nodes/crowpanel-epaper-370` | CH340 UART0 (`/dev/cu.usbserial-*`) |
| **Planned Nodes** | ESP32-C6 1.47", OLED, Heltec LoRa | *Planned* | `esp32-gauge-147.local`, `esp32-oled.local`, `esp32-heltec-relay.local` |

---

## ⚡ Flashing Firmware Over-The-Air (OTA)

### 1. Flash Central Gateway Wirelessly (requires `-DESPDASH_GATEWAY_WIFI=1`, see note above):
```bash
~/.platformio/penv/bin/pio run -t upload --upload-port esp32-gateway.local -d firmware/esp32-gateway
```

### 2. Flash Display Gauge Node Wirelessly:
```bash
~/.platformio/penv/bin/pio run -t upload --upload-port esp32-gauge-round.local -d firmware/display-nodes/xiao-round-gauge
```
