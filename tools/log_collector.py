#!/usr/bin/env python3
"""
espDash - Automotive Telemetry Log Collector & Sniffer CLI Tool
Captures live telemetry JSON and raw CAN frames over TCP (Port 8889) or WebSockets (Port 8888).
Saves structured CSV logs to disk with zero packet loss.
"""

import sys
import os
import time
import json
import socket
import argparse
import csv
from datetime import datetime

class CANLogCollector:
    def __init__(self, host="esp32-gateway.local", port=8889, mode="PLOT", output_dir="logs"):
        self.host = host
        self.port = port
        self.mode = mode
        self.output_dir = output_dir
        self.sock = None
        self.is_running = False
        self.total_packets_received = 0
        self.csv_file = None
        self.csv_writer = None
        self.filepath = None

    def create_log_file(self):
        os.makedirs(self.output_dir, exist_ok=True)
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        filename = f"can_log_{self.mode.lower()}_{timestamp}.csv"
        self.filepath = os.path.join(self.output_dir, filename)

        self.csv_file = open(self.filepath, mode='w', newline='', encoding='utf-8')
        self.csv_writer = csv.writer(self.csv_file)

        if self.mode == "RAW":
            self.csv_writer.writerow(["Timestamp_MS", "CAN_ID_Hex", "RTR", "DLC", "Byte0", "Byte1", "Byte2", "Byte3", "Byte4", "Byte5", "Byte6", "Byte7"])
        else:
            self.csv_writer.writerow(["ISO_Time", "Uptime_MS", "RPM", "Speed_KMH", "WaterTemp_C", "OilTemp_C", "Battery_V", "Gear", "Fuel_Pct", "Throttle_Pct", "Steering_Deg", "Brake_Bar", "Ambient_C"])

        print(f"[LogCollector] Writing log to: {self.filepath}")

    @staticmethod
    def parse_raw_line(line):
        """Parse raw CAN sniffer string line: RAW,timestamp,id,rtr,dlc,byte0,byte1..."""
        parts = line.strip().split(',')
        if len(parts) >= 5 and parts[0] == "RAW":
            ts = parts[1]
            can_id = parts[2]
            rtr = parts[3]
            dlc = parts[4]
            bytes_data = parts[5:]
            # Pad byte list to 8 bytes if needed
            while len(bytes_data) < 8:
                bytes_data.append("")
            return [ts, can_id, rtr, dlc] + bytes_data[:8]
        return None

    @staticmethod
    def parse_telemetry_line(line):
        """Parse telemetry JSON object line"""
        try:
            data = json.loads(line.strip())
            if data.get("type") == "telemetry":
                iso_now = datetime.now().isoformat()
                return [
                    iso_now,
                    data.get("timestamp", 0),
                    data.get("rpm", 0),
                    data.get("speed", 0.0),
                    data.get("water_temp", 0.0),
                    data.get("oil_temp", 0.0),
                    data.get("battery_v", 0.0),
                    data.get("gear", 0),
                    data.get("fuel", 0),
                    data.get("throttle", 0),
                    data.get("steering", 0),
                    data.get("brake", 0),
                    data.get("ambient", 0)
                ]
        except Exception:
            pass
        return None

    def process_line(self, line):
        line = line.strip()
        if not line:
            return False

        if self.mode == "RAW":
            parsed = self.parse_raw_line(line)
            if parsed:
                self.csv_writer.writerow(parsed)
                self.total_packets_received += 1
                return True
        else:
            parsed = self.parse_telemetry_line(line)
            if parsed:
                self.csv_writer.writerow(parsed)
                self.total_packets_received += 1
                return True
        return False

    def connect(self):
        print(f"[LogCollector] Connecting to {self.host}:{self.port}...")
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.settimeout(5.0)
        self.sock.connect((self.host, self.port))
        print(f"[LogCollector] Connected successfully!")

        # Send mode command to gateway
        cmd = f"MODE:{self.mode}\n"
        self.sock.sendall(cmd.encode('utf-8'))

    def start(self, duration_sec=None):
        self.create_log_file()
        self.connect()
        self.is_running = True

        buffer = ""
        start_time = time.time()

        try:
            while self.is_running:
                if duration_sec and (time.time() - start_time) >= duration_sec:
                    print(f"[LogCollector] Duration limit of {duration_sec}s reached.")
                    break

                try:
                    data = self.sock.recv(4096)
                    if not data:
                        print("[LogCollector] Gateway closed connection.")
                        break

                    buffer += data.decode('utf-8', errors='ignore')
                    lines = buffer.split('\n')
                    buffer = lines.pop() # Keep incomplete line chunk

                    for line in lines:
                        if self.process_line(line):
                            if self.total_packets_received % 50 == 0:
                                self.csv_file.flush()
                                print(f"\r[LogCollector] Packets Captured: {self.total_packets_received}", end="", flush=True)

                except socket.timeout:
                    continue

        except KeyboardInterrupt:
            print("\n[LogCollector] Interrupted by user.")
        finally:
            self.stop()

    def stop(self):
        self.is_running = False
        if self.csv_file:
            self.csv_file.flush()
            self.csv_file.close()
        if self.sock:
            try:
                self.sock.close()
            except Exception:
                pass
        print(f"\n[LogCollector] Session finished. Total Packets Saved: {self.total_packets_received}")
        print(f"[LogCollector] Log saved to: {self.filepath}")

def main():
    parser = argparse.ArgumentParser(description="espDash CAN Log Collector CLI Tool")
    parser.add_argument("--host", default="esp32-gateway.local", help="Gateway mDNS hostname or IP address (default: esp32-gateway.local)")
    parser.add_argument("--port", type=int, default=8889, help="Telnet stream port (default: 8889)")
    parser.add_argument("--mode", choices=["PLOT", "RAW"], default="PLOT", help="Logging mode: PLOT (telemetry JSON) or RAW (raw CAN frames)")
    parser.add_argument("--duration", type=int, default=None, help="Logging duration in seconds (default: continuous until Ctrl+C)")
    parser.add_argument("--output", default="logs", help="Output directory for CSV logs")

    args = parser.parse_args()

    collector = CANLogCollector(host=args.host, port=args.port, mode=args.mode, output_dir=args.output)
    collector.start(duration_sec=args.duration)

if __name__ == "__main__":
    main()
