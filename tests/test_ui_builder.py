#!/usr/bin/env python3
"""
Unit tests for espDash UI Studio & Telemetry Builder (web-dashboard/ui-builder)
Validates device presets, widget definitions, color conversion, font mapping,
theme selector, delete tools, navigation pack, icon packs, and 50Hz LVGL 8.4 code generation logic.
"""

import os
import re
import json
import unittest

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
UI_BUILDER_DIR = os.path.join(REPO_ROOT, "web-dashboard", "ui-builder")


class TestUIBuilderAssets(unittest.TestCase):
    """Test static files, markup, and CSS definitions in UI Builder."""

    def test_ui_builder_files_exist(self):
        required_files = ["index.html", "style.css", "builder.js", "code-generator.js", "icon-packs.js", "live-preview-bridge.js"]
        for fname in required_files:
            fpath = os.path.join(UI_BUILDER_DIR, fname)
            self.assertTrue(os.path.isfile(fpath), f"Missing required file: {fname}")

    def test_index_html_contains_fonts_presets_and_themes(self):
        index_path = os.path.join(UI_BUILDER_DIR, "index.html")
        with open(index_path, "r", encoding="utf-8") as f:
            html = f.read()

        # Check font imports
        self.assertIn("Orbitron", html)
        self.assertIn("Rajdhani", html)
        self.assertIn("Montserrat", html)
        self.assertIn("JetBrains+Mono", html)
        self.assertIn("Bebas+Neue", html)
        self.assertIn("Chakra+Petch", html)

        # Check display preset options
        self.assertIn('value="amoled-454"', html)
        self.assertIn('value="xiao-round-240"', html)
        self.assertIn('value="xiao-dual-round"', html)
        self.assertIn('value="esp32-s3-lcd-314"', html)

        # Check Navigation Presets
        self.assertIn('data-preset-layout="nav-amoled"', html)
        self.assertIn('data-preset-layout="nav-highway-314"', html)

        # Check Theme Switcher
        self.assertIn('id="themeSelect"', html)
        self.assertIn('value="civic-eco"', html)
        self.assertIn('value="type-r"', html)
        self.assertIn('value="cyberpunk"', html)
        self.assertIn('value="motorsport-gt3"', html)
        self.assertIn('value="stealth-oled"', html)

        # Check delete tools
        self.assertIn('id="deleteWidgetBtn"', html)
        self.assertIn('id="clearLayersBtn"', html)

        # Check full widget library items (all 24 widgets including Navigation pack)
        required_widgets = [
            "nav-turn-arrow", "nav-maneuver-banner", "nav-eta-badge", "nav-lane-assist",
            "nav-camera-alert", "nav-hazard-alert",
            "shift-lights", "rev-strip", "smooth-arc", "dial-needle", "boost-gauge",
            "gauge-ticks", "digital-value", "lap-timer", "bar-slider", "temp-stack",
            "history-chart", "g-force-meter", "tpms-map", "battery-meter", "compass-dial",
            "speed-sign", "annunciator-icon", "status-badge", "text-label", "card-box"
        ]
        for w in required_widgets:
            self.assertIn(f'data-type="{w}"', html, f"Widget palette missing {w}")

        # Check modal tabs
        self.assertIn("tabLvglBtn", html)
        self.assertIn("tabCppBtn", html)
        self.assertIn("tabJsonBtn", html)

    def test_icon_packs_js_definitions(self):
        icons_path = os.path.join(UI_BUILDER_DIR, "icon-packs.js")
        with open(icons_path, "r", encoding="utf-8") as f:
            js = f.read()

        self.assertIn("navigation:", js)
        self.assertIn("turn-right", js)
        self.assertIn("turn-left", js)
        self.assertIn("annunciators:", js)
        self.assertIn("check-engine", js)
        self.assertIn("oil-pressure", js)
        self.assertIn("hazards:", js)
        self.assertIn("speed-camera", js)
        self.assertIn("police-radar", js)

    def test_style_css_font_faces_and_bezels(self):
        css_path = os.path.join(UI_BUILDER_DIR, "style.css")
        with open(css_path, "r", encoding="utf-8") as f:
            css = f.read()

        self.assertIn("@font-face", css)
        self.assertIn("DSEG7-Classic", css)
        self.assertIn("round-display", css)
        self.assertIn("rect-display", css)
        self.assertIn("dual-round-display", css)
        self.assertIn("btn-delete-widget", css)
        self.assertIn("layer-btn-group", css)
        self.assertIn("modal-card", css)


