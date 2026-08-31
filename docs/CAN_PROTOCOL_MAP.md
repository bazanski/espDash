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
| **ON-ROAD** | Observed working on this car while driving (2026-08-09). Qualitative — "looks right" from a dashboard readout, not yet checked against a synced recording. See §E |
| **CONFIRMED** | Agrees on this car *and* at least one independent reference trace |
| **LOCAL** | Verified on this car against the instrument cluster; no external corroboration |
| **REFERENCE** | From opendbc / a reference trace; consistent with capture but not seen exercised here |
| **UNMAPPED** | No source message identified — deliberately left inert rather than guessed |

Reference data used:

| Source | What it is |
|---|---|
| `espDash_raw_can_log_2026-08-08` | This car. 112,962 frames, 80.8 s, **stationary**, engine idling and revved to ~5100 rpm, shifter cycled through P/R/N/D/S, wheel turned lock to lock, brake and throttle exercised |
| `canlog_0002/0003/0004.bin` | This car. 2026-08-15 outing, ~4.1 M frames, **37.8 km driven**. Tank ~90 % → ~80 % by the dash. The distance ground truth for the odometer decode |
| `canlog_0005…0009.bin` | This car. 2026-08-29, five scripted stationary tests, 3.5 min total, engine idling in Park, **tank at ~10 %** (low-fuel lamp not yet lit). One test per subsystem: WOT pulls, climate controls, lights + indicators, steering-wheel buttons, central locking + all four windows. The 10 %-vs-85 % pair is what finally settled fuel level |
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
| **Vehicle speed** | `0x158` | 50 Hz | `BE16(d[0],d[1]) × 0.01 km/h` | 8.69–9.75 km/h on the 10 km/h control trace. Driving on this car (2026-08-09): reads plausible, tracked expected speed | **ON-ROAD** |
| **Wheel speeds** ×4 | `0x1D0` | 25 Hz | **four 15-bit fields**, start bits 7/8/25/42, `× 0.01 km/h` | On the 10 km/h trace all four read 8.1–9.5 km/h with 0.70 km/h spread. Not individually checked against each other on this car yet — only the combined speed reading was observed | **CONFIRMED** |
| **Coolant temp** | `0x324` | 5 Hz | `d[0] − 40` °C | 83–86 °C here (dash-verified), 85–92 °C on the Si while warming | **CONFIRMED** |
| ~~Fuel level~~ ~~instant~~ **Fuel consumption (TRIP AVERAGE)** | `0x324` | 5 Hz | `d[1]` = **L/100km ×10** (94 → 9.4). Not km/L, not instantaneous — see §H | Two independent proofs on 2026-08-29. **Units:** 10 min idling in Park with the car never moving climbs the byte 89→98; only an *average L/100km* rises while burning fuel over zero distance (km/L would mean efficiency improving while stationary). **Average, not instant:** two WOT pulls to the rev limiter move it by *one count*, and over the 37-min drive it traces a textbook average — 9.4 cold, 8.0 by 24 km of motorway, back to 8.7 slowing into town. Decoded as `fuel_consumption_x10` (name kept for wire compatibility) | **CONFIRMED** |
| **Fuel level (tank)** ✅ | `0x1A6` | 50 Hz | `d[3] × 0.5` **litres**; equivalently `d[3]` **is** the percentage of the nominal 50 L tank (clamp to 100 for display). Ceiling 105 = 52.5 L = brimmed | **Found 2026-08-30 by recording an actual refuel.** Over 42 s of pumping `d[3]` climbs smoothly 40 → 105, stops dead with the pump, then sits at exactly 105 through a 512 s drive (sd 0.24 — float against its upper stop). Every dash reading ever noted agrees: ">90%"→89%, ">80%"→81%, "~10%"→17%, lamp lit→13%, "full"→100%. Never exceeds 105 in any capture. See §I | **CONFIRMED** |
| **Low-fuel warning lamp** | `0x294` | 25 Hz | `d[0] & 0x01` | Mirrored independently on `0x405 d[0] & 0x04`; the two agree in every capture. Passes the tight test: 0 at a 17% tank with the lamp *not* yet lit (2026-08-29), 1 at 13% with it lit (2026-08-30) — three raw counts apart, which every level-derived bit fails | **CONFIRMED** |
| **Engaged gear** | `0x188` | 50 Hz | `d[0]`, 1–5; `7` = shifting | rpm-per-km/h forms a clean ratio ladder over an 886 s drive — 113 / 62 / 42 / 28 for gears 1–4, steps of 1.83 / 1.47 / 1.49. Holds in both D and S, so manual shifts in the S gate are fully visible. `7` has 40 % scatter: the converter-unlocked transient, not a gear. Distinct from the **selector** in `d[3]` | **CONFIRMED** |
| **S (manual) gate** | `0x188` | 50 Hz | `d[4] & 0x10` | `d[2]` also reads `0x80` in D vs `0x08` in S. Clean split across 86 k frames | **LOCAL** |
| **ECON mode** | `0x221` | 25 Hz | `d[2] & 0x80` | Exactly one 78 s OFF window inside an 886 s drive, matching a scripted "ECON off mid-drive, then on" test | **LOCAL** |
| **Steering angle** | `0x156` | 50 Hz | `BE16s(d[0],d[1]) / 10` ° | Magnitude confirmed on the Si (spans ±500°, matching the sensor's spec). **Sign** corrected by an on-road test on this car (2026-08-09): opendbc's documented `-0.1` factor gave inverted left/right here | **ON-ROAD** |
| **Steering rate** | `0x156` | 50 Hz | `BE16s(d[2],d[3])` °/s | opendbc `STEER_ANGLE_RATE`. Sign changed to match the angle fix above; not independently re-checked | **REFERENCE** |
| **Brake pressure** | `0x1A4` | 25 Hz | `BE16(d[0],d[1]) × 0.015625 − 1.609375` | Rest ≈ 98–100 raw on both cars; range to 526 here. On-road: "looks ok" | **ON-ROAD** |
| **Brake switch** | `0x17C` | 50 Hz | `d[4] & 0x01` | opendbc `BRAKE_SWITCH` bit 32 | **REFERENCE** |
| **Accelerator pedal** | `0x17C` | 50 Hz | `d[0] / 255` | **Retuned on this car (2026-08-29).** Two deliberate WOT pulls into the limiter plateau at 211–213 for 2.5 s each — the pedal against its kickdown detent — and the 37-min road log holds 214 then 255 for 4.5 s through one kickdown, so the field is a plain 0–255 scale. `0x13C` b4 mirrors it exactly. The previous divisors (97, then the Si's 139) both clipped: at 139 the gauge read 100 % from 55 % of real travel | **CONFIRMED** |
| **Wheels moving** | `0x1B0` | 25 Hz | `d[1] & 0x10` | True 89.4 % of a driving trace, agrees with `0x1D0` speed on 88.2 % of paired samples | **CONFIRMED** |
| **Ambient temp** | `0x21E` | 10 Hz | `d[3] − 40` °C | 33 °C on 2026-08-29 with the dash reading exactly 33; 31–33 °C drifting down over the 37-min evening drive. **Was `(int8)d[4]`** — retracted, see §C and §G | **CONFIRMED** |
| **In-car temp** | `0x21E` | 10 Hz | `d[2] − 40` °C | Same offset as ambient, one byte earlier. 24 → 19 °C over the 37-min drive as the cabin cooled; 18–19 °C on a 33 °C day. Not wired to the protocol yet — no field for it | **LOCAL** |
| **Distance travelled** | `0x377` | 10 Hz | `BE16(d[1],d[2]) × 0.05 km` | **49.81 m/count** regressed against speed integrated over the 37-min drive: 758 counts for 37.79 km, residual ≤ 1.1 counts (55 m) across 38 samples an hour apart. Independently 48.7 m/count on `canlog_0003`. Frozen while idling in Park for 10 min. 16-bit, so it wraps every 3276 km — use deltas, not the absolute value | **CONFIRMED** |
| **Turn signal L / R** | `0x294` | 25 Hz | `d[0] & 0x20` left, `d[0] & 0x40` right | Left-then-right in `canlog_0007`, in that order, matching the test script exactly; held for the length of the stalk hold rather than blinking, so this is the stalk request, not the lamp | **LOCAL** |
| **Headlight level** | `0x1A6` | 50 Hz | `d[0] & 0x03`: `0`=DRL/off `1`=position `2`=low beam `3`=high beam | `canlog_0007` ramps 0→1→2→3 then back 3→2→1→0, then several more up-down switches — exactly the scripted sequence. Level 1 is inferred (the script named only DRL/low/high) | **LOCAL** |
| **Blower fan speed** | `0x510` | 2 Hz | `d[1]`, 1–5 observed | Constant `5` in every other recording; steps through 5→1→5→1→5→4→3→2→5 only while the fan was being adjusted in `canlog_0006`. Honda's dial goes to 7; only 1–5 were exercised | **LOCAL** |
| ~~**Battery voltage**~~ | `0x305` | 5 Hz | **retracted — not decoded** | Read `d[0] × 100` mV = "14.2 V". Retracted 2026-08-29: it is *exactly* 142 in every stationary capture weeks apart and 6–14 across the whole 37-min drive. A bitfield, not a measurement — see §C and §G | **UNMAPPED** |
| **Gear selector** | `0x188` | 50 Hz | `d[3]`: `01`=P `02`=R `04`=N `08`=D `00`=S | **Corrected by an on-road test (2026-08-09)** — every position was off by one against the original mapping. No external reference exists (`0x188` is absent from the Si, which is manual), so a driving car is the only oracle for this signal | **ON-ROAD** |
| **Engine RPM (alt)** | `0x1DC` | 25 Hz | `BE16(d[1],d[2])` | Tracks `0x17C` within a few rpm on both cars. Used only if `0x17C` goes stale | **CONFIRMED** |
| **VSA / ESP disabled** | `0x1A4` | 25 Hz | `d[3] & 0x10` | opendbc `ESP_DISABLED` bit 28. Constant 0 across every capture, so the bit position is **unverified** | **REFERENCE** |
| **Computer braking** | `0x1A4` | 25 Hz | `d[2] & 0x80` | opendbc `COMPUTER_BRAKING` bit 23. Constant 0 in all captures | **REFERENCE** |

`BE16` = big-endian unsigned 16-bit, `BE16s` = big-endian signed.

### The data matrix — everything the car gives us, in one place

Every row above, as a driver-facing catalogue: what it is, where it comes from, and whether a
display node can render it today. **The `key` column is the identifier used everywhere else** — in
`EspDashSignals.h`, in the gateway's telemetry JSON, and in the `SIGNALS` serial command.

| key | Signal | Unit | Range | Source | On the wire |
|---|---|---|---|---|---|
| `speed` | Vehicle speed | km/h | 0–220 | `0x158` b0-1 | v2.0 |
| `rpm` | Engine RPM | — | 0–7000 | `0x17C` b2-3 | v2.0 |
| `gear` | Selector position | P/R/N/D/S | — | `0x188` b3 | v2.0 |
| `gear_num` | **Engaged gear** | 1–5 | — | `0x188` b0 | **v2.3** |
| `throttle` | Accelerator pedal | % | 0–100 | `0x17C` b0 ÷255 | v2.0 |
| `brake` | Brake pressure | % | 0–100 | `0x1A4` b0-1 | v2.0 |
| `steering` | Steering angle | ° | ±500 | `0x156` b0-1 | v2.0 |
| `coolant` | Coolant temperature | °C | −40–140 | `0x324` b0 −40 | v2.0 |
| `ambient` | Outside temperature | °C | −40–60 | `0x21E` b3 −40 | v2.0 |
| `cabin_temp` | **In-car temperature** | °C | −40–60 | `0x21E` b2 −40 | **v2.4** |
| `fuel_level` | **Fuel tank level** | % | 0–100 | `0x1A6` b3 | **v2.3** |
| `fuel_litres` | **Fuel remaining** | L | 0–52.5 | `0x1A6` b3 ×0.5 | **v2.3** |
| `fuel_avg` | Trip-average consumption | L/100km | 0–30 | `0x324` b1 | v2.0 |
| `odo_km` | **Distance travelled** | km | delta only | `0x377` b1-2 ×0.05 | **v2.4** |
| `fan_speed` | **Blower fan** | 0–7 | — | `0x510` b1 | **v2.4** |
| `lights` | **Headlight level** | DRL/POS/LOW/HIGH | — | `0x1A6` b0 &3 | **v2.4** |
| `wheel_fl` `wheel_fr` `wheel_rl` `wheel_rr` | Individual wheel speeds | km/h | 0–220 | `0x1D0`, four 15-bit fields | v2.1 |
| `low_fuel` | **Low-fuel lamp** | flag | — | `0x294` b0 bit0 | **v2.3** |
| `econ` | **ECON mode** | flag | — | `0x221` b2 bit7 | **v2.3** |
| `sport` | **S (manual) gate** | flag | — | `0x188` b4 bit4 | **v2.3** |
| `turn_l` `turn_r` | **Turn signals** | flag | — | `0x294` b0 bit5/6 | **v2.3** |
| `brake_sw` | Brake switch | flag | — | `0x17C` b4 bit0 | v2.0 |
| `abs` `tc` `cel` | Warning lamps | flag | — | **UNMAPPED** — always false | v2.0 |
| `vsa` | VSA / ESP disabled | flag | — | `0x1A4` b3 bit4, unverified | v2.0 |

**Not available and not pretended to be:** engine oil temperature (diagnostic PID only, and the
gateway is listen-only), 12 V battery voltage (§C — retracted), distance-to-empty (searched every
bit-field on 2026-08-30, absent; the cluster computes it internally), ABS/TC/CEL (§B).

`odo_km` is a **delta source**: the wire carries a 16-bit 50 m counter that wraps every 3276 km, so
a consumer records its first reading and reports distance since. `EspDashSignals.h` does this for
you.

---

## B. Deliberately not decoded

| Signal | Why |
|---|---|
| **ABS active** | **UNMAPPED.** `0x1A0`, which earlier firmware read, does not exist on a 9th-gen bus — it appears only on the 2008 8th-gen car. No available capture contains an ABS activation. See `CAPTURE_ABS_TC.md` |
| **Traction control** | **UNMAPPED.** Same as above. The Si trace peaks at 2.68 km/h front-to-rear slip with zero sustained slip windows — that run never triggered TC |
| **Check engine light** | **UNMAPPED.** No candidate identified |
| ~~Fuel level (tank)~~ | **DECODED 2026-08-30** — moved to §A. The "strong negative result" recorded here on 2026-08-29 was wrong; §I explains exactly how the search missed a signal that was in its own output |
| ~~Low-fuel warning lamp~~ | **DECODED 2026-08-30** — moved to §A |
| **Central locking, window position** | **Not on this bus at all.** `canlog_0009` locked and unlocked the car and ran all four windows fully down and up. Across all 47 IDs not one byte moved outside its normal idle behaviour. Body functions live on Honda's B-CAN, which this gateway is not wired to |
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
| `0x156` sign | `−int16 / 10` | `int16 / 10` | On-road test (2026-08-09) showed left/right inverted. The magnitude (`/10`) was right; opendbc's negative factor did not carry over to this chassis |
| `0x188` | `04`=P `01`=R `08`=N `00`=D `02`=S | `01`=P `02`=R `04`=N `08`=D `00`=S | On-road test (2026-08-09): every position was off by one against the physical selector. No external reference exists for this ID at all — see §A |
| `0x21E` | ambient = `(int8)d[4]` | ambient = `d[3] − 40` | `d[4]` is a bitfield. Cycling the climate controls drives it through `00/21/60/80/81/A0` — **−128 °C to +96 °C on the gauge** — and it steps `0x20`→`0x60` mid-drive and stays. `0x20 = 32` only ever looked like a temperature because the two captures that "confirmed" it were both taken at ~32 °C |
| `0x305` | battery = `d[0] × 100` mV | **not decoded** | `d[0]` is *exactly* `0x8E` (=142, "14.2 V") in four stationary captures spread over three weeks — never 141, never 143 — and `0x06`/`0x0E` (0.6 V / 1.4 V) throughout a 37-min drive. Real alternator output wanders with load; this does not vary at all. Values are bit patterns (`8E/4E/0E/06` share a low nibble, differ in bits 6–7), consistent with opendbc's `SEATBELT_STATUS` |
| `PEDAL_GAS_FULL_SCALE` | `139` (2015 Si maximum) | `255` | Two deliberate WOT pulls into the limiter (2026-08-29, in Park) plateau at **211–213**, and the road log holds **214 then 255** for 4.5 s through one kickdown. At 139 the gauge reported 100 % from 55 % of real pedal travel |

---

## D. Full bus inventory

45 standard IDs observed on this car. 26 decoded or named; **19 remain unmapped** — the honest
remaining surface. Seven were added on 2026-08-29 (§G), and fuel level, the low-fuel lamp, engaged gear,
the S gate and ECON on 2026-08-30 (§I).

| ID | DLC | Hz | Meaning | ID | DLC | Hz | Meaning |
|---|---|---|---|---|---|---|---|
| `0x039` | 3 | 25 | — | `0x324` | 8 | 10 | coolant + consumption ✅ |
| `0x091` | 8 | 100 | `KINEMATICS_ALT` lat accel (opendbc) | `0x328` | 8 | 10 | — |
| `0x13C` | 8 | 100 | `GAS_PEDAL` (b4 mirrors 0x17C b0) | `0x372` | 2 | 10 | flag, **not** ambient |
| `0x156` | 6 | 100 | steering ✅ | `0x374` | 7 | 10 | `STALK_STATUS` wipers/lights |
| `0x158` | 8 | 100 | `ENGINE_DATA` speed + RPM ✅ | `0x377` | 8 | 10 | **distance travelled** ✅ |
| `0x17C` | 8 | 100 | `POWERTRAIN_DATA` ✅ | `0x378` | 8 | 10 | — |
| `0x188` | 6 | 100 | selector + engaged gear + S ✅ | `0x386` | 8 | 10 | trip computer (11-bit) |
| `0x18E` | 3 | 100 | — | `0x3A1` | 4 | 5 | — (this car only) |
| `0x1A4` | 8 | 50 | `VSA_STATUS` brake ✅ | `0x3D7` | 8 | 5 | — |
| `0x1A6` | 8 | 50 | **FUEL LEVEL** + headlights + buttons ✅ | `0x400` | 5 | 3.3 | — |
| `0x1AA` | 8 | 50 | b6 mirrors brake low byte | `0x401` | 7 | 3.3 | — (this car only) |
| `0x1B0` | 7 | 50 | `STANDSTILL` ✅ | `0x403` | 5 | 3.3 | — (this car only) |
| `0x1D0` | 8 | 50 | `WHEEL_SPEEDS` ✅ | `0x405` | 8 | 3.3 | low-fuel lamp (2nd source) ✅ |
| `0x1DC` | 4 | 50 | RPM (alt) ✅ | `0x40C` | 8 | 3.3 | **VIN**, muxed by b0 |
| `0x1EA` | 8 | 50 | `VEHICLE_DYNAMICS` lat+long accel | `0x40F` | 8 | 3.3 | — (this car only) |
| `0x1ED` | 3 | 50 | wheel-button value adjust | `0x428` | 7 | 3.3 | — |
| `0x21E` | 7 | 25 | **ambient + cabin** + HVAC ✅ | `0x42D` | 7 | 3.3 | trip computer (13-bit) |
| `0x221` | 4 | 25 | ECON mode ✅ | `0x454` | 6 | 3.3 | — |
| `0x255` | 8 | 25 | `ROUGH_WHEEL_SPEED` (**no Honda checksum**) | `0x465` | 8 | 3.3 | — |
| `0x294` | 8 | 25 | low-fuel lamp + turn signals ✅ | `0x510` | 8 | 2 | **blower fan** ✅ (this car only) |
| `0x295` | 4 | 25 | — | `0x6C1` | 1 | 3.3 | — (this car only) |
| `0x305` | 7 | 10 | flags — **not** battery | | | | |
| `0x309` | 8 | 10 | `CAR_SPEED` (opendbc) | | | | |
| `0x320` | 8 | 10 | — | | | | |

Seven IDs are unique to this car versus the 2015 Si — `0x188 0x3A1 0x401 0x403 0x40F 0x510 0x6C1` —
consistent with the automatic gearbox and UK-market/trim differences. No external source covers them.

**Worth noting for the planned GY-BNO08X IMU:** `0x091` (lateral acceleration, 100 Hz) and `0x1EA`
(`VEHICLE_DYNAMICS`, lateral + longitudinal at 0.0015 m/s² resolution) are already on the bus for
free. Both are named by opendbc and neither has been decoded yet.

---

## E. On-road test drive (2026-08-09)

First test of this decode table with the car actually moving. Gateway and round-gauge display node
both flashed and running; observations are qualitative from watching the gauges, not from a synced
recording.

**Working:**
- Vehicle speed — plausible, tracked as expected
- Brake pressure — "looks ok"
- Steering — direction was inverted; **fixed** (§C) and shipped in this update, not yet re-tested on
  road
- Gear selector — every position was off by one; **fixed** (§C) and shipped in this update, not yet
  re-tested on road

**Working but flagged for follow-up — since RESOLVED (2026-08-29):**
- **Accelerator pedal** — read a value, but "maybe not super accurate." The cause was
  `PEDAL_GAS_FULL_SCALE` in `can_decode.h`, set from the *2015 Si's* observed maximum (139), not
  this car's. The wide-open-throttle run called for here was recorded on 2026-08-29 and gave the
  real number: **255**. See §A and §G. The capture is kept as the `civic9_user_2014_wot.txt` test
  fixture, so the scale cannot silently regress.

**RESOLVED (2026-08-15) — `0x324 d[1]` is not fuel level:**
The original `d[1] / 2 %` guess is retracted. A 49-minute real outing (three recordings,
`canlog_0002/0003/0004.bin`, ~1.03M checksum-valid `0x324` frames) settled it two ways:

1. **The dash disagreed outright, not just by drifting.** Fuel read >90% before the outing, >80%
   after, with no refuel. `d[1]` never left the 80–103 raw range at *either* end of the whole
   session — under any linear scale that range cannot reach 80–90%. This was a wrong absolute
   value, not sender noise around a roughly-correct one.
2. **The real behavior is a clean, physically sensible signal — just not fuel level.** Binned by
   speed, `d[1]` is smooth and monotonic: mean 92.4 at 0–10 km/h down to 80.0 at 90–100 km/h,
   thousands of samples per bin, effectively no scatter. Scaled `d[1] / 10` it reads
   8.0–10.3 L/100km — a plausible trip-computer figure for this car, and exactly the reading a
   driver watching the *fuel gauge bar* (not the consumption readout) would never have
   cross-checked. (Credit: this reframe was the user's hypothesis, not derived from the data first —
   it fit strictly better than an earlier "sender tilt" theory floated before the speed-binned shape
   was checked.)

   > **Refined 2026-08-29 — it is the trip AVERAGE, not an instantaneous reading.** The
   > speed-binned correlation above is real but was over-read: it is a *time* artifact, because the
   > slow bins fall early in the trip and the fast ones later. See **§H** for the two experiments
   > that separate the two, and for why the units are L/100km and not km/L.

None of `0x324`'s other bytes look like a plausible tank-level signal either (byte 0 is confirmed
coolant; bytes 2/3/5 range over most of 0–255, too volatile for a slowly-draining tank; byte 6 is a
single bit; byte 7 is the checksum+counter nibble). **Actual fuel level remains UNMAPPED.**

Diagnostic-style request/response traffic was also seen on the bus this session (extended IDs
`0x18DB33F1` request / `0x18DAF10E` response, ISO 15765-4 extended addressing) — no external scan
tool was connected, so this is very likely the instrument cluster itself, polling the ECU
internally for its own trip computer. It was asking Mode 01 PIDs `0x05`/`0x0B`/`0x0C`/`0x0D`
(coolant/MAP/RPM/speed) only, not `0x2F` (standard fuel level), so it didn't resolve this. Their
answers cross-validated our own decode independently, though: PID 0x05 `0x79` → 121−40 = 81 °C,
matching `0x324` byte0's coolant reading; PID 0x0C `0x0B,0x34` → 717 RPM at idle; PID 0x0D `0x00`
→ 0 km/h stationary — all consistent. (This also suggests where `0x324 byte1` itself comes from:
the cluster likely computes and broadcasts the same consumption figure it derives from these four
inputs, rather than every module re-deriving it.)

The gateway is listen-only by design and cannot transmit a PID 0x2F request itself (see §F). If
fuel level over CAN is wanted, that needs a second, separate, transmitting OBD tool — this gateway
will not become one.

> **SOLVED 2026-08-30 — `0x1A6` byte 3.** The before/after-refuel bracketing suggested here was
> exactly the right instinct; it just needed the refuel itself on tape rather than inferred from two
> sessions. See **§A** for the decode and **§I** for why the intermediate "exhaustive negative" of
> 2026-08-29 was wrong.

**Not yet exercised this session:** ABS, TC, CEL (still UNMAPPED per §B — unaffected by this
drive), wheel-speed agreement across all four wheels individually (only the combined speed reading
was watched), oil temperature (not on the bus, unaffected).

---

## F. OBD-II (passive observation only)

The 2026-08-06 capture contains `0x18DB33F1` (functional request) and `0x18DAF10E` (physical
response); this was originally attributed to a scan tool being plugged in at the time. **That
attribution is now doubtful.** The 2026-08-15 session shows the identical pair, polling the same
four PIDs, with no external tool connected at all (see §E) — most likely the instrument cluster
itself, computing its own trip-computer consumption figure. The 2026-08-06 capture was never
independently confirmed to have a scan tool attached; it may be the same internal traffic. Either
way: standard Mode 01 PIDs and Honda's Mode 22 extended PIDs — including engine oil and ATF
temperature — are reachable **only by transmitting a request**, which this gateway will not do.

If those values are ever wanted, use a second, separate OBD dongle for calibration. The gateway
stays listen-only.

---

## G. Scripted subsystem tests (2026-08-29)

Five short stationary recordings, engine idling in Park, one subsystem exercised per recording
against a written script. That structure is what makes them decisive: an unexplained byte either
moves in exactly the recording where its subsystem was touched, or it is not that signal.

| File | Script | Outcome |
|---|---|---|
| `canlog_0005` | Two WOT pulls into the limiter. Tank ~10 % | `PEDAL_GAS_FULL_SCALE` retuned 139 → 255 |
| `canlog_0006` | Climate: fan speeds, modes, vents | Blower speed, HVAC flags, **and the ambient-temp byte was wrong** |
| `canlog_0007` | Left then right indicator; DRL → low → high → off | Indicators and headlight level, both new |
| `canlog_0008` | Every steering-wheel button | Partial — only some buttons reach this bus |
| `canlog_0009` | Central locking; all four windows down and up | **Nothing.** Not on this bus |

All five decode losslessly: 0 node-dropped, 0 gateway-dropped frames, and the Honda checksum passes
on every standard ID.

### Fuel level: an exhaustive negative — ⚠️ WRONG, retracted 2026-08-30

> Everything in this subsection was superseded ten days later: fuel level **is** on this bus, at
> `0x1A6` byte 3. The reasoning below is kept verbatim because the *way* it failed is worth more
> than the conclusion it reached — see **§I**. Do not act on it.



The 2026-08-15 outing ran at a ~85 % tank, the 2026-08-29 session at ~10 %. That pair is the
strongest bracketing this project will get without a refuel log, so it was applied exhaustively:
**every contiguous bit-field of width 4–16, at every bit offset, of every standard ID**, scored on
whether it sits near-constant inside each session and lands at roughly one eighth of its old value
in the new one.

Three candidates survived that filter. All three then failed the test that actually matters —
**fuel level must fall during a drive, and never rise**:

- `0x377` b1–b2 rose steadily *upward* through the outing (190 → 200 → 203 → 214) and kept climbing
  between sessions. It is an accumulator, and identifying it as **distance travelled** is the most
  valuable single result of this session (§A).
- `0x305` b1 takes `0xA8`/`0x14`/`0x94`/`0x24` — bit patterns, not a gauge.
- `0x188` b3 is the gear selector, already decoded.

So: **tank level is not broadcast on this car's F-CAN.** ~~This is now a supported negative result
rather than an unfinished search.~~ **It was neither. See §I.**

### `0x40C` — the VIN, in plain ASCII

`0x40C` is multiplexed: byte 0 is a frame index cycling `0x00`–`0x3F`, and indices `1`–`4` of each
block carry ASCII. Concatenated: **`SHHFK2840EU000473`** — `SHH` is Honda of the UK Manufacturing,
consistent with the seven IDs unique to this car versus the 2015 Si. `0x40F` is muxed the same way
with a shorter cycle. No telemetry value, but it explains two of the "noisy" unmapped IDs, and it
is worth knowing that anything logging raw frames from this bus is logging the VIN.

### Trip computer: `0x386` and `0x42D`

Both go to **all-ones while the car is stationary** and carry real values only once it is moving —
the signature of a computed trip figure that is undefined over zero distance.

- `0x386` b0 + top 3 bits of b1 is an **11-bit** field: `0x7FF` (invalid) at idle; 11 → 192 rising
  across the outing, falling back at the end as the drive slowed into town.
- `0x42D` b0 + top 5 bits of b1 is a **13-bit** field: `0x1FFF` (invalid) at idle; 7046 decaying
  asymptotically to 6197 — the shape of a long-window running average converging.
- `0x42D` b2–b5 are frozen for a whole ignition cycle and differ between cycles, i.e. snapshotted
  at key-on.

Average economy and distance-to-empty are the obvious candidates, but the scaling is unverified and
neither is decoded. **A single drive that photographs the i-MID trip readout at start and end
resolves both**, the same way the dash resolved `0x324 d[1]`.

### Steering-wheel buttons: partly on this bus

`0x1A6` byte 0 carries two unrelated things. The low two bits are the headlight level (§A); the
**high bits pulse for 140–200 ms per button press** — `0x20`, `0x40`, `0x80` and combinations,
seven presses in `canlog_0008`. In the same window `0x1ED` switches from `01 FF` to `40 32` then
`40 31`, a value being stepped down by one per press.

But the script pressed *every* button on the wheel and only a handful appear. The rest — like the
locks and windows — are B-CAN. Anything approaching complete steering-wheel button coverage needs
that second bus.

### Climate control

| Signal | ID | Detail |
|---|---|---|
| Blower fan speed | `0x510` b1 | 1–5 observed; decoded (§A) |
| Mode / distribution | `0x510` b0 | `0x58` default, `0x54`, `0x4C`. Three positions only — not enough to map |
| HVAC flags | `0x21E` b4 | `0x01`, `0x20`, `0x40`, `0x80`. **This is the byte that used to be read as ambient temperature** |
| HVAC flags | `0x21E` b5 | `04`/`05`/`07`/`09`/`14`/`15`/`25` |
| A/C request | `0x221` b0 | `ECON_STATUS`; bit 0 toggles with the A/C, bit 1 differs between sessions |

Enough for a "fan speed + A/C on" readout. Set temperature and vent selection would need a
recording that sweeps the temperature dial through its full range — this one did not.

### Left unresolved

- `0x378` b5 latched `0xC8` → `0xA8` one second into the lights test and never returned, through
  three later recordings. A light-related state, but not one this data separates.
- `0x374` (`STALK_STATUS` in opendbc) b5 bit 7 set for three seconds mid-test — a stalk position,
  unidentified. Wipers were never exercised.
- `0x324` b2 and `0x377` b6 both advance by one roughly every 100 s of idling. Slow accumulators,
  purpose unknown.

---

## H. `0x324` byte 1 — units and time constant, settled (2026-08-29)

Two readings of this byte were in circulation, and they disagree by more than a scale factor:

| Reading | Raw 94 means | Fuel over the 37.79 km drive |
|---|---|---|
| **L/100km ×10** (this map, gateway `main`) | 9.4 L/100km | 3.44 L |
| km/L ×10, converted as `10000 / raw` | 9.4 km/L = 10.6 L/100km | 4.15 L |

Both produce numbers a Civic could plausibly return, so arithmetic alone cannot choose. Two
experiments do.

### Experiment 1 — the units. Ten minutes of idling in Park

The 2026-08-29 session ran the engine continuously with the car **never moving**. The byte climbs
monotonically the whole time:

| Recording | Engine running for | `d[1]` |
|---|---|---|
| `canlog_0005` | 65 s | 89–90 |
| `canlog_0006` | 191 s | 93 |
| `canlog_0007` | 412 s | 96 |
| `canlog_0008` | 517 s | 97 |
| `canlog_0009` | 614 s | 98 |

Burning fuel while covering zero distance can only ever drag an **L/100km** average *upward*, and
that is what happens: 8.9 → 9.8. The km/L reading requires the opposite — fuel *efficiency*
improving from 8.9 to 9.8 km/L while the car sits still and goes nowhere, which is impossible.

**Units are L/100km ×10. The `10000 / raw` conversion is wrong** and inverts the signal's direction.

### Experiment 2 — the time constant. It is an average

The original "instantaneous consumption" label came from a speed-binned correlation over one drive
(§E). That correlation is real but was over-read: the low-speed bins fall early in the trip and the
high-speed bins later, so it is equally consistent with a slow average. Two checks separate them:

- **Two wide-open-throttle pulls to the rev limiter move the byte by one count** (`canlog_0005`).
  The single change happens at t = 1.8 s; the pulls are at 10 s and 22 s. An instantaneous reading
  would spike enormously.
- **Across the 37-min drive it traces a textbook trip average**, not a live reading:

| Distance | Speed then | `d[1]` |
|---|---|---|
| 0.00 km | stopped | 9.4 |
| 1.97 km | 66 km/h | 9.0 |
| 7.71 km | 77 km/h | 8.3 |
| 24.16 km | 77 km/h | **8.0** |
| 35.89 km | 43 km/h | 8.3 |
| 36.98 km | 48 km/h | 8.7 |

It starts high and cold at zero distance, improves as motorway kilometres accumulate, bottoms at
24 km, and rises again as the drive slows into town. An instantaneous figure would swing with every
throttle input and saturate at its maximum every time the car stopped — it does neither.

### Consequences

- The wire field stays named `fuel_consumption_x10` (append-only rule; renaming it would break every
  node in the field), but **it carries a trip average**. UI labels should say so.
- **A live instantaneous figure has to be computed**, from RPM, throttle, speed and DFCO — this byte
  cannot supply one. That is a legitimate feature, just not a decode.
- Distance is now available directly (`0x377`, §A), so a genuine trip figure can be derived and
  cross-checked against this byte rather than trusted blind.

---

## I. Fuel tank level — found, and why it took four sessions (2026-08-30)

**`0x1A6` byte 3. `raw / 105 × 100` = percent. 105 is full.**

### The capture that settled it

Six recordings were made on 2026-08-30: three with the low-fuel lamp lit and the dash showing
39–40 km to empty, one 886 s drive on that lamp, **one of the refuel itself**, and one 512 s drive
on the resulting full tank. The refuel is the whole answer:

| Time into the refuel | `0x1A6` b3 | % |
|---|---|---|
| 0 s | 40 | 38 % |
| 10 s | 57 | 54 % |
| 20 s | 71 | 68 % |
| 30 s | 86 | 82 % |
| 40 s | 102 | 97 % |
| 42 s → 50 s | **105** | **100 %** |

It climbs smoothly while the pump runs, stops dead the moment the pump does, and then holds at
exactly 105 for the whole 512 s drive afterwards. No other interpretation survives that.

### Cross-checked against every dash reading ever noted

| Session | Dash said | `b3 / 105` |
|---|---|---|
| 2026-08-15, before the outing | ">90 %" | **89 %** |
| 2026-08-15, after the outing | ">80 %" | **81 %** |
| 2026-08-29 | "~10 %", lamp not yet lit | **17 %** |
| 2026-08-30 | lamp lit, 39–40 km left | **13 %** |
| 2026-08-30 | "full tank" | **100 %** |

Two further physical checks confirm it is a real float sender:

- **On a full tank it is motionless** — sd 0.24 over 24,921 frames. The float is against its upper
  mechanical stop and physically cannot move.
- **Near empty it sloshes** — sd 4.80, swinging ±8 counts while driving, exactly as fuel moves
  around a nearly-empty tank under braking and cornering.

Byte 4 moves opposite to byte 3 but is *not* its complement (the sums drift from 186 to 127), so it
is left undecoded rather than guessed at.

### Why three earlier searches missed it

This is the useful part. The signal was inside the output of the very first search that went
looking for it, and was discarded three separate times:

1. **The whole-byte bracket (2026-08-29) ranked it 8th and it was dismissed by eye.** The row read
   `0x1A6 b3: old 85.5 (sd 6.2, min 5, max 105) vs new 17.8`. That is the correct answer, in the
   right place, with the right numbers. It was rejected as noise because of the wide min–max
   spread. **The sloshing that made it look noisy is the strongest evidence that it is a float
   gauge.** Every other candidate was rock-steady precisely because it was a frozen constant.
2. **The monotonicity scan could never have found it.** It looked for bytes that only ever decrease
   over a long drive. A sloshing float goes up and down hundreds of times per trip. The filter was
   built for the wrong physical model.
3. **The exhaustive bit-field search excluded it by one arbitrary threshold.** It required a
   candidate's full-tank value to be at least 35 % of the *byte's* range, i.e. ≥ 89 of 255. The
   observed value was 85.5. **The gauge's full scale is 105, not 255** — the assumption that a
   percentage signal would use the whole byte is what hid it, by a margin of 1.4 %.

Three lessons, in order of how much they would have saved:

- **Record the transition, don't infer it.** Two sessions at different tank levels invite exactly
  the reasoning above. Fifty seconds of an actual refuel is unarguable. The same is true of every
  slow signal: capture it *changing*.
- **Model the physics before filtering on statistics.** "Smooth and monotonic" describes a
  computed value; a mechanical float is noisy by nature. A filter encodes an assumption about what
  the signal looks like, and a wrong assumption silently deletes the answer.
- **Never assume a field uses its container's full range.** A byte holding 0–105 is not unusual.

The negative result published in §G on 2026-08-29 was stated far too confidently for something
resting on filters with assumptions baked into them. It is retracted in place rather than deleted.

### Scale: the byte is half-litres, and a full tank really does exceed 100 %

The first reading of this signal assumed 105 was "full" and scaled `raw / 105`. Gleb pushed back:
*the dash needle went above the F mark after filling — could 105 simply mean 105 %?* It could, and
it does. Two independent numbers agree:

- **Litres per count, measured against the odometer.** Over one 77.7 km outing (true distance from
  `0x377`, unrecorded gaps included) at the trip computer's own 8.4 L/100km, 6.6 L were burned for a
  13-count drop — **0.50 L per count**.
- **That puts the 105 ceiling at 52.5 L**: a 50 L tank plus ~2.5 L of filler neck. The `raw / 105`
  reading never had an explanation for why the ceiling is 105 rather than 100 or 255.

So one count = 0.5 L = one percent of the nominal 50 L tank, and `raw` **is** the percentage, with
genuine headroom above 100 % when brimmed. A third check falls out for free: the low-fuel lamp
lights at 13–14 counts = **6.5–7.0 L remaining**, which is Honda's reserve for this car.

The firmware clamps to 100 % for display — "104 %" reads like a fault — but does not rescale, so at
the low end, the only place a driver needs the number to be right, it is the raw percentage.

**One check still outstanding:** the tank went from 13 counts to 105, i.e. 92 counts pumped. Under
this reading the receipt should say **≈46 L**; under the old `raw / 105` reading it would be ≈44 L.
If a future fill is recorded end to end, note the litres and it is settled outright.

### Regression guards

`civic9_user_2014_refuel.txt` and `civic9_user_2014_lowfuel.txt` are the two ends of the gauge, held
in `firmware/esp32-gateway/test/fixtures/`. The tests assert that a full tank reads exactly 100 %
(which pins `FUEL_LEVEL_FULL_RAW = 105`) and that the lamp is asserted for the whole low capture and
nowhere in the refuel. **Do not delete these fixtures** — they cost a tank of fuel to produce.
