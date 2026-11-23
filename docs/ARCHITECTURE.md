# 🏛️ espDash System Architecture Specification

## Overview

**espDash** is a low-latency, modular automotive telemetry network designed for the **2014 Honda Civic (9th Gen)**.

---

## 1. Gateway Dual Wireless Architecture

The central ESP32-S3 gateway maintains two concurrent output channels:

1. **ESP-NOW 2.4 GHz Peer-to-Peer Multicast Broadcast:**
   * Transmits a compact **8-byte header + 38-byte telemetry body (46 bytes total, proto v2.4)** at 20Hz to in-car display nodes (~1-3ms latency, zero connection handshake wait) on fixed channel `ESPDASH_ESPNOW_CHANNEL` (`1`).
   * Also supports on-demand batched raw CAN log streaming (`ESPDASH_MSG_CANLOG` / `ESPDASH_MSG_CANLOG_IDS`) controlled by upstream `ESPDASH_MSG_NODE_CMD` advertisements from the SD recorder node.
2. **WebSocket / WebSerial Debug Channel (Port 8888):**
   * Serves 10Hz live telemetry JSON and raw CAN frames to the web dashboard on laptop/tablet over USB WebSerial (`navigator.serial`, default) or Wi-Fi (`esp32-gateway.local` when built with `-DESPDASH_GATEWAY_WIFI=1`).

---

## 2. Telemetry Payload

**`firmware/shared/EspDashProto/EspDashProto.h` is the single source of truth.** It is included by
the gateway and by every display node; the struct is never copied into a node. The block below is a
reader's summary — if it and the header ever disagree, the header is right.

Wire format is an 8-byte `EspDashHeader` followed by a 38-byte `EspDashTelemetry` body
(proto v2.4). The layout is **append-only**: receivers gate on `payload_len` via `ESPDASH_HAS()`,
so an old node ignores trailing fields from a new gateway and a new node skips fields an old
gateway does not send. Neither goes dark, and no field below may be reordered or removed.

```cpp
typedef struct __attribute__((packed)) {
    // ---- v2.0 ---- APPEND ONLY BELOW ------------------------------------
    uint16_t rpm;                  // 0-9000 RPM
    uint16_t speed_kmh_x10;        // 0-3000 = 0-300.0 km/h
    int16_t  water_temp_x10;       // -40.0 .. +150.0 C
    int16_t  oil_temp_x10;         // NOT on the broadcast bus - stays 0
    uint16_t battery_mv;           // millivolts from Gateway INA228 I2C sensor (0 if absent)
    uint8_t  gear;                 // selector: 0=P 1=R 2=N 3=D 4=S
    uint8_t  fuel_consumption_x10; // TRIP-AVERAGE consumption, L/100km x10 (94 = 9.4)
                                   // NOT tank level - see below
    int16_t  steering_deg;         // signed, negative = left
    int8_t   ambient_temp;         // whole degrees C
    uint8_t  flags;                // ESPDASH_FLAG_*
    uint8_t  throttle_pct;         // 0-100 %
    uint8_t  brake_pct;            // 0-100 %
    uint32_t timestamp_ms;         // gateway uptime
    // ---- added in v2.1 --------------------------------------------------
    uint16_t wheel_fl_x10, wheel_fr_x10, wheel_rl_x10, wheel_rr_x10;
    // ---- added in v2.3 --------------------------------------------------
    uint8_t  fuel_level_pct;       // TANK LEVEL 0-100 % (0x1A6 b3 = half-litres,
                                   // 1 count = 1 % of 50 L; 105 = brimmed, clamped)
    uint8_t  gear_num;             // engaged gear 1-5, 0 = none/shifting
    uint8_t  flags2;               // LOW_FUEL / ECON / SPORT / TURN_L / TURN_R
                                   // / FUEL_VALID / ODO_VALID
    // ---- added in v2.4 --------------------------------------------------
    uint16_t odo_50m;              // distance counter, 50 m/count (wraps at 3276 km; use deltas)
    int8_t   cabin_temp;           // in-car temperature, whole degrees C
    uint8_t  fan_speed;            // blower 0-7
    uint8_t  lights;               // 0=DRL 1=POS 2=LOW 3=HIGH
} EspDashTelemetry;
```

