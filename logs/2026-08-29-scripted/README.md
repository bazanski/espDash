# 2026-08-29 scripted subsystem captures

Five stationary recordings, engine idling in Park, one subsystem per recording. Tank at ~10 % by
the dash, low-fuel lamp **not yet lit**. Ambient 33 °C on the dash. Decode with
`tools/canlog_decode.py`. Full analysis in `docs/CAN_PROTOCOL_MAP.md` §G.

| File | Script |
|---|---|
| `canlog_0005.bin` | Two wide-open-throttle pulls into the rev limiter |
| `canlog_0006.bin` | Climate: fan speeds, A/C and ventilation modes |
| `canlog_0007.bin` | Left indicator, then right; then DRL → low beam → high beam → off in reverse, plus several more switches and one more left/right indicator pair partway through |
| `canlog_0008.bin` | Every steering-wheel button pressed |
| `canlog_0009.bin` | Central locking open/close; all four windows fully down and up |

Kept because they cost car time to produce and the SD card gets reused. `canlog_0005` is also
committed in reduced form as the `civic9_user_2014_wot.txt` gateway test fixture.

> **The `.bin` files here are gitignored** (`*.bin` in `.gitignore`), so they live only on this
> laptop and the SD card — this README travels with the repo, the captures do not. The
> regression-critical subsets are committed separately as `.txt` fixtures under
> `firmware/esp32-gateway/test/fixtures/`. Back the originals up somewhere before reusing the card.
