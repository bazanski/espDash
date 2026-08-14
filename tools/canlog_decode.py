#!/usr/bin/env python3
"""
Decode a .bin CAN log recorded on the SD card by esp-rectangular-314 into the
same CSV schema the web dashboard produces, so every existing analysis script
works unchanged.

    python3 tools/canlog_decode.py canlog_0000.bin -o drive.csv
    python3 tools/canlog_decode.py canlog_0000.bin --stats

File format (see firmware/display-nodes/esp-rectangular-314/src/canlog.cpp):
    header: "ESPDASHCANLOG" + u8 version + pad to 16 bytes
    records: u8 type, u16 len, payload
      type 1 = frame batch: EspDashCanLogHdr {u32 base_ms, u8 count,
               u8 flags, u16 gw_dropped} then `count` frames of
               u8 id_idx, u8 dlc, u16 ts_delta, [u16 raw_id if idx==0xFF],
               u8 data[dlc]
      type 2 = id table: u8 count, u8 reserved, u16 ids[count]

Record-oriented on purpose: a file truncated by an ignition cut still decodes
cleanly up to the last complete record.
"""

import argparse
import struct
import sys
from collections import Counter

MAGIC = b"ESPDASHCANLOG"
REC_BATCH = 1
REC_IDS = 2
ID_ESCAPE = 0xFF


def honda_checksum(can_id, data):
    """Honda's 4-bit checksum, low nibble of the last byte. 44 of this car's
    45 IDs satisfy it, so it doubles as an integrity check on the wireless
    path: if frames were corrupted or misaligned in transit, this catches it."""
    s = 0
    a = can_id
    while a > 0:
        s += a & 0xF
        a >>= 4
    for i, x in enumerate(data):
        if i == len(data) - 1:
            x >>= 4
        s += (x & 0xF) + (x >> 4)
    return (8 - s) & 0xF


def decode(path):
    """Yields (abs_ms, can_id, dlc, data_bytes). Also returns stats via the
    generator's .stats attribute once exhausted."""
    with open(path, "rb") as f:
        blob = f.read()

    if not blob.startswith(MAGIC):
        raise SystemExit(f"{path}: not an espDash canlog file")
    version = blob[13]
    if version != 1:
        print(f"warning: unknown file version {version}, attempting anyway", file=sys.stderr)

    pos = 16
    id_table = []
    frames = []
    stats = {
        "batches": 0,
        "id_tables": 0,
        "gw_dropped_max": 0,
        "truncated": False,
        "escaped_ids": 0,
    }

    while pos + 3 <= len(blob):
        rtype = blob[pos]
        rlen = struct.unpack_from("<H", blob, pos + 1)[0]
        pos += 3
        if pos + rlen > len(blob):
            stats["truncated"] = True
            break
        payload = blob[pos:pos + rlen]
        pos += rlen

        if rtype == REC_IDS:
            if len(payload) < 2:
                continue
            count = payload[0]
            ids = []
            for i in range(count):
                off = 2 + i * 2
                if off + 2 > len(payload):
                    break
                ids.append(struct.unpack_from("<H", payload, off)[0])
            if ids:
                id_table = ids
            stats["id_tables"] += 1

        elif rtype == REC_BATCH:
            if len(payload) < 8:
                continue
            base_ms, count, _flags, gw_dropped = struct.unpack_from("<IBBH", payload, 0)
            stats["batches"] += 1
            stats["gw_dropped_max"] = max(stats["gw_dropped_max"], gw_dropped)

            p = 8
            for _ in range(count):
                if p + 4 > len(payload):
                    break
                idx = payload[p]
                dlc = payload[p + 1] & 0x0F
                ts_delta = struct.unpack_from("<H", payload, p + 2)[0]
                p += 4
                if idx == ID_ESCAPE:
                    if p + 2 > len(payload):
                        break
                    can_id = struct.unpack_from("<H", payload, p)[0]
                    p += 2
                    stats["escaped_ids"] += 1
                else:
                    can_id = id_table[idx] if idx < len(id_table) else None
                if p + dlc > len(payload):
                    break
                data = payload[p:p + dlc]
                p += dlc
                if can_id is None:
                    # id table not seen yet (recording started mid-stream and
                    # the periodic table hasn't arrived). Skip rather than
                    # emit a wrong id.
                    continue
                frames.append((base_ms + ts_delta, can_id, dlc, data))

    return frames, stats


def main():
    ap = argparse.ArgumentParser(description="Decode espDash SD .bin CAN logs to CSV")
    ap.add_argument("input")
    ap.add_argument("-o", "--output", help="CSV output path (default: alongside input)")
    ap.add_argument("--stats", action="store_true", help="print summary, skip CSV")
    args = ap.parse_args()

    frames, stats = decode(args.input)
    if not frames:
        raise SystemExit("no frames decoded")

    frames.sort(key=lambda f: f[0])
    span = (frames[-1][0] - frames[0][0]) / 1000.0

    # Integrity: the Honda checksum is an independent oracle on the whole
    # wireless path. A low pass rate means corruption, not a decode bug.
    per_id = Counter()
    ok_id = Counter()
    for _ms, cid, dlc, data in frames:
        per_id[cid] += 1
        if data and (data[-1] & 0xF) == honda_checksum(cid, list(data)):
            ok_id[cid] += 1
    total = sum(per_id.values())
    total_ok = sum(ok_id.values())

    print(f"file      : {args.input}")
    print(f"frames    : {len(frames)}")
    print(f"span      : {span:.1f} s  ({len(frames)/span:.0f} fps)" if span > 0 else "span: n/a")
    print(f"unique ids: {len(per_id)}")
    print(f"batches   : {stats['batches']}   id tables: {stats['id_tables']}")
    print(f"checksum  : {total_ok}/{total} pass ({100.0*total_ok/total:.1f}%)")
    if stats["gw_dropped_max"]:
        print(f"WARNING   : gateway reported {stats['gw_dropped_max']} dropped frames")
    if stats["truncated"]:
        print("WARNING   : file truncated (power cut?) - decoded up to last complete record")
    if stats["escaped_ids"]:
        print(f"note      : {stats['escaped_ids']} frames used the raw-id escape")

    bad = [(c, ok_id[c], per_id[c]) for c in per_id if ok_id[c] != per_id[c]]
    if bad:
        print("ids failing checksum:")
        for cid, ok, tot in sorted(bad, key=lambda x: -x[2])[:10]:
            print(f"  0x{cid:03X}: {ok}/{tot}")

    if args.stats:
        return

    out = args.output or args.input.rsplit(".", 1)[0] + ".csv"
    with open(out, "w") as f:
        f.write("ISO_Timestamp,Gateway_Time_ms,CAN_ID,RTR,DLC,"
                "Byte0,Byte1,Byte2,Byte3,Byte4,Byte5,Byte6,Byte7\n")
        for ms, cid, dlc, data in frames:
            cells = ",".join(f"{b:02X}" for b in data)
            # ISO column left empty: the node has no RTC, and inventing a
            # timestamp would be worse than an honest blank. Gateway_Time_ms
            # is the real time base and is what the analysis scripts use.
            f.write(f",{ms},0x{cid:03X},0,{dlc}" + ("," + cells if cells else "") + "\n")
    print(f"wrote     : {out}")


if __name__ == "__main__":
    main()