**Minor 2 is deliberately skipped.** An unmerged branch (`feature/xiao-dual-round-gauge`) already
published a v2.2 with a *different* field at offset 30. Jumping to 2.3 (and now 2.4) keeps the
guarantee that a version number identifies exactly one layout.

### Three fields with important caveats

These are the ones that trip people up, so they are called out here rather than only in the
protocol map:

| Field | State | Why |
|---|---|---|
| `oil_temp_x10` | always 0 | Not broadcast at all. Exists only as a Mode-22 diagnostic PID, which needs a transmitted request; the gateway is listen-only by design |
| `battery_mv` | live from Gateway **INA228 I2C** sensor (`0` if sensor absent) | CAN `0x305` byte 0 was retracted on 2026-08-29 (`0x305` is `SEATBELT_STATUS`, not voltage). The gateway now measures the 12 V rail directly via an INA228 on I2C (`SDA=GPIO1`, `SCL=GPIO2`, addr `0x40`) at 10 Hz (`INA` / `I2C:SCAN` serial commands). Display nodes should render `--.- V` when `0` |
| `fuel_consumption_x10` | **trip average**, not tank level and not a live figure | Renamed from `fuel_pct` on 2026-08-15; the name is kept only for wire compatibility. It is the trip-average consumption in L/100km ×10 (`CAN_PROTOCOL_MAP.md` §H). Actual tank level is carried separately in `fuel_level_pct` (`0x1A6` byte 3, added in v2.3) |

### 2.1 The signal catalog — how screens get laid out

**`firmware/shared/EspDashProto/EspDashSignals.h`.** Every signal the car gives us is now on the
wire whether or not any screen renders it, and the catalog turns "which readings does this display
show" into **a list of IDs instead of a firmware change**:

```cpp
static const EspDashSignalId kBottomRow[3] = {
    ESPDASH_SIG_COOLANT, ESPDASH_SIG_FUEL_LEVEL, ESPDASH_SIG_GEAR,
};
// ... the render loop asks the catalog for everything else
espdash_signal_format(&pkt, plen, kBottomRow[i], odo_base, txt, sizeof(txt));
spr.drawString(String(txt) + info->unit, x, y);
spr.drawString(info->label, x, y + 14);
```

Reordering that array re-lays-out the screen. No gateway change, no protocol change, no per-node
format strings to get wrong. `firmware/display-nodes/xiao-round-gauge` drives its bottom row this
way as the worked example.

The catalog supplies, per signal: a **stable key** (the same string used in the telemetry JSON and
by `espdash_signal_by_key()`, so a layout can come from config rather than compiled-in enums), a
short **label**, a **unit**, a sensible **gauge range**, the number of **decimals**, and a **kind**
(number / flag / enum, with text for enums like `DRL / POS / LOW / HIGH`).

Two things it deliberately does *not* own:

- **Warning colours.** A red threshold is a judgement about this specific car, not a property of
  the number, so it stays in the node. Where the car has its own telltale — the low-fuel lamp — use
  that rather than inventing a percentage.
- **Layout geometry.** Positions and sizes belong to the screen.

**Version safety is built in.** Every accessor takes the sender's `payload_len` and reports
"no reading" when that gateway is too old to carry the field, so a node built against a newer
catalog shows `--` rather than a zero or garbage. Same contract as `ESPDASH_HAS()`.

Run `SIGNALS` on the gateway's serial or telnet console to print the live catalog — id, key, label,
unit, range and the current value of each — which is the authoritative list to lay a screen out
from.

### Adding a new signal end to end

1. Decode it in `can_decode.cpp`, add the field to `CanDecodeState`.
2. Append to `EspDashTelemetry`, bump `ESPDASH_PROTO_MINOR`, update the size `static_assert`.
3. Copy it into the packet and the JSON in the gateway's `main.cpp`.
4. Append to `EspDashSignalId` **and** the matching row in `espdash_signal_table()`, add a case to
   `espdash_signal_x10()`. The `static_assert` on `ESPDASH_SIG_COUNT` catches a mismatch.
