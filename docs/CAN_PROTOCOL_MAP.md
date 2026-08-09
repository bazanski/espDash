# Honda Civic 2014 (9th Gen) CAN Bus Signal Matrix & Protocol Registry

This document serves as the authoritative protocol map for decoding F-CAN (500 kbps) telemetry signals for `espDash`.

---

## 🟢 Category A: Fully Verified & Working Live in Car

| Signal Name | CAN ID (Hex) | Rate | Byte Position & Decoding Formula | Verified Live Range / Values | Status |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Engine RPM** | `0x1DC` | 50 Hz | `((data[1] << 8) \| data[2])` RPM | `650 - 4,957 RPM` | 🟢 **VERIFIED IN CAR** |
| **Coolant Temp (°C)** | `0x324` | 10 Hz | `(float)data[0] - 40.0f` °C | **`86.0 - 87.0 °C`** | 🟢 **VERIFIED IN CAR** |
| **Steering Wheel Angle** | `0x156` | 50 Hz | `(int16_t)((data[0] << 8) \| data[1]) / 9.0f` ° | **`0°` Center, `-540°` Full Left, `+540°` Full Right** | 🟢 **VERIFIED IN CAR** |
| **12V Battery Voltage** | `0x305` | 5 Hz | `data[0] * 100` mV | **`14.2 Volts`** (Charging idle voltage) | 🟢 **VERIFIED IN CAR** |
| **Outdoor Ambient Temp** | `0x21E` / `0x372` | 5 Hz | `data[4]` on `0x21E` or `data[0]` on `0x372` °C | **`32 °C`** (Matches vehicle dash display) | 🟢 **VERIFIED IN CAR** |
| **Fuel Level (%)** | `0x324` | 10 Hz | `(data[1] / 2.0f)` % | **`50.0% - 53.0%`** (Matches physical fuel tank) | 🟢 **VERIFIED IN CAR** |

---

## 🟡 Category B: Verified in Log / Calibrated for Fine Tuning

| Signal Name | CAN ID (Hex) | Rate | Byte Position & Decoding Formula | Observed Behavior & Notes | Status |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Accelerator Pedal (%)** | `0x17C` | 50 Hz | `(data[0] / 97.0f) * 100.0f` % | `0%` (Foot off pedal) ➔ `100%` (Full WOT pedal depth) | 🟡 **PEDAL SENSOR** |
| **Gear Selection** | `0x188` | 50 Hz | `data[3]` (`0x04`=P, `0x01`=R, `0x08`=N, `0x00`=D, `0x02`=S) | Updated inverted shifter permutation mapping | 🟡 **VERIFIED MAP** |
| **Vehicle Speed (km/h)** | `0x1D0` | 50 Hz | `((data[0] << 8) \| data[1]) / 10.0f` km/h | Low-speed FL wheel speed (reads 1-6 km/h crawling speed) | 🟡 **WHEEL SPREAD** |
| **Brake Light Switch** | `0x1A4` | 50 Hz | `(data[0] > 0)` | `false` (released) / `true` (light pedal touch) | 🟡 **LIGHT SWITCH** |
| **Brake Fluid Pressure** | `0x1A4` | 50 Hz | `(data[1] - 30) / 180.0f * 100.0f` % | `0 Bar` at resting idle ➔ `100 Bar` firm pressure | 🟡 **FLUID PRESSURE** |

---

## 🔴 Category C: Additional Parameters & Diagnostics

| Signal Name | Target Parameter | CAN ID Candidate | Status & Action Required |
| :--- | :--- | :--- | :--- |
| **Individual Wheel Speeds** | `w_fl`, `w_fr`, `w_rl`, `w_rr` | `0x1D0` Bytes 0-7 | 🟢 Mapped: FL (Bytes 0-1), FR (Bytes 2-3), RL (Bytes 4-5), RR (Bytes 6-7) |
| **Check Engine Light (CEL)** | `flags & 0x08` | `0x1A0` / `0x1DC` | 🔴 Check error warning bits |
| **Shift Light Warning** | `flags & 0x02` | `0x1DC` RPM > 6800 RPM | 🟢 Active at 6,800 RPM threshold |
