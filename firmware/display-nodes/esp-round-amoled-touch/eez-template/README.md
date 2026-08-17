# 🎨 espDash Production UI - EEZ Studio Starter Template

This directory contains the starter project template matching the current production **XIAO 240x240 Round Universal Telemetry Gauge** UI layout.

---

## 📁 Files Included

* **[`xiao_round_gauge.eez-project`](file:///Users/kickoff_laptop/Developer/espDash/firmware/display-nodes/xiao-round-gauge/eez-template/xiao_round_gauge.eez-project)**: EEZ Studio project file pre-populated with all production screen elements, color variables, dimensions, and named widgets.
* **[`themes/18_black_white_shapes`](file:///Users/kickoff_laptop/Developer/espDash/firmware/display-nodes/xiao-round-gauge/eez-template/themes/18_black_white_shapes)**: EEZ Studio Theme 18 ("The Black White Shapes") asset bundle, including vector SVGs, preview renders, TrueType fonts (`arial.ttf`, `arialbd.ttf`), and `theme.json` manifest.

---

## 🎨 Registered Themes

| Theme Name | Description | Background | Accents | Fonts |
| :--- | :--- | :--- | :--- | :--- |
| **Default (Cyber Cyan)** | Dark cybernetic theme with neon cyan & green accents | `#0b0f19` | Cyan `#00f0ff` / Green `#00ff00` | Inter / Orbitron |
| **Theme 18 (The Black White Shapes)** | High-contrast monochrome shape theme from EEZ Studio Dashboard Styles #18 | `#000000` / `#121212` | Silver `#DBCFCF` / Mid-Grey `#9A9797` / White `#FFFFFF` | Arial / Arial Bold |

---

## 🎯 Production UI Layout & Widget Reference

| EEZ Studio Widget Name | Type | Coordinates (X, Y) | Dimensions (W x H) | Function / Telemetry Variable | Production Style & Colors |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `ui_status_header` | Label | (60, 14) | 120 x 16 | Header Status (`"ESP-NOW 20Hz"`) | Cyan `#00f0ff` |
| `ui_batt_voltage` | Label | (80, 28) | 80 x 14 | Battery Voltage (`"13.8V"`) | Muted Grey `#8c8c8c` |
| `ui_arc_rpm` | Arc | (20, 20) | 200 x 200 | RPM Arc Sweep (0-8000 RPM) | Cyan `#00f0ff` / Yellow / Red |
| `ui_arc_throttle` | Arc | (38, 38) | 164 x 164 | Throttle Slider Arc (0-100%) | Bright Green `#00ff00` |
| `ui_arc_brake` | Arc | (38, 38) | 164 x 164 | Brake Slider Arc (0-100%) | Bright Blue `#0088ff` / Red alert |
| `ui_label_throttle_val`| Label | (48, 112) | 40 x 16 | Throttle % (`"65%"`) | Green `#00ff00` |
| `ui_label_brake_val` | Label | (48, 112) | 40 x 16 | Brake % (`"0%"`) | Blue `#0088ff` / Red |
| `ui_label_steering` | Label | (90, 68) | 60 x 14 | Steering Angle (`"0°"`, `"45°R"`) | Muted Grey `#8c8c8c` |
| `ui_label_speed_val` | Label | (50, 95) | 140 x 45 | Large Digital Speed (`"115"`) | White `#ffffff` (Font 48px / 7-seg) |
| `ui_label_speed_unit` | Label | (90, 145) | 60 x 14 | Speed Unit (`"KM/H"`) | Muted Grey `#8c8c8c` |
| `ui_label_temp` | Label | (45, 174) | 50 x 16 | Coolant Temp (`"92°C"`) | Yellow `#ffff00` (Red if >105°C) |
| `ui_label_fuel` | Label | (95, 174) | 50 x 16 | Fuel Level (`"F:75%"`) | Green `#00ff00` (Red if <=15%) |
| `ui_gear_container` | Badge | (156, 168) | 24 x 24 | Gear Ring Badge | Cyan `#00f0ff` Circle |
| `ui_label_gear` | Label | (156, 172) | 24 x 16 | Gear Text (`"P"`, `"N"`, `"1"-"6"`) | White `#ffffff` |

---

## 🚀 How to Use & Export Changes

1. **Open EEZ Studio**:
   - File $\rightarrow$ **Open Project** $\rightarrow$ Select [`xiao_round_gauge.eez-project`](file:///Users/kickoff_laptop/Developer/espDash/firmware/display-nodes/xiao-round-gauge/eez-template/xiao_round_gauge.eez-project).
2. **Make your UI modifications**:
   - Rearrange widgets, adjust fonts, change color palettes, or add new telemetry indicators.
3. **Export Code**:
   - In Project Settings, set Output Directory to: `../src/ui`
   - Press **`Cmd+Shift+G`** (or `Ctrl+Shift+G`) to generate C/C++ LVGL code into `firmware/display-nodes/xiao-round-gauge/src/ui`.
4. **Notify Agent**:
   - Ask the AI agent: *"I updated the EEZ Studio template and exported it to `firmware/display-nodes/xiao-round-gauge/src/ui`. Please bind telemetry and flash firmware."*
