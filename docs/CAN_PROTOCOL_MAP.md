# 📊 2014 Honda Civic (9th Gen) CAN Bus Protocol Map

Reverse-engineered from live 500 kbps F-CAN bus dumps.

---

## 🏎️ Broadcast F-CAN Bus Value Table (11-bit CAN IDs)

| CAN ID | Frequency | Parameter Name | Decoding Formula / Unit | Sample Raw Hex | Live Value |
| :---: | :---: | :--- | :--- | :--- | :--- |
| **`0x17C`** | 100 Hz | **Engine Speed (RPM)** | `(Byte[2] << 8) \| Byte[3]` | `00 00 02 BD 00 00 00 19` | **701 RPM** (Idle) |
| **`0x1A4`** | 50 Hz | **Throttle Position / Load** | `Byte[1]` (0–255 scale) | `00 66 00 00 00 00 00 3A` | **40 %** |
| **`0x091`** | 50 Hz | **Calculated Engine Load** | `Byte[0]` (%) | `80 2C 87 ED FF 00 00 2E` | **50.1 %** |
| **`0x1D0`** | 10 Hz | **Coolant Temperature** | `Byte[0] - 40` (°C) | `00 80 00 00 00 00 00 0A` | **88.0 °C** |
| **`0x156`** | 50 Hz | **Vehicle Speed & Gear** | `Byte[1] / 2` (km/h), `Byte[4]` (Gear) | `FF B8 00 02 07 3F` | **0 km/h (P / N / D)** |
| **`0x1AA`** | 50 Hz | **Steering Wheel Angle** | `(int16_t)((Byte[0]<<8)\|Byte[1])` (deg) | `7F FF 00 00 00 00 66 30` | **0.0°** |
| **`0x1B0`** | 50 Hz | **Brake Pedal Pressure** | `Byte[1]` (Hydraulic Pressure) | `00 0F 00 00 00 00 3A` | **15 Bar** |
| **`0x1DC`** | 20 Hz | **Target Idle Setpoint** | `(Byte[1] << 8) \| Byte[2]` (RPM) | `02 02 BC 30` | **700 RPM Target** |
| **`0x13C`** | 10 Hz | **Fuel Tank Level & Range**| `Byte[1]` (Fuel Level %) | `00 4D 00 98 00 00 04 20` | **77 % Fuel** |
| **`0x305`** | 5 Hz | **Ambient Outdoor Air Temp**| `Byte[0] - 40` (°C) | `8E 14 00 00 00 00 05` | **22.0 °C** |
| **`0x309`** | 5 Hz | **12V Battery Voltage** | `Byte[1] / 10.0` (Volts) | `00 8A 00 00 00 00 00 0C` | **13.8 Volts** |

---

## 🔬 OBD-II Diagnostic Value Table (29-bit CAN IDs)

* **Request ID:** `0x18DB33F1` $\to$ **Response ID:** `0x18DAF10E`

| Mode / PID | Parameter Name | Decoding Formula | Live Value Captured |
| :---: | :--- | :--- | :--- |
| **`01 0C`** | **Engine Speed RPM** | `((Byte[3] << 8) \| Byte[4]) / 4.0` | **701.0 RPM** |
| **`01 05`** | **Engine Coolant Temp** | `Byte[3] - 40` (°C) | **92.0 °C** |
| **`01 0D`** | **Vehicle Speed** | `Byte[3]` (km/h) | **0 km/h** |
| **`01 5C`** | **Engine Oil Temperature** | `Byte[3] - 40` (°C) | **83.0 °C** |
