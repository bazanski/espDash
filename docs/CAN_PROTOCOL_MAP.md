# Honda Civic 2014 (9th Gen) — CAN Bus Protocol Map

Authoritative decode map for `espDash`. Bus: **F-CAN, 500 kbps, 11-bit standard IDs**.
The gateway is `TWAI_MODE_LISTEN_ONLY` and never transmits.

The implementation of this table is `firmware/esp32-gateway/src/can_decode.cpp`, and it is
regression-tested against recorded traces by `pio test -e native -d firmware/esp32-gateway`.

---

## Sources and confidence

Every row below is tagged with the evidence behind it. Nothing here is asserted from a single
source alone unless it says so.

| Tag | Meaning |
|---|---|
| **CONFIRMED** | Agrees on this car *and* at least one independent reference trace |
| **LOCAL** | Verified on this car against the instrument cluster; no external corroboration |
| **REFERENCE** | From opendbc / a reference trace; consistent with capture but not seen exercised here |
| **UNMAPPED** | No source message identified — deliberately left inert rather than guessed |

Reference data used:

| Source | What it is |
|---|---|
| `espDash_raw_can_log_2026-08-08` | This car. 112,962 frames, 80.8 s, **stationary**, engine idling and revved to ~5100 rpm, shifter cycled through P/R/N/D/S, wheel turned lock to lock, brake and throttle exercised |
| rusEFI `OEM-Docs/Honda/civic-2015-si-9gen/1-2-3.trc` | **2015 Civic Si, same 9th generation.** 151,221 frames, driving. Shares **38 of this car's 45 IDs (84%) with identical DLCs** |
| rusEFI `OEM-Docs/Honda/2008-civic-5d-r18k2/...-driving-10kmh.trc` | 2008 Civic, 8th gen, recorded **at a known 10 km/h** — a ground-truth control for speed signals |
| [opendbc](https://github.com/commaai/opendbc) `dbc/generator/honda/_honda_common.dbc` | Honda-generic definitions. Targets 2016+ Honda Sensing cars, so it is a strong prior, **not authority** for this chassis — it is demonstrably wrong on `0x324` and `0x305` here |
| [Knio/carhack](https://github.com/Knio/carhack/blob/master/Cars/Honda.markdown) | 8th-gen community notes; independently gives `0x17C` bytes 2‑3 as RPM |

### Honda checksum — a free correctness oracle

Nearly every message carries a **4-bit checksum in the low nibble of its last byte** (DBC bit 59)
and a **2-bit rolling counter** (bit 61). Verified across all 112,962 frames of this car's capture:
**44 of 45 IDs pass at exactly 100.0%** (`0x255` is the sole exception and uses a different scheme).

Two consequences worth internalising:

- **The last byte of an 8-byte message is metadata, not signal.** Any decode that reads it is wrong.
- Frames can be validated before use. `can_decode.cpp` rejects any frame failing the checksum, so a
  corrupted frame can never drive a gauge. All three reference traces pass at 100%.

```
checksum = (8 - (sum of ID nibbles + sum of all data nibbles, excluding the checksum nibble)) & 0xF
```

---

## A. Decoded signals

| Signal | ID | Rate | Decode | Evidence | Status |
|---|---|---|---|---|---|
| **Engine RPM** | `0x17C` | 50 Hz | `BE16(d[2],d[3])` | 644–5115 here, 616–3653 on the Si, and carhack documents the same bytes for 8th gen | **CONFIRMED** |
| **Vehicle speed** | `0x158` | 50 Hz | `BE16(d[0],d[1]) × 0.01 km/h` | 8.69–9.75 km/h on the 10 km/h control trace | **CONFIRMED** |
| **Wheel speeds** ×4 | `0x1D0` | 25 Hz | **four 15-bit fields**, start bits 7/8/25/42, `× 0.01 km/h` | On the 10 km/h trace all four read 8.1–9.5 km/h with 0.70 km/h spread | **CONFIRMED** |
| **Coolant temp** | `0x324` | 5 Hz | `d[0] − 40` °C | 83–86 °C here (dash-verified), 85–92 °C on the Si while warming | **CONFIRMED** |
| **Fuel level** | `0x324` | 5 Hz | `d[1] / 2` % | 50.5–51 % here (matches tank), 28–31.5 % on the Si | **CONFIRMED** |
| **Steering angle** | `0x156` | 50 Hz | `−BE16s(d[0],d[1]) / 10` ° | −492…+514° on the Si, inside the sensor's ±500° spec | **CONFIRMED** |
| **Steering rate** | `0x156` | 50 Hz | `−BE16s(d[2],d[3])` °/s | opendbc `STEER_ANGLE_RATE`, consistent with capture | **REFERENCE** |
| **Brake pressure** | `0x1A4` | 25 Hz | `BE16(d[0],d[1]) × 0.015625 − 1.609375` | Rest ≈ 98–100 raw on both cars; range to 526 here | **CONFIRMED** |
| **Brake switch** | `0x17C` | 50 Hz | `d[4] & 0x01` | opendbc `BRAKE_SWITCH` bit 32 | **REFERENCE** |
| **Accelerator pedal** | `0x17C` | 50 Hz | `d[0] / 139` | Max 97 here (stationary only), 139 on the Si under real pulls; `0x13C` b4 agrees exactly | **CONFIRMED** |
| **Wheels moving** | `0x1B0` | 25 Hz | `d[1] & 0x10` | True 89.4 % of a driving trace, agrees with `0x1D0` speed on 88.2 % of paired samples | **CONFIRMED** |
| **Ambient temp** | `0x21E` | 10 Hz | `(int8)d[4]` °C | 32 °C here (matches dash), 2 °C on the Si's cold-weather drive | **CONFIRMED** |
| **Battery voltage** | `0x305` | 5 Hz | `d[0] × 100` mV | 14.2 V, dash-verified. opendbc calls this `SEATBELT_STATUS` — does not apply here | **LOCAL** |
| **Gear selector** | `0x188` | 50 Hz | `d[3]`: `04`=P `01`=R `08`=N `00`=D `02`=S | All five values occur in this car's shifter-movement segment. **Absent from the Si** (manual gearbox), so no external check exists | **LOCAL** |
| **Engine RPM (alt)** | `0x1DC` | 25 Hz | `BE16(d[1],d[2])` | Tracks `0x17C` within a few rpm on both cars. Used only if `0x17C` goes stale | **CONFIRMED** |
| **VSA / ESP disabled** | `0x1A4` | 25 Hz | `d[3] & 0x10` | opendbc `ESP_DISABLED` bit 28. Constant 0 across every capture, so the bit position is **unverified** | **REFERENCE** |
| **Computer braking** | `0x1A4` | 25 Hz | `d[2] & 0x80` | opendbc `COMPUTER_BRAKING` bit 23. Constant 0 in all captures | **REFERENCE** |

`BE16` = big-endian unsigned 16-bit, `BE16s` = big-endian signed.

## B. Deliberately not decoded

| Signal | Why |
|---|---|
| **ABS active** | **UNMAPPED.** `0x1A0`, which earlier firmware read, does not exist on a 9th-gen bus — it appears only on the 2008 8th-gen car. No available capture contains an ABS activation. See `CAPTURE_ABS_TC.md` |
| **Traction control** | **UNMAPPED.** Same as above. The Si trace peaks at 2.68 km/h front-to-rear slip with zero sustained slip windows — that run never triggered TC |
| **Check engine light** | **UNMAPPED.** No candidate identified |
| **Oil temperature** | **Not on the broadcast bus at all** in any of the four captures. It exists only as a Mode-22 diagnostic PID, which requires transmitting a request. The gateway is listen-only by design, so this stays 0 — it is not an unfinished gap |

## C. Corrections applied (and what they were before)

| ID | Previously | Now | Why it mattered |
|---|---|---|---|
| `0x1D0` | four aligned 16-bit words `/10` | four packed **15-bit** fields `×0.01` | Old reading gives 166 / 328 / 666 / 1297 km/h on a trace recorded at 10 km/h — each wheel ~2× the previous, the signature of bit misalignment |
| `0x158` | `d[0] − 40` as **coolant** | `ENGINE_DATA` — speed b0‑1, RPM b2‑3 | It was never a temperature; the guard `d[0] > 0` was masking it |
| `0x156` | `int16 / 9.0` | `−int16 / 10` | Old scale gave ±548–572°, beyond any physical lock, **and the sign was inverted** so left/right were swapped |
| `0x1A4` | `(d[1] − 30)/180 × 100` | 16-bit `USER_BRAKE` | Reading byte 1 alone showed **~38 % brake at rest**, and any raw value above 255 wrapped — hard braking read as 0 % |
| `0x17C` | `d[0] / 97` | `d[0] / 139` | 97 was just the highest value reached while revving stationary; the gauge saturated at ~70 % of real pedal travel |
| `0x372` | overwrote ambient temp | **not decoded** | DLC 2, byte 1 is pure counter+checksum, and byte 0 only ever takes `{0, 32}` *during a drive* — a flag (`0x20`) that coincidentally matched a 32 °C reading |
| `0x1A0`, `0x1D6` | decoded | **removed** | Present in none of the four captures |

---

## D. Full bus inventory

45 standard IDs observed on this car. 17 decoded or named; **28 remain unmapped** — the honest
remaining surface.

| ID | DLC | Hz | Meaning | ID | DLC | Hz | Meaning |
|---|---|---|---|---|---|---|---|
| `0x039` | 3 | 25 | — | `0x324` | 8 | 10 | coolant + fuel ✅ |
| `0x091` | 8 | 100 | `KINEMATICS_ALT` lat accel (opendbc) | `0x328` | 8 | 10 | — |
| `0x13C` | 8 | 100 | `GAS_PEDAL` (b4 mirrors 0x17C b0) | `0x372` | 2 | 10 | flag, **not** ambient |
| `0x156` | 6 | 100 | steering ✅ | `0x374` | 7 | 10 | `STALK_STATUS` wipers/lights |
| `0x158` | 8 | 100 | `ENGINE_DATA` speed + RPM ✅ | `0x377` | 8 | 10 | — |
| `0x17C` | 8 | 100 | `POWERTRAIN_DATA` ✅ | `0x378` | 8 | 10 | — |
| `0x188` | 6 | 100 | gear selector ✅ | `0x386` | 8 | 10 | — |
| `0x18E` | 3 | 100 | — | `0x3A1` | 4 | 5 | — (this car only) |
| `0x1A4` | 8 | 50 | `VSA_STATUS` brake ✅ | `0x3D7` | 8 | 5 | — |
| `0x1A6` | 8 | 50 | — | `0x400` | 5 | 3.3 | — |
| `0x1AA` | 8 | 50 | b6 mirrors brake low byte | `0x401` | 7 | 3.3 | — (this car only) |
| `0x1B0` | 7 | 50 | `STANDSTILL` ✅ | `0x403` | 5 | 3.3 | — (this car only) |
| `0x1D0` | 8 | 50 | `WHEEL_SPEEDS` ✅ | `0x405` | 8 | 3.3 | `DOORS_STATUS` |
| `0x1DC` | 4 | 50 | RPM (alt) ✅ | `0x40C` | 8 | 3.3 | — |
| `0x1EA` | 8 | 50 | `VEHICLE_DYNAMICS` lat+long accel | `0x40F` | 8 | 3.3 | — (this car only) |
| `0x1ED` | 3 | 50 | `HUD_SETTING` imperial flag | `0x428` | 7 | 3.3 | — |
| `0x21E` | 7 | 25 | ambient ✅ | `0x42D` | 7 | 3.3 | — |
| `0x221` | 4 | 25 | `ECON_STATUS` | `0x454` | 6 | 3.3 | — |
| `0x255` | 8 | 25 | `ROUGH_WHEEL_SPEED` (**no Honda checksum**) | `0x465` | 8 | 3.3 | — |
| `0x294` | 8 | 25 | — | `0x510` | 8 | 2 | — (this car only) |
| `0x295` | 4 | 25 | — | `0x6C1` | 1 | 3.3 | — (this car only) |
| `0x305` | 7 | 10 | battery ✅ | | | | |
| `0x309` | 8 | 10 | `CAR_SPEED` (opendbc) | | | | |
| `0x320` | 8 | 10 | — | | | | |

Seven IDs are unique to this car versus the 2015 Si — `0x188 0x3A1 0x401 0x403 0x40F 0x510 0x6C1` —
consistent with the automatic gearbox and UK-market/trim differences. No external source covers them.

**Worth noting for the planned GY-BNO08X IMU:** `0x091` (lateral acceleration, 100 Hz) and `0x1EA`
(`VEHICLE_DYNAMICS`, lateral + longitudinal at 0.0015 m/s² resolution) are already on the bus for
free. Both are named by opendbc and neither has been decoded yet.

---

## E. OBD-II (passive observation only)

The 2026-08-06 capture contains `0x18DB33F1` (functional request) and `0x18DAF10E` (physical
response) because a scan tool was plugged in at the time. Standard Mode 01 PIDs and Honda's Mode 22
extended PIDs — including engine oil and ATF temperature — are reachable **only by transmitting a
request**, which this gateway will not do.

If those values are ever wanted, use a second, separate OBD dongle for calibration. The gateway
stays listen-only.
