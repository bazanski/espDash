# Capture Recipe — Finding the ABS and Traction Control Bits

## Why this is still open

ABS and TC are the only warning indicators in the dashboard with no source message behind them.
That is not an oversight — it is a data problem:

- **`0x1A0` does not exist on a 9th-gen bus.** Earlier firmware decoded ABS/TC from it, but it
  appears in none of this car's captures, nor the 2015 Civic Si trace. It shows up only on the
  2008 8th-gen car. Those badges could never have lit.
- **No available capture contains an activation.** The Si driving trace peaks at 2.68 km/h of
  front-to-rear slip with zero sustained slip windows — that gear-pull run never triggered TC. No
  bit in any VSA-related ID (`0x1A4`, `0x1AA`, `0x1B0`, `0x255`, `0x1EA`) tracks braking or slip.

A signal that never changes cannot be found by any amount of analysis. It has to be *provoked*.
One short session settles both.

## Safety first

Do this on a **private surface with room and no traffic** — an empty wet car park, a gravel yard,
or a track day. Not a public road. You need to deliberately lose traction and to brake hard enough
to trigger ABS, and you should be the only thing moving.

The gateway is listen-only and cannot affect the car. The risk here is entirely the driving.

## Procedure

Record in raw mode so nothing is filtered out:

```bash
python3 tools/log_collector.py --mode RAW --duration 300
# or use the dashboard's record button, which sends MODE:RAW itself
```

**Speak or note the wall-clock time of each event** — the correlation step needs to know roughly
when each thing happened.

1. **Baseline, 30 s.** Engine running, car stationary, nothing touched. This is the reference
   against which everything else is diffed.
2. **Normal driving, 60 s.** Gentle acceleration, gentle braking, a few turns. Gives the analyzer
   a "moving but no intervention" control so it does not mistake ordinary motion for an event.
3. **TC event ×3.** From a standstill on the loose or wet surface, floor the throttle in first so
   the front wheels spin up. Hold ~2 s. Let the car settle ~10 s between attempts. Three separate
   events matter more than one long one — a bit that fires three times in the right windows is
   conclusive, one that fires once could be coincidence.
4. **ABS event ×3.** From ~50 km/h, brake hard enough to feel the pedal pulse. That pulse is the
   confirmation ABS actually engaged; if you don't feel it, the event didn't happen and the run
   is wasted.
5. **Optional — VSA off.** Press the VSA/traction-off button and repeat one TC attempt. This is the
   only way to confirm `0x1A4` bit 28 (`ESP_DISABLED`), which opendbc names but which reads
   constant 0 in every capture so far.

## Analysis

Replay the capture and look for bits that are 0 during baseline and normal driving, and 1 only
inside the event windows. Start with `0x1A4`, `0x1AA`, `0x1B0`, `0x255` and `0x1A6` — the VSA-adjacent
IDs — but let the search cover all 45 IDs, since the answer may well be in one of the 28 still
unmapped.

Two things make this much easier than it sounds:

- **Mask the checksum and counter.** Bit 59‑62 of nearly every message is metadata and will look
  like a busy signal otherwise. `honda_checksum()` in `firmware/esp32-gateway/src/can_decode.cpp`
  identifies which IDs follow the scheme (44 of 45 do).
- **Wheel slip is computable.** `0x1D0` gives all four wheel speeds, so front-minus-rear is a
  ground-truth slip trace to correlate candidate bits against — no manual timestamp alignment needed
  for TC. For ABS, correlate against hard deceleration in the same signal.

## When you have it

Add the bit to `can_decode_frame()` in `firmware/esp32-gateway/src/can_decode.cpp`, set
`abs_active` / `tc_active`, and add the fixture and an assertion to
`firmware/esp32-gateway/test/test_decode/test_decode.cpp` so it stays correct. Then move the row in
`docs/CAN_PROTOCOL_MAP.md` from **UNMAPPED** to **LOCAL** (or **CONFIRMED** if a reference trace
ever corroborates it).

Until then the badges stay false. A warning light that cannot light is better than one that lights
for the wrong reason.