5. Add a row to the data matrix in `CAN_PROTOCOL_MAP.md` §A.

Nothing in step 4 or 5 requires touching a display node — it can pick the signal up whenever it
wants to.

---

## 3. Signal provenance

Every decoded signal carries an evidence tag (**CONFIRMED / LOCAL / ON-ROAD / REFERENCE /
UNMAPPED**) in `docs/CAN_PROTOCOL_MAP.md`. Nothing is asserted from a single source alone. Two
working rules come out of that document and belong here too:

- **The last byte of a Honda message is metadata, not signal.** 44 of this car's 45 IDs carry a
  4-bit checksum in the low nibble and a 2-bit counter above it. Any decode that reads it is wrong.
- **A signal that never changes cannot be validated by analysis — it has to be provoked.** Scripted
  single-subsystem captures with a written running order are this project's highest-yield tool; see
  `CAN_PROTOCOL_MAP.md` §G and `CAPTURE_ABS_TC.md`.

---

## 4. Power Management, Sensors & IMU Integration

### Gateway Automatic Power Saving (Ignition-Off Protection)
* **2-Tier Sleep State Machine (`firmware/esp32-gateway`):** Monitors CAN bus activity (`last_can_rx_ms`), controlled via `POWER:AUTO` (default), `POWER:AWAKE`, and `POWER?`:
  * **Tier 1 (25 s CAN silence):** Flushes logs, suspends worker tasks, shuts down TWAI and Wi-Fi/ESP-NOW radios, and enters **Light Sleep** with `<1 ms` hardware wake-up on `GPIO 16` (`CAN_RX`).
  * **Tier 2 (30 min continuous silence):** Escalates to **Deep Sleep** (~15 µA core draw) with RTC EXT0 wake on `GPIO 16`.
* **12 V Battery Voltage Sensor (INA228):** Connected to the Gateway over I2C (`SDA=GPIO1`, `SCL=GPIO2`, address `0x40`), polled at 10 Hz into `battery_mv`.

### On-Node IMU Integration (`esp-touch-350`) & Future Gateway IMU
* **Implemented on `esp-touch-350`:** MPU-6500 (auto-detects MPU-9250 + AK8963 magnetometer) on the touch I2C bus (`IO38`/`IO39`, guarded by `bsp/i2c_lock.h`), sampled at 200 Hz with Mahony sensor fusion in vehicle coordinates, driving the IMU diagnostic screen and the Airbus-style PFD attitude/G-load display.
* **Future Gateway GY-BNO08X Integration (Planned):** Optional 9-DOF sensor on the Central Gateway for fleet-wide G-force/tilt broadcast (`ESPDASH_MSG_IMU`) and parking shock detection.

---

## 5. Wireless Protocol Co-existence (ESP-NOW + BLE + Wi-Fi)

* **ESP-NOW & BLE Simultaneous Operation:** ESP32-S3 features hardware Wi-Fi/Bluetooth Coexistence (`esp_coex`). ESP-NOW and BLE can operate concurrently.
* **ESP-NOW Performance Optimization:** Disabling standard Wi-Fi router STA connection when in pure track mode eliminates Wi-Fi beacon listening overhead and reduces ESP-NOW packet latency to `< 0.5 ms`.

### 5.1 Two Wi-Fi/ESP-NOW defects found and fixed (2026-08-09)

Both were invisible in bench testing (both boards near a stable, in-range network) and only showed
up once the gateway and node were run somewhere without one, i.e. the actual use case.

**Wi-Fi power-save silently drops most ESP-NOW packets.** With both boards associated to an AP,
STA modem sleep parks the radio between DTIM beacons and it sleeps through most ESP-NOW broadcasts.
Measured on the bench with the gateway sending at 20 Hz:

  | | rate received | delivery |
  |---|---|---|
  | before `esp_wifi_set_ps(WIFI_PS_NONE)` | 3.4–8.8 Hz | ~30% |
  | after | 19.7–20.2 Hz | 100% (244/244, 0 gaps) |

