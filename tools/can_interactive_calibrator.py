#!/usr/bin/env python3
"""
Interactive CAN Calibration & Diagnostic Tool for Honda 2014 Civic (espDash)
-----------------------------------------------------------------------------
Usage:
  python3 tools/can_interactive_calibrator.py --ip 10.0.0.x
  python3 tools/can_interactive_calibrator.py --file espDash_raw_can_log_2026-08-06T14-32-21-348Z.csv
"""

import sys
import os
import time
import socket
import argparse
import csv

def analyze_dump(filepath):
    print(f"\n==========================================================================")
    print(f" ANALYZING DUMP FILE: {os.path.basename(filepath)}")
    print(f"==========================================================================")
    
    can_frames = {}
    with open(filepath, 'r') as f:
        reader = csv.reader(f)
        header = next(reader, None)
        for row in reader:
            if not row: continue
            for i, val in enumerate(row):
                if val.startswith("0x"):
                    cid = val.upper()
                    dlc = int(row[i+2])
                    b = [int(x, 16) for x in row[i+3:i+3+dlc]]
                    if cid not in can_frames:
                        can_frames[cid] = []
                    can_frames[cid].append(b)
                    break

    print(f"Found {len(can_frames)} unique CAN IDs across dump.\n")
    print(f"{'CAN ID':10s} | {'Count':7s} | {'DLC':4s} | {'Byte 0 Range':14s} | {'Byte 1 Range':14s} | {'Byte 2 Range':14s} | {'Byte 3 Range':14s}")
    print("-" * 90)

    for cid in sorted(can_frames.keys()):
        frames = can_frames[cid]
        dlc = len(frames[0]) if frames else 0
        b_ranges = []
        for byte_idx in range(min(4, dlc)):
            b_vals = [f[byte_idx] for f in frames if len(f) > byte_idx]
            min_v, max_v = min(b_vals), max(b_vals)
            b_ranges.append(f"{min_v:3d}..{max_v:3d} (0x{min_v:02X}..0x{max_v:02X})")
        
        r0 = b_ranges[0] if len(b_ranges) > 0 else "N/A"
        r1 = b_ranges[1] if len(b_ranges) > 1 else "N/A"
        r2 = b_ranges[2] if len(b_ranges) > 2 else "N/A"
        r3 = b_ranges[3] if len(b_ranges) > 3 else "N/A"
        print(f"{cid:10s} | {len(frames):7d} | {dlc:4d} | {r0:14s} | {r1:14s} | {r2:14s} | {r3:14s}")

import subprocess
import re

ESPRESSIF_MAC_PREFIXES = (
    "1c:db:d4", "24:0a:c4", "24:62:ab", "30:ae:a4", "70:b8:f6", "7c:df:a1", "84:f7:03", "8c:aa:b5",
    "a4:cf:12", "c4:4f:33", "e0:e2:e6", "e8:9f:6d", "ec:62:60", "f4:12:fa", "34:85:18",
    "34:b7:da", "40:22:d8", "48:e7:29", "54:43:b2", "60:55:f9", "64:e8:33", "68:b6:b3",
    "7c:9e:bd", "80:65:99", "90:38:0C", "a0:20:a6", "a4:7b:9d", "ac:67:b2", "b4:3a:45",
    "b4:e6:2d", "bc:dd:c2", "bc:ff:4d", "c8:2b:96", "cc:50:e3", "d8:a0:1d", "dc:54:75"
)

def get_mac_for_ip(ip):
    try:
        output = subprocess.check_output(["arp", "-a", ip], stderr=subprocess.STDOUT).decode('utf-8')
        match = re.search(r'([0-9a-fA-F]{1,2}[:-][0-9a-fA-F]{1,2}[:-][0-9a-fA-F]{1,2}[:-][0-9a-fA-F]{1,2}[:-][0-9a-fA-F]{1,2}[:-][0-9a-fA-F]{1,2})', output)
        if match:
            mac_raw = match.group(1).replace('-', ':')
            # Normalize MAC to 2-digit zero-padded format
            parts = [f"{int(p, 16):02x}" for p in mac_raw.split(':')]
            return ":".join(parts)
    except Exception:
        pass
    return None

