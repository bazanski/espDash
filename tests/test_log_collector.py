#!/usr/bin/env python3
"""
Unit tests for espDash Log Collector Mechanism (tools/log_collector.py)
"""

import os
import unittest
import tempfile
import json
from tools.log_collector import CANLogCollector

class TestCANLogCollector(unittest.TestCase):

    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory()
        self.output_dir = self.temp_dir.name

    def tearDown(self):
        self.temp_dir.cleanup()

    def test_parse_raw_line(self):
        valid_raw = "RAW,12345,0x17C,0,8,00,00,02,BD,00,00,00,19"
        parsed = CANLogCollector.parse_raw_line(valid_raw)
        
        self.assertIsNotNone(parsed)
        self.assertEqual(parsed[0], "12345")      # timestamp
        self.assertEqual(parsed[1], "0x17C")      # CAN ID
        self.assertEqual(parsed[2], "0")          # RTR
        self.assertEqual(parsed[3], "8")          # DLC
        self.assertEqual(parsed[4], "00")         # Byte0
        self.assertEqual(parsed[7], "BD")         # Byte3

    def test_parse_short_raw_line(self):
        short_raw = "RAW,12346,0x156,0,5,FF,B8,00,02,07"
        parsed = CANLogCollector.parse_raw_line(short_raw)
        
        self.assertIsNotNone(parsed)
        self.assertEqual(len(parsed), 12)         # TS, ID, RTR, DLC + 8 Bytes
        self.assertEqual(parsed[4], "FF")
        self.assertEqual(parsed[8], "07")
        self.assertEqual(parsed[9], "")           # Padded byte

    def test_parse_telemetry_json(self):
        sample_json = json.dumps({
            "type": "telemetry",
            "rpm": 701,
            "speed": 0.0,
            "water_temp": 88.0,
            "oil_temp": 83.0,
            "battery_v": 13.80,
            "gear": 0,
            "fuel": 77,
            "throttle": 40,
            "steering": 0,
            "brake": 15,
            "ambient": 22,
            "timestamp": 123456
        })

        parsed = CANLogCollector.parse_telemetry_line(sample_json)
        self.assertIsNotNone(parsed)
        self.assertEqual(parsed[1], 123456)       # Uptime MS
        self.assertEqual(parsed[2], 701)          # RPM
        self.assertEqual(parsed[3], 0.0)          # Speed
        self.assertEqual(parsed[4], 88.0)         # Water temp
        self.assertEqual(parsed[6], 13.80)        # Battery V

    def test_process_line_and_csv_write(self):
        collector = CANLogCollector(mode="PLOT", output_dir=self.output_dir)
        collector.create_log_file()

        sample_json = json.dumps({
            "type": "telemetry",
            "rpm": 2500,
            "speed": 65.5,
            "water_temp": 90.0,
            "oil_temp": 95.0,
            "battery_v": 14.1,
            "gear": 3,
            "fuel": 60,
            "throttle": 25,
            "steering": -5,
            "brake": 0,
            "ambient": 24,
            "timestamp": 99999
        })

        res = collector.process_line(sample_json)
        self.assertTrue(res)
        self.assertEqual(collector.total_packets_received, 1)

        collector.stop()

        # Verify CSV file contents on disk
        self.assertTrue(os.path.exists(collector.filepath))
        with open(collector.filepath, 'r') as f:
            lines = f.readlines()
            self.assertEqual(len(lines), 2)  # Header + 1 Data Row
            self.assertIn("ISO_Time,Uptime_MS,RPM", lines[0])
            self.assertIn("2500,65.5,90.0", lines[1])

if __name__ == "__main__":
    unittest.main()