Fixed by calling `esp_wifi_set_ps(WIFI_PS_NONE)` on both the gateway and every display node, right
before `esp_now_init()`. Both devices are vehicle/mains powered, so the extra draw is an acceptable
trade for a dependable link — this does not apply to a battery node design.

**A Wi-Fi connection that drops mid-drive can silently kill ESP-NOW until the gateway is rebooted.**
The gateway's boot sequence only ever handled "no known network found within 15 s" — in that case it
locks the radio to a fixed channel and stays there for the rest of the boot, which is stable.
It never handled "connected fine at boot, then drove out of range." In that case
`WiFi.setAutoReconnect(true)` leaves the radio scanning/roaming indefinitely, and the ESP-NOW peer
is configured with `channel = 0` ("follow the station's current channel") — which is only
well-defined while genuinely associated. Mid-reconnect, that channel is a moving target, so
`esp_now_send()` can fail outright (previously unchecked) or succeed while broadcasting on a channel
no display node is listening to. Symptom observed on-car: the round gauge searched continuously and
never found the gateway, and was only fixed by power-cycling the *gateway* (not the node) — the
reboot re-runs the 15 s boot-time timeout and lands back on the stable fixed-channel path.

Fixed in `firmware/esp32-gateway/src/main.cpp`'s `loop()`: if Wi-Fi has been down for 20 s
(long enough to not react to a momentary blip, short enough not to leave the car undriveable-by-gauge
for long), the gateway deliberately abandons the reconnect attempt — `setAutoReconnect(false)`,
disconnects, and locks to channel 1, mirroring the boot-time fallback. ESP-NOW telemetry is the
higher-priority function while driving; regaining the dashboard/OTA link over Wi-Fi can wait for the
next reboot. `esp_now_send()` failures are now counted (`espnow_send_fail`) and the fallback state
is reported (`wifi_fallback:yes/no`), both visible via the gateway's `STATS` serial command — so this
class of failure is diagnosable next time instead of requiring another blind reboot-and-guess.

### 5.2 The fallback didn't hold up on-car — gateway Wi-Fi is now off by default (2026-08-10)

Both §5.1 fixes were on the gateway when it was tested driving. The ESP-NOW link still connected
briefly then degraded and dropped consistently — even at 40 cm, which is too close and too
consistent to be a range/RF explanation. That points at the gateway's own radio still doing something
disruptive to ESP-NOW, even with power-save disabled and the 20 s abandon-and-relock fallback in
place.

Rather than add a third patch on top of two that didn't hold, the gateway's Wi-Fi station is now
**disabled by default** via a compile-time flag, `ESPDASH_GATEWAY_WIFI` (`firmware/esp32-gateway/platformio.ini`,
default `0`). At the default setting the gateway never attempts a Wi-Fi connection at all: no
association, no scanning, no roaming, no runtime fallback to abandon — because there's nothing left
to fall back from. The §5.1 mitigations are eliminated by construction rather than defended against
further.

The code for the old Wi-Fi-connected behavior — mDNS, ArduinoOTA, the WebSocket dashboard path,
Telnet, and the §5.1 runtime fallback — is **gated behind the flag, not deleted**, since gateway Wi-Fi
is likely to be wanted again for some future use case. Setting `-DESPDASH_GATEWAY_WIFI=1` and
rebuilding restores it exactly; both configurations are verified to compile (default: 16.4% RAM /
20.9% flash; flag on: 18.5% / 23.4%, matching the pre-§5.2 build almost exactly).

**Consequences:** Every gateway flash now uses USB unless `-DESPDASH_GATEWAY_WIFI=1` is re-enabled. Display nodes have likewise been updated to default to `-DESPDASH_NODE_WIFI=0` in their `platformio.ini` files so every node stays locked on `ESPDASH_ESPNOW_CHANNEL` (`1`) without AP channel-mismatch drops (`docs/OTA_UPDATE_GUIDE.md` and `CLAUDE.md` reflect this).

### 5.3 Hardware & Display Node Fleet

