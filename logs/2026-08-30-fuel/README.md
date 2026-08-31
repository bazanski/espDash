# 2026-08-30 — the session that solved fuel tank level

Decode with `tools/canlog_decode.py`. Full analysis in `docs/CAN_PROTOCOL_MAP.md` §I.

| File | What it is | Tank |
|---|---|---|
| `canlog_0011.bin` | stationary, low-fuel lamp lit, dash showing 39–40 km to empty | 13 % |
| `canlog_0012.bin` | same | 13 % |
| `canlog_0013.bin` | same | 13 % |
| `canlog_0014.bin` | 886 s drive on the lamp; ECON switched off mid-drive and back on; gearbox in S with manual shifts | 11 % |
| `canlog_0015.bin` | **the refuel itself**, caught mid-fill — `0x1A6` b3 climbs 40 → 105 while the pump runs | 38 → 100 % |
| `canlog_0016.bin` | 512 s drive on the resulting full tank | 100 % |

`0015` is the single most valuable recording in this project: fuel tank level had survived three
analytical searches across two prior sessions, and fifty seconds of a running fuel pump settled it
outright. It is committed in reduced form as the `civic9_user_2014_refuel.txt` test fixture, with
`0013` as `civic9_user_2014_lowfuel.txt` — the two ends of the gauge.

`canlog_0010.bin` and `canlog_0012.bin` are absent from `canlog_index.csv`: `0010` was truncated by
a power cut and `0012` never made the manifest. Both still decode.

> **The `.bin` files here are gitignored** (`*.bin` in `.gitignore`), so they live only on this
> laptop and the SD card — this README travels with the repo, the captures do not. The
> regression-critical subsets are committed separately as `.txt` fixtures under
> `firmware/esp32-gateway/test/fixtures/`. Back the originals up somewhere before reusing the card.
