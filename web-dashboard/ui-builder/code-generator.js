/**
 * espDash UI Studio - Multi-Target Code Generator
 * Converts 1-to-1 canvas widgets into:
 * 1. High-Performance Native LVGL 8.4 C Code with 50Hz Batched Updates & Nav Support (screens.c / ui.c)
 * 2. Double-buffered TFT_eSPI C++ Sprite Rendering Code (XIAO / SPI nodes)
 * 3. JSON Project Schema (Import/Export)
 */

window.CodeGenerator = {
    /**
     * Convert #RRGGBB hex color to 16-bit RGB565 hex format (e.g., 0x07FF)
     */
    hexToRgb565: function(hex) {
        if (!hex || hex[0] !== '#') return '0x0000';
        const r = parseInt(hex.substr(1, 2), 16) || 0;
        const g = parseInt(hex.substr(3, 2), 16) || 0;
        const b = parseInt(hex.substr(5, 2), 16) || 0;
        const r5 = (r >> 3) & 0x1F;
        const g6 = (g >> 2) & 0x3F;
        const b5 = (b >> 3) & 0x1F;
        const rgb565 = (r5 << 11) | (g6 << 5) | b5;
        return '0x' + rgb565.toString(16).toUpperCase().padStart(4, '0');
    },

    /**
     * Map web font names to LVGL 8.4 font descriptors
     */
    mapLvglFont: function(fontFamily, fontSize) {
        const size = parseInt(fontSize) || 20;
        const fam = (fontFamily || '').toLowerCase();

        if (fam.includes('segment7') || fam.includes('dseg')) {
            if (size >= 100) return '&ui_font_segment7_120';
            if (size >= 70) return '&ui_font_segment7_80';
            if (size >= 60) return '&ui_font_dseg_regular_60';
            if (size >= 46) return '&ui_font_dseg_regular_46';
            if (size >= 30) return '&ui_font_segment7_56';
            if (size >= 16) return '&ui_font_dseg_regular_20';
            return '&ui_font_dseg_mini_light_20';
        }

        const montserratSizes = [8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30, 32, 34, 36, 38, 40, 42, 44, 46, 48];
        let closest = montserratSizes.reduce((prev, curr) => Math.abs(curr - size) < Math.abs(prev - size) ? curr : prev);
        return `&lv_font_montserrat_${closest}`;
    },

    /**
     * Target 1: Generate Native LVGL 8.4 C Code with 50Hz Batched Update Loop
     */
    generateLvglC: function(devicePreset, widgets) {
        let code = `// =========================================================================\n`;
        code += `// Auto-generated LVGL 8.4 Screen Definition for espDash\n`;
        code += `// Target Display: ${devicePreset.name} (${devicePreset.width}x${devicePreset.height})\n`;
        code += `// High-Performance Zero-Allocation Architecture (50Hz Refresh Ready)\n`;
        code += `// =========================================================================\n\n`;
        code += `#include <Arduino.h>\n`;
        code += `#include <lvgl.h>\n`;
        code += `#include "screens.h"\n`;
        code += `#include "fonts.h"\n`;
        code += `#include "styles.h"\n`;
        code += `#include "ui.h"\n`;
        code += `#include <EspDashProto.h> // Shared telemetry & nav wire protocol\n\n`;

        // 1. Screen Init Function
        code += `void create_screen_main() {\n`;
        code += `    lv_obj_t *obj = lv_obj_create(0);\n`;
        code += `    objects.main = obj;\n`;
        code += `    lv_obj_set_pos(obj, 0, 0);\n`;
        code += `    lv_obj_set_size(obj, ${devicePreset.width}, ${devicePreset.height});\n`;
        code += `    lv_obj_set_style_bg_color(obj, lv_color_hex(0x050811), LV_PART_MAIN);\n`;
        code += `    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);\n\n`;
        code += `    lv_obj_t *parent_obj = obj;\n\n`;

        widgets.forEach((w, idx) => {
            const varName = (w.id || `widget_${idx + 1}`).replace(/[^a-zA-Z0-9_]/g, '_');
            const colorHex = (w.color || '#00f0ff').replace('#', '0x');
            const bgHex = (w.bgColor || w.trackColor || '#141c30').replace('#', '0x');

            code += `    // --- [Widget ${idx + 1}] ${w.type} (${varName}) ---\n`;
            code += `    {\n`;

            switch (w.type) {
                case 'smooth-arc':
                case 'shift-lights':
                case 'boost-gauge': {
                    code += `        lv_obj_t *obj = lv_arc_create(parent_obj);\n`;
                    code += `        objects.${varName} = obj;\n`;
                    const diameter = (w.radius || 100) * 2;
                    code += `        lv_obj_set_pos(obj, ${w.x - (w.radius || 100)}, ${w.y - (w.radius || 100)});\n`;
                    code += `        lv_obj_set_size(obj, ${diameter}, ${diameter});\n`;
                    code += `        lv_arc_set_range(obj, ${w.min || 0}, ${w.max || 100});\n`;
                    code += `        lv_arc_set_value(obj, ${w.value || 50});\n`;
                    code += `        lv_arc_set_bg_angles(obj, ${w.startAngle || 135}, ${w.endAngle || 405});\n`;
                    code += `        lv_obj_set_style_arc_color(obj, lv_color_hex(${colorHex}), LV_PART_INDICATOR);\n`;
                    code += `        lv_obj_set_style_arc_width(obj, ${w.thickness || 10}, LV_PART_INDICATOR);\n`;
                    code += `        lv_obj_set_style_arc_color(obj, lv_color_hex(${bgHex}), LV_PART_MAIN);\n`;
                    code += `        lv_obj_set_style_arc_width(obj, ${w.thickness || 10}, LV_PART_MAIN);\n`;
                    code += `        lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);\n`;
                    break;
                }

                case 'digital-value':
                case 'text-label':
                case 'lap-timer':
                case 'nav-maneuver-banner':
                case 'nav-eta-badge': {
                    const fontDescriptor = this.mapLvglFont(w.fontFamily, w.fontSize);
                    code += `        lv_obj_t *obj = lv_label_create(parent_obj);\n`;
                    code += `        objects.${varName} = obj;\n`;
                    code += `        lv_obj_set_pos(obj, ${w.x}, ${w.y});\n`;
                    code += `        lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);\n`;
                    code += `        lv_obj_set_style_text_color(obj, lv_color_hex(${colorHex}), LV_PART_MAIN);\n`;
                    code += `        lv_obj_set_style_text_font(obj, ${fontDescriptor}, LV_PART_MAIN);\n`;
                    code += `        lv_label_set_text_static(obj, "${w.text || w.value || '0'}${w.unit || ''}");\n`;
                    break;
                }

                case 'nav-turn-arrow':
                case 'nav-camera-alert':
                case 'nav-hazard-alert':
                case 'nav-lane-assist': {
                    code += `        lv_obj_t *obj = lv_obj_create(parent_obj);\n`;
                    code += `        objects.${varName} = obj;\n`;
                    code += `        lv_obj_set_pos(obj, ${w.x - 30}, ${w.y - 30});\n`;
                    code += `        lv_obj_set_size(obj, ${w.w || 60}, ${w.h || 60});\n`;
                    code += `        lv_obj_set_style_bg_color(obj, lv_color_hex(${bgHex}), LV_PART_MAIN);\n`;
                    code += `        lv_obj_set_style_border_color(obj, lv_color_hex(${colorHex}), LV_PART_MAIN);\n`;
                    code += `        lv_obj_set_style_radius(obj, ${w.borderRadius || 8}, LV_PART_MAIN);\n`;
                    break;
                }

                case 'bar-slider':
                case 'rev-strip':
                case 'temp-stack':
                case 'battery-meter': {
                    code += `        lv_obj_t *obj = lv_bar_create(parent_obj);\n`;
                    code += `        objects.${varName} = obj;\n`;
                    code += `        lv_obj_set_pos(obj, ${w.x}, ${w.y});\n`;
                    code += `        lv_obj_set_size(obj, ${w.w || 140}, ${w.h || 16});\n`;
                    code += `        lv_bar_set_range(obj, ${w.min || 0}, ${w.max || 100});\n`;
                    code += `        lv_bar_set_value(obj, ${w.value || 50}, LV_ANIM_OFF);\n`;
                    code += `        lv_obj_set_style_bg_color(obj, lv_color_hex(${bgHex}), LV_PART_MAIN);\n`;
                    code += `        lv_obj_set_style_bg_color(obj, lv_color_hex(${colorHex}), LV_PART_INDICATOR);\n`;
                    code += `        lv_obj_set_style_radius(obj, ${w.borderRadius || 4}, LV_PART_MAIN);\n`;
                    code += `        lv_obj_set_style_radius(obj, ${w.borderRadius || 4}, LV_PART_INDICATOR);\n`;
                    break;
                }

                case 'dial-needle':
                case 'gauge-ticks':
                case 'compass-dial': {
                    code += `        lv_obj_t *obj = lv_meter_create(parent_obj);\n`;
                    code += `        objects.${varName} = obj;\n`;
                    const mDim = (w.radius || 100) * 2;
                    code += `        lv_obj_set_pos(obj, ${w.x - (w.radius || 100)}, ${w.y - (w.radius || 100)});\n`;
                    code += `        lv_obj_set_size(obj, ${mDim}, ${mDim});\n`;
                    code += `        lv_meter_scale_t *scale = lv_meter_add_scale(obj);\n`;
                    code += `        lv_meter_set_scale_ticks(obj, scale, ${w.tickCount || 11}, 2, 8, lv_color_hex(0x64748b));\n`;
                    code += `        lv_meter_set_scale_range(obj, scale, ${w.min || 0}, ${w.max || 100}, ${(w.endAngle || 405) - (w.startAngle || 135)}, ${w.startAngle || 135});\n`;
                    code += `        lv_meter_indicator_t *indic = lv_meter_add_needle_line(obj, scale, 3, lv_color_hex(${colorHex}), -10);\n`;
                    code += `        lv_meter_set_indicator_value(obj, indic, ${w.value || 50});\n`;
                    break;
                }

                default: {
                    code += `        lv_obj_t *obj = lv_obj_create(parent_obj);\n`;
                    code += `        objects.${varName} = obj;\n`;
                    code += `        lv_obj_set_pos(obj, ${w.x}, ${w.y});\n`;
                    code += `        lv_obj_set_size(obj, ${w.w || 80}, ${w.h || 50});\n`;
                    code += `        lv_obj_set_style_bg_color(obj, lv_color_hex(${bgHex}), LV_PART_MAIN);\n`;
                    code += `        lv_obj_set_style_border_color(obj, lv_color_hex(${colorHex}), LV_PART_MAIN);\n`;
                    code += `        lv_obj_set_style_radius(obj, ${w.borderRadius || 8}, LV_PART_MAIN);\n`;
                    break;
                }
            }

            code += `    }\n\n`;
        });

        code += `}\n\n`;

        // 2. High-Performance 50Hz Update Loop
        code += `// =========================================================================\n`;
        code += `// 50Hz Zero-Allocation Telemetry & Nav Tick Handler\n`;
        code += `// Call this directly from your ESP-NOW packet handler or 20-50Hz timer\n`;
        code += `// =========================================================================\n`;
        code += `void update_screen_ui(const EspDashTelemetry &pkt) {\n`;

        widgets.forEach((w) => {
            const varName = (w.id || '').replace(/[^a-zA-Z0-9_]/g, '_');
            if (!w.binding || !varName) return;

            if (w.type === 'smooth-arc' || w.type === 'shift-lights' || w.type === 'boost-gauge') {
                code += `    if (objects.${varName}) lv_arc_set_value(objects.${varName}, pkt.${w.binding});\n`;
            } else if (w.type === 'bar-slider' || w.type === 'rev-strip' || w.type === 'temp-stack' || w.type === 'battery-meter') {
                code += `    if (objects.${varName}) lv_bar_set_value(objects.${varName}, pkt.${w.binding}, LV_ANIM_OFF);\n`;
            } else if (w.type === 'digital-value') {
                if (w.binding === 'speed_kmh') {
                    code += `    if (objects.${varName}) lv_label_set_text_fmt(objects.${varName}, "%d", pkt.speed_kmh_x10 / 10);\n`;
                } else if (w.binding === 'battery_v') {
                    code += `    if (objects.${varName}) lv_label_set_text_fmt(objects.${varName}, "%d.%dV", pkt.battery_mv / 1000, (pkt.battery_mv % 1000) / 100);\n`;
                } else if (w.binding === 'water_temp') {
                    code += `    if (objects.${varName}) lv_label_set_text_fmt(objects.${varName}, "%d°C", pkt.water_temp_x10 / 10);\n`;
                } else {
                    code += `    if (objects.${varName}) lv_label_set_text_fmt(objects.${varName}, "%d", pkt.${w.binding});\n`;
                }
            } else if (w.type === 'status-badge' && w.binding === 'gear') {
                code += `    if (objects.${varName}) {\n`;
                code += `        const char* gears[] = {"P", "R", "N", "D", "S", "1", "2", "3", "4", "5", "6"};\n`;
                code += `        lv_label_set_text(objects.${varName}, (pkt.gear <= 10) ? gears[pkt.gear] : "D");\n`;
                code += `    }\n`;
            }
        });

        code += `}\n`;
        return code;
    },

    /**
     * Target 2: Generate TFT_eSPI C++ Double-Buffered Sprite Rendering Code
     */
    generateCpp: function(devicePreset, widgets) {
        let code = `// =========================================================================\n`;
        code += `// Auto-generated TFT_eSprite UI Layout for espDash (${devicePreset.name})\n`;
        code += `// Target Display: ${devicePreset.width}x${devicePreset.height} (${devicePreset.shape})\n`;
        code += `// Double-Buffered Sprite Engine for Smooth 50 FPS Rendering\n`;
        code += `// =========================================================================\n\n`;
        code += `#include <Arduino.h>\n`;
        code += `#include <TFT_eSPI.h>\n`;
        code += `#include <EspDashProto.h> // Shared telemetry & nav wire protocol\n\n`;
        code += `static TFT_eSPI tft = TFT_eSPI();\n`;
        code += `static TFT_eSprite spr = TFT_eSprite(&tft);\n\n`;

        code += `// Color Definitions (16-bit RGB565)\n`;
        code += `#define COLOR_BG ${this.hexToRgb565('#050811')}\n`;
        
        const usedColors = new Map();
        widgets.forEach((w, idx) => {
            if (w.color) usedColors.set(`COLOR_WIDGET_${idx + 1}`, this.hexToRgb565(w.color));
            if (w.bgColor) usedColors.set(`COLOR_BG_${idx + 1}`, this.hexToRgb565(w.bgColor));
            if (w.trackColor) usedColors.set(`COLOR_TRACK_${idx + 1}`, this.hexToRgb565(w.trackColor));
        });

        usedColors.forEach((hex, name) => {
            code += `#define ${name} ${hex}\n`;
        });
        code += `\n`;

        code += `void initDisplayUI() {\n`;
        code += `    tft.init();\n`;
        if (devicePreset.rotation !== undefined) {
            code += `    tft.setRotation(${devicePreset.rotation});\n`;
        }
        code += `    tft.fillScreen(COLOR_BG);\n`;
        code += `    spr.setColorDepth(16);\n`;
        code += `    spr.createSprite(${devicePreset.width}, ${devicePreset.height});\n`;
        code += `}\n\n`;

        code += `void renderCustomUI(const EspDashTelemetry& telemetry) {\n`;
        code += `    spr.fillSprite(COLOR_BG);\n\n`;

        widgets.forEach((w, idx) => {
            code += `    // --- [Widget ${idx + 1}] ${w.type} (${w.id}) ---\n`;
            switch (w.type) {
                case 'shift-lights': {
                    const rpmValExpr = w.binding ? `telemetry.${w.binding}` : `5400`;
                    code += `    {\n`;
                    code += `        float shiftStart = ${(w.startAngle || 210)} * DEG_TO_RAD;\n`;
                    code += `        float shiftEnd = ${(w.endAngle || 330)} * DEG_TO_RAD;\n`;
                    code += `        const int ledCount = ${w.ledCount || 12};\n`;
                    code += `        float rpmPct = min(1.0f, (float)${rpmValExpr} / ${(w.max || 7000)}.0f);\n`;
                    code += `        for (int i = 0; i < ledCount; i++) {\n`;
                    code += `            float angle = shiftStart + ((float)i / (ledCount - 1)) * (shiftEnd - shiftStart);\n`;
                    code += `            int lx = ${w.x} + cos(angle) * ${(w.radius || 110)};\n`;
                    code += `            int ly = ${w.y} + sin(angle) * ${(w.radius || 110)};\n`;
                    code += `            float threshold = (float)(i + 1) / ledCount;\n`;
                    code += `            if (rpmPct >= threshold) {\n`;
                    code += `                uint16_t c = COLOR_WIDGET_${idx + 1};\n`;
                    code += `                if (i >= ledCount * 0.75f) c = TFT_RED;\n`;
                    code += `                else if (i >= ledCount * 0.45f) c = TFT_YELLOW;\n`;
                    code += `                spr.fillCircle(lx, ly, ${w.ledRadius || 4}, c);\n`;
                    code += `            } else {\n`;
                    code += `                spr.fillCircle(lx, ly, ${w.ledRadius || 4}, COLOR_TRACK_${idx + 1});\n`;
                    code += `            }\n`;
                    code += `        }\n`;
                    code += `    }\n`;
                    break;
                }

                case 'nav-turn-arrow': {
                    code += `    {\n`;
                    code += `        spr.fillRoundRect(${w.x - 30}, ${w.y - 30}, 60, 60, 8, COLOR_BG_${idx + 1});\n`;
                    code += `        spr.drawRoundRect(${w.x - 30}, ${w.y - 30}, 60, 60, 8, COLOR_WIDGET_${idx + 1});\n`;
                    code += `        spr.setTextColor(COLOR_WIDGET_${idx + 1}, COLOR_BG_${idx + 1});\n`;
                    code += `        spr.setTextDatum(MC_DATUM);\n`;
                    code += `        spr.drawString("↗", ${w.x}, ${w.y}, 4);\n`;
                    code += `    }\n`;
                    break;
                }

                case 'digital-value':
                case 'text-label':
                case 'nav-maneuver-banner':
                case 'nav-eta-badge': {
                    const textValExpr = w.binding ? `String(telemetry.${w.binding})` : `"${w.text || w.value || '0'}"`;
                    code += `    spr.setTextColor(COLOR_WIDGET_${idx + 1}, COLOR_BG);\n`;
                    code += `    spr.setTextDatum(MC_DATUM);\n`;
                    code += `    spr.drawString((${textValExpr} + "${w.unit || ''}").c_str(), ${w.x}, ${w.y}, 4);\n`;
                    break;
                }

                case 'smooth-arc':
                case 'boost-gauge': {
                    const arcValExpr = w.binding ? `telemetry.${w.binding}` : w.value || 50;
                    code += `    {\n`;
                    code += `        int val = ${arcValExpr};\n`;
                    code += `        int mappedAngle = map(val, ${w.min || 0}, ${w.max || 100}, ${w.startAngle || 135}, ${w.endAngle || 405});\n`;
                    code += `        spr.drawSmoothArc(${w.x}, ${w.y}, ${w.radius}, ${w.radius - (w.thickness || 12)}, ${w.startAngle || 135}, ${w.endAngle || 405}, COLOR_TRACK_${idx + 1}, COLOR_BG, true);\n`;
                    code += `        spr.drawSmoothArc(${w.x}, ${w.y}, ${w.radius}, ${w.radius - (w.thickness || 12)}, ${w.startAngle || 135}, mappedAngle, COLOR_WIDGET_${idx + 1}, COLOR_BG, true);\n`;
                    code += `    }\n`;
                    break;
                }

                case 'card-box': {
                    code += `    spr.fillRoundRect(${w.x}, ${w.y}, ${w.w || 100}, ${w.h || 60}, ${w.borderRadius || 8}, COLOR_BG_${idx + 1});\n`;
                    code += `    spr.drawRoundRect(${w.x}, ${w.y}, ${w.w || 100}, ${w.h || 60}, ${w.borderRadius || 8}, COLOR_WIDGET_${idx + 1});\n`;
                    break;
                }

                default: {
                    code += `    spr.drawCircle(${w.x}, ${w.y}, 14, COLOR_WIDGET_${idx + 1});\n`;
                    break;
                }
            }
            code += `\n`;
        });

        code += `    spr.pushSprite(0, 0);\n`;
        code += `}\n`;

        return code;
    },

    /**
     * Target 3: Export Full JSON Schema
     */
    generateJson: function(devicePreset, widgets) {
        return JSON.stringify({
            schemaVersion: "2.2",
            timestamp: new Date().toISOString(),
            preset: devicePreset,
            widgets: widgets
        }, null, 2);
    }
};