* **Gateway:** `esp32-gateway.local` (`firmware/esp32-gateway`, Waveshare ESP32-S3-RS485-CAN + INA228 12 V I2C sensor)
* **Implemented Display Nodes (`firmware/display-nodes/`):**
  1. `esp-rectangular-314` — Waveshare ESP32-S3-LCD-3.16 (ST7701 RGB LCD + QMI8658 IMU + SD_MMC wireless raw CAN recorder)
  2. `xiao-round-gauge` — Seeed XIAO ESP32-S3 / ESP32-S3-Zero + GC9A01 240×240 Round SPI TFT (dual-screen carousel: EEZ LVGL UI + catalog-driven `TFT_eSprite` UI)
  3. `xiao-dual-round` — Seeed XIAO ESP32-S3 / ESP32-S3-Zero + 2× GC9A01 240×240 round panels on shared SPI as a 240×480 virtual LVGL display
  4. `esp-round-amoled-touch` — Waveshare ESP32-S3-Touch-AMOLED-1.32 (466×466 CO5300 QSPI AMOLED + CST820 Touch, Eco Coach UI)
  5. `esp-touch-350` — LCD Wiki ES3C35P 3.5" 480×320 QSPI Capacitive Touch LCD + MPU-6500/9250 IMU (3 pages: Car Data, IMU Diagnostics, Airbus PFD)
  6. `crowpanel-epaper-370` — Elecrow CrowPanel 3.7" E-Paper (`ESP32-S3-WROOM-1-N8R8`, UC8253 416×240 B&W slow-data dash: Fuel/Range/Trip/Climate, Session Health/Service, and 0 mA Parked Snapshot + Catalog Matrix, with smart partial/full refresh)
* **Planned / Pending Nodes:**
  * `esp32-gauge-147.local` — Waveshare ESP32-C6-LCD-1.47
  * `esp32-oled.local` — Generic ESP32-S3 + I2C OLED shift light
  * `esp32-heltec-relay.local` — Heltec V3/V4 Meshtastic LoRa Bridge to Home Assistant

## 6. Display node UI sources (EEZ Studio)

The LVGL UIs are designed in EEZ Studio and **exported into** the firmware
trees. The generated `src/ui/` directories are build artefacts — editing them
by hand is lost on the next export. Edit the `.eez-project` and re-export.

| Node | EEZ project |
|---|---|
| `esp-rectangular-314` | `/Users/kickoff_laptop/eez-projects/esp32-s3-lcd-3.16 gauges/esp32-s3-lcd-3.16 gauges.eez-project` |
| `xiao-round-gauge` | `firmware/display-nodes/xiao-round-gauge/eez-template/xiao_round_gauge.eez-project` (in-repo) |
| `xiao-dual-round` | *not yet created.* Will be a **240×480** project (not 240×240 — see §5 Node 7), covering both panels' widget trees in one export, and will live in-repo at `firmware/display-nodes/xiao-dual-round/eez-template/`, matching `xiao-round-gauge`'s convention. Until it exists, `firmware/display-nodes/xiao-dual-round/src/main.cpp` hand-codes a placeholder LVGL UI behind a `build_placeholder_ui()` function, commented with exactly what to delete and where `ui_init()`/`ui_tick()` slot in once the export lands. |

The rectangular node's project lives **outside this repository**, so it is not
version-controlled with the firmware that consumes it. Keep that in mind before
relying on `git` to recover a UI change.

### Two gotchas that cost time

- **Include path.** EEZ exports `#include <lvgl/lvgl.h>`, not `<lvgl.h>`, so
  the node's `platformio.ini` needs
  `-I ${PROJECT_DIR}/.pio/libdeps/esp-rectangular-314` or the build fails on
  the generated files.
- **Fonts must be enabled.** EEZ labels can reference a font that
  `lv_conf.h` has switched off (we hit `LV_FONT_MONTSERRAT_20 0`). The symptom
  is a link error or blank text, not an obvious font warning.
- **Recolour markup is not a colour.** Labels driven from `main.cpp` set their
  colour with `lv_obj_set_style_text_color()`; `#RRGGBB ...#` markup left text
  invisible against the dark background under the LIGHT theme.

`objects.status_label` and `objects.warning_label` are the two single-line
labels reserved for firmware messages (SD state, record status). Firmware
leaves them empty when it has nothing to say.
