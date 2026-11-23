# Implementation Plan: espDash - Distributed Automotive Telemetry System

This implementation plan tracks the architecture for **espDash**, a distributed, low-latency, car-safe automotive telemetry network and web dashboard for the **2014 Honda Civic**. A central ESP32-S3 CAN Gateway captures vehicle bus data, measures the 12 V rail via an INA228 I2C sensor, broadcasts **ESP-NOW v2.4 (~1-3ms latency)** to multiple display nodes on fixed channel `1`, and streams JSON/raw CAN frames to a **Web Telemetry Dashboard** (WebSerial / optional WebSocket).

---

## 🏛️ System Architecture Diagram

```mermaid
flowchart TD
    subgraph Car_CAN_Bus ["🚗 Vehicle F-CAN Bus (500 kbps)"]
        OBD2["OBD-II Port (Pin 6 CAN-H / Pin 14 CAN-L)"]
        INA["INA228 I2C Sensor (12V Rail)"]
    end

    subgraph Central_Gateway ["⚡ Central Gateway (Waveshare ESP32-S3-RS485-CAN)"]
        TWAI["TWAI Passive Receiver (100% Listen-Only)"]
        DECODER["Honda Civic CAN Decoding Engine"]
        SLEEP["2-Tier Sleep Manager (GPIO 16 CAN Wake)"]
        ESPNOW_TX["ESP-NOW v2.4 Broadcast (20Hz) + CanLog"]
        SERIAL_WS["USB WebSerial / Optional WS (Port 8888)"]
        
        OBD2 --> TWAI --> DECODER
        INA --> ESPNOW_TX
        DECODER --> ESPNOW_TX
        DECODER --> SERIAL_WS
    end

    subgraph Laptop_Debug ["💻 Laptop / Tablet Debug Station"]
        WEB_APP["🌐 Web Telemetry Dashboard & Python Tools\n(HTML5/CSS3/Canvas + WebSerial + WebSockets)"]
        SERIAL_WS ==>|"WebSerial / WebSocket Stream"| WEB_APP
    end

    subgraph In_Car_Nodes ["🏎️ Implemented Display Nodes"]
        NODE1["esp-rectangular-314\nWaveshare 3.16 inch LCD + SD CAN Recorder"]
        NODE2["xiao-round-gauge\nSeeed XIAO / S3-Zero + 240x240 GC9A01"]
        NODE3["esp-round-amoled-touch\nWaveshare 1.32 inch 466x466 AMOLED Touch"]
        NODE4["xiao-dual-round\nXIAO / S3-Zero + Dual 240x240 GC9A01"]
        NODE5["esp-touch-350\nES3C35P 3.5 inch Touch + MPU-6500 IMU + PFD"]
        NODE6["crowpanel-epaper-370\nElecrow 3.7 inch E-Paper Slow-Data & Parked Dash"]

        ESPNOW_TX ==>|"ESP-NOW v2.4 Ch1"| NODE1
        ESPNOW_TX ==>|"ESP-NOW v2.4 Ch1"| NODE2
        ESPNOW_TX ==>|"ESP-NOW v2.4 Ch1"| NODE3
        ESPNOW_TX ==>|"ESP-NOW v2.4 Ch1"| NODE4
        ESPNOW_TX ==>|"ESP-NOW v2.4 Ch1"| NODE5
        ESPNOW_TX ==>|"ESP-NOW v2.4 Ch1"| NODE6
    end

    subgraph Planned_Nodes ["🏠 Planned Expansion"]
        PLANNED["ESP32-C6 1.47 / OLED / Heltec LoRa Relay -> Home Assistant"]
        ESPNOW_TX -.-> PLANNED
    end
```

---

## 📶 Multi-Network Wi-Fi & Credentials Specification

Every node incorporates **WiFiMulti** to automatically connect to whatever network is present:

| Network SSID | Password | Network Role / Location |
| :--- | :--- | :--- |
| **`Complex_parking`** | `12345678` | Garage / Parking Lot Network |
| **`Bazanski_ph`** | `52288488` | Mobile Phone Hotspot |
| **`Bazanski_IS`** | `52288488` | Home Network |
| **`IOT-monday`** | `fsdL2Dp*KBU0y#9F&c!Zbq853axj` | Office / Fallback Network |

* **Zero Disassembly Firmware Flashing:** Flash any gauge mounted in the vehicle dashboard wirelessly over Wi-Fi (`Complex_parking`, `Bazanski_ph`, `Bazanski_IS`, or `IOT-monday`) without removing trims or unplugging USB cables!

---

## 📁 Workspace Consolidation (`/Users/kickoff_laptop/Developer/espDash`)

The project is consolidated into a dedicated standalone workspace at `/Users/kickoff_laptop/Developer/espDash`:

```text
/Users/kickoff_laptop/Developer/espDash/
├── README.md
├── AGENTS.md
├── CLAUDE.md
├── docs/
│   ├── ARCHITECTURE.md
│   ├── CAN_PROTOCOL_MAP.md
│   ├── OTA_UPDATE_GUIDE.md
│   └── IMPLEMENTATION_PLAN.md
├── firmware/
│   ├── esp32-gateway/           # Central ESP32-S3 CAN Gateway Firmware
│   └── display-nodes/           # Universal gauge display node firmware
└── web-dashboard/               # HTML5/JS Web Telemetry Dashboard
```

---

## 🔄 Development & Rollout Phases

- [x] **Phase 0 (Workspace & Documentation Setup):** Consolidate `/Users/kickoff_laptop/Developer/espDash` workspace, Wi-Fi config, and architecture/CAN documentation.
- [x] **Phase 1 (Gateway ESP-NOW, INA228, Power Saving & Unit Tests):** Implemented passive 500 kbps TWAI decoder (`can_decode.cpp` + native regression suite), INA228 12 V battery monitor, 2-tier CAN-wake sleep, ESP-NOW v2.4 + `EspDashSignals.h` catalog, and raw CAN log streaming.
- [x] **Phase 2 (Modern Web Telemetry Dashboard & CLI Tools):** Built HTML5/JS Web Dashboard (`web-dashboard/`) with WebSerial & WebSocket support plus Python log collector/decoder tools (`tools/`).
- [x] **Phase 3 (Display Nodes Fleet):** Implemented `esp-rectangular-314` (with SD CAN recorder), `xiao-round-gauge`, `xiao-dual-round`, `esp-round-amoled-touch`, `esp-touch-350` (with 200 Hz MPU-6500 IMU + Airbus PFD page), and `crowpanel-epaper-370` (Elecrow 3.7" E-Paper slow-data & zero-power parked dash).
- [ ] **Phase 4 (Remaining Planned Nodes):** Waveshare ESP32-C6 1.47", OLED Shift Light, and Heltec Meshtastic Home Assistant LoRa Relay.
- [ ] **Phase 5 (Unmapped CAN Signals & Gateway IMU):** Capture ABS/TC/CEL activations (`docs/CAPTURE_ABS_TC.md`), script remaining `0x305` passenger/rear seatbelt bits, and evaluate optional gateway-level IMU (`ESPDASH_MSG_IMU`).
