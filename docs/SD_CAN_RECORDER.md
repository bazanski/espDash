# In-Car CAN Recorder (SD card, no laptop)

Records raw CAN to the SD card on `esp-rectangular-314`, streamed from the
gateway over ESP-NOW. No laptop, no Wi-Fi, no USB tether.

## Why

Every capture before this required the web dashboard connected over USB, so
real driving data only got recorded when planned in advance. The fuel-level
bug is the cost of that: the light came on at a decoded "43%" and a full tank
read "56-58%", but there was no raw dump of that drive to diagnose from. The
fuel curve needs points across a whole tank, the throttle ceiling needs a WOT
run, and the ignition-bit candidates need clean key cycles — all normal
driving that was going unrecorded.

## Using it

1. **Insert a FAT32 SD card** into the 3.14 node.
2. **Enable streaming on the gateway** — over USB serial, send `LOG:ON`
   (`LOG:OFF` to stop). This is independent of `MODE:PLOT/RAW/DUAL`, so a
   dashboard session and a recording can run at the same time.
3. **Press BOOT on the node** to start recording. The top-right of the
   display shows a red `REC` with elapsed time and MB written.
4. **Press BOOT again** to stop and close the file cleanly.
5. **Long-press BOOT (2 s)** to stop *and* unmount, so the card can be pulled
   safely.

Files land as `/sdcard/canlog_0000.bin`, `canlog_0001.bin`, … (no RTC on the
node, so the index is a counter, not a timestamp).

## Decoding

```bash
python3 tools/canlog_decode.py canlog_0000.bin --stats   # summary only
python3 tools/canlog_decode.py canlog_0000.bin -o drive.csv
```

The CSV matches the schema the web dashboard produces, so every existing
analysis script works unchanged.

`--stats` prints a Honda-checksum pass rate. That is the integrity check on
the whole wireless path: **expect ~98%, with `0x255` as the only failing ID**
(it genuinely doesn't use Honda's checksum scheme). A materially lower rate
means frames were corrupted or misaligned in transit, not that the decode is
wrong.

## Reading the indicators

| Display | Meaning |
|---|---|
| `REC 1:23  4MB` | Recording normally |
| `REC 1:23  !57` | Recording, but 57 frames were dropped — see below |
| `SD ERROR` | No card, mount failed, or a write failed |
| dim `1234MB` | Idle, showing free space |

`STATS` on the gateway reports the sending side:
`log:on batches:1234 logframes:22000 logfail:0`.

**Drop counters are deliberately visible.** A lossy capture must never look
like a clean one. Losses are counted in three independent places and all of
them survive into the file:
- `gw_dropped` — gateway ring overflowed before transmit (embedded per batch)
- node ring overflow — SD couldn't keep up
- ESP-NOW sequence gaps — packets lost over the air

## Design notes

**Bandwidth**, measured from a real 109 s drive (1,399 frames/s, 45 unique
IDs, avg DLC 6.63): variable-length encoding gives **11.3 bytes/frame ≈ 15.8
KB/s**, about 78 ESP-NOW packets/s. Verified lossless: 5,000 real frames
encoded and decoded byte-exact, timestamps included.

**SD is not the bottleneck** — SDMMC 1-bit sustains ~1-2 MB/s against a
~16 KB/s need, roughly 70× headroom. The real risk is write *latency*: cards
stall 50-250 ms for internal block erase. That's why the receive path never
touches the card directly — the ESP-NOW callback only memcpy's into a 256 KB
PSRAM ring and returns, and a separate low-priority task drains it in 8 KB
blocks. 256 KB is >15 s of buffer, far beyond any realistic stall.

**On overflow the newest record is dropped, not the oldest** — the opposite
of the gateway's live-monitoring ring. For a log, already-buffered bytes are
the ones belonging to a coherent file; discarding the newest keeps the file
valid and makes the loss explicit.

**`fsync` every 2 s**, so an ignition cut loses seconds rather than the whole
file. The format is record-oriented for the same reason: a truncated file
still decodes cleanly up to its last complete record.

**Pin sharing** — the SD (CLK=GPIO1, CMD=GPIO2, D0=GPIO42) shares GPIO1/2
with the ST7701 panel's 3-wire init SPI, and the BOOT button is GPIO0, which
is the panel's init CS. All three only become available after
`release_st7701_spi_pins()`, so `canlog_init()` and `rec_button_init()` must
stay *after* that call in `setup()`. The board is designed for this handoff —
the panel only needs those pins during init.

**A separate message type** (`ESPDASH_MSG_CANLOG = 4`) with its own parser,
`espdash_parse_canlog()`. `espdash_parse()` still hard-rejects anything that
isn't telemetry, deliberately: relaxing it would let a display node render a
log batch as telemetry. Nodes that don't record simply never call the new
parser and ignore type 4 harmlessly.

## Not yet verified on hardware

Everything above is bench-verified in software (byte-exact round-trip, all
builds, existing test suites). Still untested against real hardware:

- SD mount on the physical card (pin config is from the vendor example for
  this exact board, but unverified on this unit)
- Whether the BOOT switch is physically wired to GPIO0 on this board — if
  not, the trigger falls back to a serial command with no other design change
- Sustained ~78 packets/s over ESP-NOW while 20 Hz telemetry shares the radio
- Whether display gauges stay smooth during recording

If the link can't sustain full rate, the fallback is an ID allow-list
(logging only the ~11 decoded IDs plus the unmapped candidates cuts the rate
~3×), which still fully serves the fuel/throttle/ignition investigations that
motivated this.
