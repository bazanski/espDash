# Implementation Plan: espDash - Distributed Automotive Telemetry System

This implementation plan outlines the architecture for **espDash**, a distributed, low-latency, car-safe automotive telemetry network and web dashboard for the **2014 Honda Civic**. A central ESP32-S3 CAN Gateway captures vehicle bus data, broadcasts it using **ESP-NOW (~1-3ms latency)** to multiple display nodes, and serves a **Concurrent WebSocket / WebSerial Debug Channel** to a modern **Web Telemetry Dashboard**. 

Every node in the network features **Wireless ArduinoOTA / WebOTA** firmware updating capability.

---

## 🏛️ System Architecture Diagram

```mermaid
flowchart TD
    subgraph Car_CAN_Bus ["🚗 Vehicle F-CAN Bus (500 kbps)"]
        OBD2["OBD-II Port (Pin 6 CAN-H / Pin 14 CAN-L)"]
    end

    subgraph Central_Gateway ["⚡ Central Gateway: esp32-gateway.local\n(Waveshare ESP32-S3-RS485-CAN)"]
        TWAI["TWAI Passive Receiver (100% Listen-Only)"]
        DECODER["Honda Civic CAN Decoding Engine"]
        OTA_GW["ArduinoOTA Engine (Wireless Firmware Updates)"]
        ESPNOW_TX["ESP-NOW Broadcast Engine (20Hz - 50Hz)"]
        WS_SERVER["WebSocket / WebSerial Debug Server (Port 8888)"]
        
        OBD2 --> TWAI --> DECODER
        DECODER --> ESPNOW_TX
        DECODER --> WS_SERVER
    end

    subgraph Laptop_Debug ["💻 Laptop / Tablet Debug Station"]
        WEB_APP["🌐 Modern Web Telemetry Dashboard\n(HTML5/CSS3/Canvas + WebSockets + WebSerial)\n[Python App Deprecated]"]
        WS_SERVER ==>|"WebSocket / WebSerial Stream"| WEB_APP
    end

    subgraph In_Car_Nodes ["🏎️ Universal Telemetry Display Gauge Nodes (All ArduinoOTA Enabled)"]
        NODE1["Node 1: esp32-gauge-3inch.local\nWaveshare ESP32-S3-LCD-3.16\n(Universal Telemetry Gauge)"]
        NODE2["Node 2: esp32-gauge-round.local\nSeeed XIAO ESP32-S3 + JXL v1.1\n(Universal Circular Telemetry Gauge)"]
        NODE3["Node 3: esp32-gauge-amoled.local\nWaveshare ESP32-S3-Touch-AMOLED-1.32\n(Universal Round Touch Gauge)"]
        NODE4["Node 4: esp32-gauge-147.local\nWaveshare ESP32-C6-LCD-1.47\n(Universal Telemetry Gauge)"]
        NODE5["Node 5: esp32-shiftlight.local\nGeneric ESP32-S3 + I2C OLED\n(Universal Gauge / Shift Light)"]

        ESPNOW_TX ==>|"ESP-NOW Peer-to-Peer Broadcast\n(~1-3ms latency, zero connection wait)"| NODE1
        ESPNOW_TX ==>|"ESP-NOW Peer-to-Peer Broadcast\n(~1-3ms latency, zero connection wait)"| NODE2
        ESPNOW_TX ==>|"ESP-NOW Peer-to-Peer Broadcast\n(~1-3ms latency, zero connection wait)"| NODE3
        ESPNOW_TX ==>|"ESP-NOW Peer-to-Peer Broadcast\n(~1-3ms latency, zero connection wait)"| NODE4
        ESPNOW_TX ==>|"ESP-NOW Peer-to-Peer Broadcast\n(~1-3ms latency, zero connection wait)"| NODE5
    end

    subgraph Home_Automation ["🏠 Home Assistant LoRa Telemetry Relay"]
        NODE6["Node 6: esp32-heltec-relay.local\nHeltec V3/V4 Meshtastic ESP32-S3 Node\n(LoRa Vehicle Telemetry Transmitter)"]
        HA["Home Assistant Server\n(Meshtastic Integration Node)"]

        ESPNOW_TX -.->|"Telemetry Data"| NODE6
        NODE6 ==>|"LoRa 868/915 MHz Mesh"| HA
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

## 🔄 Proposed Development & Rollout Phases

- [x] **Phase 0 (Workspace & Documentation Setup):** Create `/Users/kickoff_laptop/Developer/espDash` workspace, populate Wi-Fi credentials (`IOT-monday`), and save full documentation.
- [ ] **Phase 1 (Gateway ESP-NOW & WebSockets Firmware):** Port gateway firmware with concurrent ESP-NOW broadcasting and WebSocket/WebSerial streaming.
- [ ] **Phase 2 (Modern Web Telemetry Dashboard App):** Build the HTML5/JS Web Dashboard with WebSockets & WebSerial support.
- [ ] **Phase 3 (Universal Display Gauges & ArduinoOTA):** Implement universal display node firmware for 3.16" LCD, Round JXL, AMOLED 1.32", ESP32-C6 LCD 1.47", and OLED shift light with ArduinoOTA enabled.
- [ ] **Phase 4 (Heltec Meshtastic Home Assistant Relay):** Build Heltec ESP32-S3 bridge to send vehicle status over LoRa Mesh to Home Assistant.
