#!/usr/bin/env python3
"""
ESP32 Gateway CAN Bus Reverse Engineering Bridge & Analyzer
------------------------------------------------------------
Adapts CSS-Electronics CAN Reverse Engineering methodologies to work with
the Waveshare ESP32-S3-RS485-CAN Gateway over Wi-Fi (Telnet TCP:8889 / WebSockets)
or local raw CAN CSV dumps, without requiring CANsub USB hardware or DB9 cables.

Usage:
  # Analyze a raw CAN CSV dump:
  python3 tools/esp32_can_bridge.py --file espDash_raw_can_log_2026-08-06T14-32-21-348Z.csv --action "steering_left"

  # Live stream from ESP32 Gateway:
  python3 tools/esp32_can_bridge.py --ip esp32-gateway.local --listen 10
"""

import sys
import os
import time
import socket
import csv
import argparse
from typing import Dict, List, Tuple

class ESP32CANFrame:
    def __init__(self, timestamp: float, can_id: int, dlc: int, data: bytes):
        self.timestamp = timestamp
        self.can_id = can_id
        self.dlc = dlc
        self.data = data

    @property
    def hex_id(self) -> str:
        return f"0x{self.can_id:03X}"

    def __repr__(self):
        hex_data = " ".join(f"{b:02X}" for b in self.data)
        return f"[{self.timestamp:.3f}] {self.hex_id} [{self.dlc}] {hex_data}"

class ESP32CANBridge:
    def __init__(self, ip: str = "esp32-gateway.local", port: int = 8889):
        self.ip = ip
        self.port = port

    def read_csv(self, filepath: str) -> List[ESP32CANFrame]:
        frames = []
        with open(filepath, 'r') as f:
            reader = csv.reader(f)
            header = next(reader, None)
            ts = 0.0
            for row in reader:
                if not row: continue
                # Parse timestamp and CAN frame from CSV row
                try:
                    for i, val in enumerate(row):
                        if val.startswith("0x"):
                            can_id = int(val, 16)
                            dlc = int(row[i+2])
                            b_data = bytes([int(x, 16) for x in row[i+3:i+3+dlc]])
                            frames.append(ESP32CANFrame(ts, can_id, dlc, b_data))
                            ts += 0.01
                            break
                except Exception:
                    continue
        return frames

    def stream_live(self, duration_sec: float = 10.0) -> List[ESP32CANFrame]:
        frames = []
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(5.0)
        print(f"Connecting to ESP32 Gateway at {self.ip}:{self.port}...")
        s.connect((self.ip, self.port))
        s.sendall(b"MODE:RAW\n")
        print(f"Streaming raw CAN frames for {duration_sec}s...")

        start_t = time.time()
        buf = ""
        while time.time() - start_t < duration_sec:
            try:
                chunk = s.recv(1024).decode('utf-8', errors='ignore')
                if not chunk: break
                buf += chunk
                while "\n" in buf:
                    line, buf = buf.split("\n", 1)
                    line = line.strip()
                    if line.startswith("RAW:"):
                        # Format: RAW:0x200 [8] 00 12 34 56 78 9A BC DE
                        parts = line.split()
                        can_id = int(parts[0].replace("RAW:", ""), 16)
                        dlc = int(parts[1].strip("[]"))
                        data_bytes = bytes([int(x, 16) for x in parts[2:]])
                        frames.append(ESP32CANFrame(time.time() - start_t, can_id, dlc, data_bytes))
            except socket.timeout:
                continue
        s.close()
        return frames

def isolate_signal_deltas(baseline: List[ESP32CANFrame], active: List[ESP32CANFrame]):
    """
    Identifies CAN IDs and bit-ranges that change between baseline and active states.
    (Methodology adapted from CSS-Electronics reverse engineering skill)
    """
    def summarize_by_id(frame_list: List[ESP32CANFrame]):
        summary = {}
        for f in frame_list:
            if f.can_id not in summary:
                summary[f.can_id] = []
            summary[f.can_id].append(f.data)
        return summary

    base_sum = summarize_by_id(baseline)
    act_sum = summarize_by_id(active)

    print(f"\n{'CAN ID':8s} | {'Delta Bytes':20s} | {'Min (Base -> Act)':20s} | {'Max (Base -> Act)':20s}")
    print("-" * 75)

    all_ids = sorted(set(base_sum.keys()) | set(act_sum.keys()))
    for cid in all_ids:
        if cid not in base_sum or cid not in act_sum:
            continue
        base_bytes = base_sum[cid]
        act_bytes = act_sum[cid]

        dlc = len(base_bytes[0])
        changed_byte_indices = []
        for idx in range(dlc):
            b_vals = [b[idx] for b in base_bytes if len(b) > idx]
            a_vals = [a[idx] for a in act_bytes if len(a) > idx]
            if min(b_vals) != min(a_vals) or max(b_vals) != max(a_vals):
                changed_byte_indices.append(idx)

        if changed_byte_indices:
            b_str = ", ".join(f"Byte {i}" for i in changed_byte_indices)
            base_min_max = f"0x{min(base_bytes[0]):02X}..0x{max(base_bytes[0]):02X}"
            act_min_max = f"0x{min(act_bytes[0]):02X}..0x{max(act_bytes[0]):02X}"
            print(f"0x{cid:03X}   | {b_str:20s} | {base_min_max:20s} | {act_min_max:20s}")

def main():
    parser = argparse.ArgumentParser(description="ESP32 CAN Bridge for Reverse Engineering")
    parser.add_argument("--file", help="Path to raw CAN CSV file")
    parser.add_argument("--ip", default="esp32-gateway.local", help="ESP32 Gateway IP")
    parser.add_argument("--listen", type=float, help="Listen duration in seconds")
    args = parser.parse_args()

    bridge = ESP32CANBridge(ip=args.ip)
    if args.file:
        frames = bridge.read_csv(args.file)
        print(f"Loaded {len(frames)} CAN frames from {args.file}")
        if frames:
            print("First 5 frames:")
            for f in frames[:5]:
                print(f"  {f}")
    elif args.listen:
        frames = bridge.stream_live(duration_sec=args.listen)
        print(f"Captured {len(frames)} live CAN frames")

if __name__ == "__main__":
    main()