def is_valid_esp32_mac(mac):
    if not mac:
        return False
    mac_lower = mac.lower()
    return any(mac_lower.startswith(prefix) for prefix in ESPRESSIF_MAC_PREFIXES)

def resolve_gateway_ip(target_ip=None):
    if target_ip and target_ip != "auto" and not target_ip.endswith(".local"):
        mac = get_mac_for_ip(target_ip)
        if mac:
            print(f"🛡️ Target {target_ip} MAC: {mac.upper()} (ESP32 Validated: {'YES' if is_valid_esp32_mac(mac) else 'NO'})")
        return target_ip

    hostname = target_ip if (target_ip and target_ip.endswith(".local")) else "esp32-gateway.local"
    print(f"🔍 Resolving Gateway IP via mDNS ({hostname})...")
    try:
        ip = socket.gethostbyname(hostname)
        mac = get_mac_for_ip(ip)
        print(f"✨ Resolved {hostname} -> {ip} | MAC: {mac.upper() if mac else 'UNKNOWN'}")
        if mac and not is_valid_esp32_mac(mac):
            print(f"⚠️ Warning: MAC {mac} does not match standard Espressif OUI prefix list.")
        return ip
    except Exception as e:
        print(f"⚠️ mDNS resolution failed for {hostname}: {e}")
        if target_ip and target_ip != "auto":
            return target_ip
        return None

def live_serial_stream(port_name=None, baudrate=115200, raw_mode=False, dual_mode=False):
    import glob
    import serial

    if not port_name:
        candidates = glob.glob("/dev/cu.usbmodem*") + glob.glob("/dev/cu.usbserial*")
        if candidates:
            port_name = candidates[0]
        else:
            print("❌ No USB Serial devices (/dev/cu.usbmodem* or /dev/cu.usbserial*) found.")
            return

    log_file_path = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "calibration_last_run.log")
    mode_str = "DUAL (RAW CAN + TELEMETRY JSON)" if dual_mode else ("RAW CAN FRAMES (MODE:RAW)" if raw_mode else "TELEMETRY JSON (MODE:PLOT)")
    print(f"\n==========================================================================")
    print(f" CONNECTING TO LIVE CENTRAL GATEWAY VIA USB SERIAL: {port_name} @ {baudrate} baud")
    print(f" MODE: {mode_str}")
    print(f" LOG FILE: {log_file_path}")
    print(f"==========================================================================")
    print("Press Ctrl+C to stop logging.\n")

    try:
        ser = serial.Serial(port_name, baudrate, timeout=1.0)
        print(f"✅ Connected to USB Serial Port at {port_name}")

        if dual_mode:
            ser.write(b"MODE:DUAL\n")
            print("📡 Sent command 'MODE:DUAL' -> Gateway streaming raw CAN + telemetry JSON simultaneously!\n")
        elif raw_mode:
            ser.write(b"MODE:RAW\n")
            print("📡 Sent command 'MODE:RAW' -> Gateway streaming raw CAN frames!\n")
        else:
            ser.write(b"MODE:PLOT\n")
            print("📡 Sent command 'MODE:PLOT' -> Gateway streaming telemetry JSON!\n")

        with open(log_file_path, "w") as log_f:
            while True:
                line = ser.readline().decode('utf-8', errors='ignore')
                if not line: continue
                sys.stdout.write(line)
                sys.stdout.flush()
                log_f.write(line)
                log_f.flush()
    except KeyboardInterrupt:
        print(f"\n\n⏹️ Live USB serial logging stopped by user. Log saved to: {log_file_path}")
    except Exception as e:
        print(f"\n❌ USB Serial connection error: {e}")

