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

**No laptop needed. One button.**

1. **Insert a FAT32 SD card** into the 3.14 node.
2. **Press BOOT on the node** — it arms the gateway over ESP-NOW *and* starts
   recording. The top-right of the display shows a red `REC` with the file
   number, elapsed time and MB written.
3. **Press BOOT again** to stop: the file is closed and the gateway stops
   streaming. The indicator turns green — that means every byte is on the
   card and you can pull it or cut power.

Repeat as many times as you like in one drive — each press/press pair makes a
new numbered file.

`LOG:ON` / `LOG:OFF` over USB serial still work as a manual override for
bench testing. The two are independent: the gateway streams if *either* the
serial latch is on or a node is asking.

### Several recordings per drive

Files are `/sdcard/canlog_0000.bin`, `canlog_0001.bin`, … The node picks the
first unused index, so recordings never overwrite each other, even across
power cycles.

**The file number is shown on screen while recording**, so you can note what
you were doing ("0003 was the fill-up").

On stop, a line is appended to `/sdcard/canlog_index.csv`:

```
file,start_uptime_ms,duration_s,frames,bytes,node_dropped,gw_dropped,seq_gaps
canlog_0000.bin,184333,109,152896,1724000,0,0,0
```

There's no RTC on this board, so `start_uptime_ms` (gateway uptime at the
start of the recording) is the closest thing to a clock — it orders the
recordings and shows how far into the drive each one began. Inventing a
wall-clock timestamp would be worse than an honest relative one.

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
| green `1234MB OK` | Idle and fully flushed — **safe to pull the card or cut power** |
| dim `1234MB` | Idle, but something is still being written out |

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

**Removing the card / cutting power.** There is no unmount gesture, because
none is needed: stopping a recording closes and `fsync`s the file, so nothing
is in flight. The display shows green `OK` exactly when that is true.

A recording also **auto-stops if the gateway goes silent for 8 s** — ignition
off, out of range, or gateway rebooted — so the file gets closed properly
even if you never touch the button. The gateway emits an id-table heartbeat
every 2 s even on a completely silent CAN bus, so this only trips when the
gateway itself has actually stopped, not merely when the car is parked.

The one genuinely unsafe moment is cutting power *mid-recording*: worst case
you lose up to 2 s (the `fsync` interval) and the file simply ends at its
last complete record. That is a bounded, recoverable loss rather than a
corrupt file.

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

**Arming is a repeated state advertisement, not an on/off command.** The node
broadcasts "I want raw CAN" (`ESPDASH_MSG_NODE_CMD = 6`) twice a second while
recording, and the gateway holds that state only while requests keep
arriving, forgetting after 3 s. Edge-triggered commands would be fragile over
fire-and-forget ESP-NOW: one dropped "start" means recording an empty file,
one dropped "stop" means the gateway transmits forever. Repeating makes it
self-healing in every direction:

- lost packet → the next one lands ~500 ms later
- node switched off or out of range → gateway stops by itself, no wasted airtime
- gateway rebooted mid-recording → the node's next advert re-arms it automatically

This is also the first **upstream** message in the project (everything else
flows gateway → nodes), so the gateway gained a receive callback and the node
gained a broadcast peer.

## Not yet verified on hardware

Everything above is bench-verified in software (byte-exact round-trip, all
builds, existing test suites). Still untested against real hardware:

- SD mount on the physical card (pin config is from the vendor example for
  this exact board, but unverified on this unit)
- Whether the BOOT switch is physically wired to GPIO0 on this board — if
  not, the trigger falls back to `LOG:ON` over serial with no other design change
- Sustained ~78 packets/s over ESP-NOW while 20 Hz telemetry shares the radio
- Whether display gauges stay smooth during recording
- The upstream node→gateway path (new direction; the gateway had never
  received an ESP-NOW packet before this)

If the link can't sustain full rate, the fallback is an ID allow-list
(logging only the ~11 decoded IDs plus the unmapped candidates cuts the rate
~3×), which still fully serves the fuel/throttle/ignition investigations that
motivated this.