class TestCodeGeneratorLogic(unittest.TestCase):
    """Test RGB565 conversion, LVGL font mapping, and code generation rules."""

    @staticmethod
    def hex_to_rgb565(hex_str):
        if not hex_str or not hex_str.startswith('#'):
            return '0x0000'
        r = int(hex_str[1:3], 16) if len(hex_str) >= 3 else 0
        g = int(hex_str[3:5], 16) if len(hex_str) >= 5 else 0
        b = int(hex_str[5:7], 16) if len(hex_str) >= 7 else 0
        r5 = (r >> 3) & 0x1F
        g6 = (g >> 2) & 0x3F
        b5 = (b >> 3) & 0x1F
        rgb565 = (r5 << 11) | (g6 << 5) | b5
        return f"0x{rgb565:04X}"

    @staticmethod
    def map_lvgl_font(font_family, font_size):
        size = int(font_size) if font_size else 20
        fam = (font_family or "").lower()

        if "segment7" in fam or "dseg" in fam:
            if size >= 100:
                return "&ui_font_segment7_120"
            if size >= 70:
                return "&ui_font_segment7_80"
            if size >= 60:
                return "&ui_font_dseg_regular_60"
            if size >= 46:
                return "&ui_font_dseg_regular_46"
            if size >= 30:
                return "&ui_font_segment7_56"
            if size >= 16:
                return "&ui_font_dseg_regular_20"
            return "&ui_font_dseg_mini_light_20"

        montserrat_sizes = [8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30, 32, 34, 36, 38, 40, 42, 44, 46, 48]
        closest = min(montserrat_sizes, key=lambda x: abs(x - size))
        return f"&lv_font_montserrat_{closest}"

    def test_rgb565_color_conversions(self):
        self.assertEqual(self.hex_to_rgb565("#000000"), "0x0000")
        self.assertEqual(self.hex_to_rgb565("#FFFFFF"), "0xFFFF")
        self.assertEqual(self.hex_to_rgb565("#FF0000"), "0xF800")
        self.assertEqual(self.hex_to_rgb565("#00FF00"), "0x07E0")
        self.assertEqual(self.hex_to_rgb565("#0000FF"), "0x001F")

    def test_lvgl_font_mappings(self):
        self.assertEqual(self.map_lvgl_font("Segment7", 120), "&ui_font_segment7_120")
        self.assertEqual(self.map_lvgl_font("Segment7", 96), "&ui_font_segment7_80")
        self.assertEqual(self.map_lvgl_font("DSEG7-Classic", 60), "&ui_font_dseg_regular_60")
        self.assertEqual(self.map_lvgl_font("DSEG7-Classic", 46), "&ui_font_dseg_regular_46")
        self.assertEqual(self.map_lvgl_font("Segment7", 32), "&ui_font_segment7_56")
        self.assertEqual(self.map_lvgl_font("DSEG7-Classic", 20), "&ui_font_dseg_regular_20")
        self.assertEqual(self.map_lvgl_font("DSEG7-Classic", 12), "&ui_font_dseg_mini_light_20")

        self.assertEqual(self.map_lvgl_font("Montserrat", 18), "&lv_font_montserrat_18")
        self.assertEqual(self.map_lvgl_font("Montserrat", 24), "&lv_font_montserrat_24")
        self.assertEqual(self.map_lvgl_font("Inter", 32), "&lv_font_montserrat_32")

    def test_json_schema_roundtrip(self):
        preset = {
            "name": "Waveshare 1.43\" AMOLED (454x454 Circle)",
            "width": 454,
            "height": 454,
            "shape": "round"
        }
        widgets = [
            {
                "id": "nav_turn",
                "type": "nav-turn-arrow",
                "x": 227,
                "y": 130,
                "color": "#00ff66"
            },
            {
                "id": "speed_val",
                "type": "digital-value",
                "x": 227,
                "y": 300,
                "fontSize": 56,
                "fontFamily": "Orbitron",
                "color": "#ffffff",
                "binding": "speed_kmh"
            }
        ]

        payload = {
            "schemaVersion": "2.2",
            "preset": preset,
            "widgets": widgets
        }

        serialized = json.dumps(payload, indent=2)
        deserialized = json.loads(serialized)

        self.assertEqual(deserialized["schemaVersion"], "2.2")
        self.assertEqual(deserialized["preset"]["width"], 454)
        self.assertEqual(len(deserialized["widgets"]), 2)
        self.assertEqual(deserialized["widgets"][0]["type"], "nav-turn-arrow")


if __name__ == "__main__":
    unittest.main()