def live_telnet_stream(ip=None, port=8889, raw_mode=False, dual_mode=False):
    resolved_ip = resolve_gateway_ip(ip)
    if not resolved_ip:
        print("❌ Could not determine Gateway IP address. Please specify with --ip <IP_OR_HOSTNAME>")
        return

    log_file_path = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "calibration_last_run.log")
    mode_str = "DUAL (RAW CAN + TELEMETRY JSON)" if dual_mode else ("RAW CAN FRAMES (MODE:RAW)" if raw_mode else "TELEMETRY JSON (MODE:PLOT)")
    print(f"\n==========================================================================")
    print(f" CONNECTING TO LIVE CENTRAL GATEWAY: {resolved_ip}:{port}")
    print(f" MODE: {mode_str}")
    print(f" LOG FILE: {log_file_path}")
    print(f"==========================================================================")
    print("Press Ctrl+C to stop logging.\n")
    
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(5.0)
        s.connect((resolved_ip, port))
        print(f"✅ Connected to Central Gateway at {resolved_ip}:{port}")
        
        mac = get_mac_for_ip(resolved_ip)
        if mac:
            print(f"🛡️ ESP32 Board MAC Validated: {mac.upper()}")

        if dual_mode:
            s.sendall(b"MODE:DUAL\n")
            print("📡 Sent command 'MODE:DUAL' -> Gateway streaming raw CAN + telemetry JSON simultaneously!\n")
        elif raw_mode:
            s.sendall(b"MODE:RAW\n")
            print("📡 Sent command 'MODE:RAW' -> Gateway streaming raw CAN frames!\n")
        else:
            s.sendall(b"MODE:PLOT\n")
            print("📡 Sent command 'MODE:PLOT' -> Gateway streaming telemetry JSON!\n")
        
        with open(log_file_path, "w") as log_f:
            while True:
                data = s.recv(1024).decode('utf-8', errors='ignore')
                if not data: break
                sys.stdout.write(data)
                sys.stdout.flush()
                log_f.write(data)
                log_f.flush()
    except KeyboardInterrupt:
        print(f"\n\n⏹️ Live logging stopped by user. Log saved to: {log_file_path}")
    except Exception as e:
        print(f"\n❌ Connection error: {e}")

def main():
    parser = argparse.ArgumentParser(description="Honda 2014 Civic CAN Interactive Calibrator")
    parser.add_argument("--ip", nargs="?", const="auto", help="Central Gateway IP or mDNS hostname (default: auto / esp32-gateway.local)")
    parser.add_argument("--serial", nargs="?", const="auto", help="Stream live telemetry via USB Serial port (e.g. /dev/cu.usbmodem101)")
    parser.add_argument("--raw", action="store_true", help="Stream raw CAN frames instead of JSON telemetry")
    parser.add_argument("--dual", action="store_true", help="Stream BOTH raw CAN frames and JSON telemetry concurrently")
    parser.add_argument("--file", help="Path to raw CAN CSV dump")
    args = parser.parse_args()

    if args.file:
        analyze_dump(args.file)
    elif args.serial is not None:
        port_name = None if args.serial == "auto" else args.serial
        live_serial_stream(port_name, raw_mode=args.raw, dual_mode=args.dual)
    elif args.ip is not None:
        live_telnet_stream(args.ip, raw_mode=args.raw, dual_mode=args.dual)
    else:
        # Try mDNS first; if not found, check USB serial; otherwise fall back to offline dump analysis
        target_ip = resolve_gateway_ip("esp32-gateway.local")
        if target_ip:
            live_telnet_stream(target_ip, raw_mode=args.raw, dual_mode=args.dual)
        else:
            import glob
            candidates = glob.glob("/dev/cu.usbmodem*") + glob.glob("/dev/cu.usbserial*")
            if candidates:
                live_serial_stream(candidates[0], raw_mode=args.raw, dual_mode=args.dual)
            else:
                default_csv = "/Users/kickoff_laptop/Developer/espDash/espDash_raw_can_log_2026-08-06T14-32-21-348Z.csv"
                if os.path.exists(default_csv):
                    print("\n📁 No live Gateway found on Wi-Fi or USB. Falling back to raw CAN CSV dump analysis.")
                    analyze_dump(default_csv)
                else:
                    parser.print_help()

if __name__ == "__main__":
    main()
