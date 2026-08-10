/**
 * espDash UI Builder - Code Generator
 * Converts 1-to-1 canvas widgets into optimized C++ double-buffered TFT_eSprite rendering code.
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
     * Generate complete C++ firmware snippet for display nodes
     */
    generateCpp: function(devicePreset, widgets) {
        let code = `// =========================================================================\n`;
        code += `// Auto-generated TFT_eSprite UI Layout for espDash (${devicePreset.name})\n`;
        code += `// Resolution: ${devicePreset.width}x${devicePreset.height} (${devicePreset.shape})\n`;
        code += `// =========================================================================\n\n`;
        code += `#include <TFT_eSPI.h>\n`;
        code += `#include <EspDashProto.h> // Shared telemetry wire protocol\n\n`;
        code += `static TFT_eSPI tft = TFT_eSPI();\n`;
        code += `static TFT_eSprite spr = TFT_eSprite(&tft);\n\n`;

        // Color definitions
        code += `// Color Definitions (16-bit RGB565)\n`;
        code += `#define COLOR_BG ${this.hexToRgb565('#0b0f19')}\n`;
        
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

        // Setup Function
        code += `void initDisplayUI() {\n`;
        code += `    tft.init();\n`;
        if (devicePreset.rotation !== undefined) {
            code += `    tft.setRotation(${devicePreset.rotation});\n`;
        }
        code += `    tft.fillScreen(COLOR_BG);\n`;
        code += `    spr.setColorDepth(16);\n`;
        code += `    spr.createSprite(${devicePreset.width}, ${devicePreset.height});\n`;
        code += `}\n\n`;

        // Render Loop Function
        code += `void drawUI(const EspDashTelemetry& telemetry) {\n`;
        code += `    spr.fillSprite(COLOR_BG);\n\n`;

        widgets.forEach((w, idx) => {
            code += `    // --- Widget ${idx + 1}: ${w.type} (${w.id}) ---\n`;
            switch (w.type) {
                case 'shift-lights':
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
                    code += `                uint16_t c = COLOR_GREEN;\n`;
                    code += `                if (i >= 8) c = COLOR_RED;\n`;
                    code += `                else if (i >= 4) c = COLOR_YELLOW;\n`;
                    code += `                spr.fillCircle(lx, ly, 4, c);\n`;
                    code += `            } else {\n`;
                    code += `                spr.fillCircle(lx, ly, 4, COLOR_DARK_GRAY);\n`;
                    code += `            }\n`;
                    code += `        }\n`;
                    code += `    }\n`;
                    break;

                case 'smooth-arc':
                    const arcValExpr = w.binding ? `telemetry.${w.binding}` : w.value || 50;
                    code += `    {\n`;
                    code += `        int val = ${arcValExpr};\n`;
                    code += `        int mappedAngle = map(val, ${w.min || 0}, ${w.max || 100}, ${w.startAngle || 135}, ${w.endAngle || 405});\n`;
                    code += `        // Track background arc\n`;
                    code += `        spr.drawSmoothArc(${w.x}, ${w.y}, ${w.radius}, ${w.radius - (w.thickness || 12)}, ${w.startAngle || 135}, ${w.endAngle || 405}, COLOR_TRACK_${idx + 1}, COLOR_BG, true);\n`;
                    code += `        // Active arc value\n`;
                    code += `        spr.drawSmoothArc(${w.x}, ${w.y}, ${w.radius}, ${w.radius - (w.thickness || 12)}, ${w.startAngle || 135}, mappedAngle, COLOR_WIDGET_${idx + 1}, COLOR_BG, true);\n`;
                    code += `    }\n`;
                    break;

                case 'digital-value':
                    const textValExpr = w.binding ? `String(telemetry.${w.binding})` : `"${w.value || '0'}"`;
                    code += `    spr.setTextColor(COLOR_WIDGET_${idx + 1}, COLOR_BG);\n`;
                    code += `    spr.setTextDatum(MC_DATUM); // Middle Center\n`;
                    code += `    spr.drawString((${textValExpr} + "${w.unit || ''}").c_str(), ${w.x}, ${w.y}, ${w.fontSize || 4});\n`;
                    break;

                case 'bar-slider':
                    const barValExpr = w.binding ? `telemetry.${w.binding}` : w.value || 50;
                    code += `    {\n`;
                    code += `        int val = ${barValExpr};\n`;
                    code += `        int fillW = map(val, ${w.min || 0}, ${w.max || 100}, 0, ${w.w || 120});\n`;
                    code += `        spr.fillRect(${w.x}, ${w.y}, ${w.w || 120}, ${w.h || 16}, COLOR_BG_${idx + 1});\n`;
                    code += `        spr.fillRect(${w.x}, ${w.y}, fillW, ${w.h || 16}, COLOR_WIDGET_${idx + 1});\n`;
                    code += `    }\n`;
                    break;

                case 'text-label':
                    code += `    spr.setTextColor(COLOR_WIDGET_${idx + 1}, COLOR_BG);\n`;
                    code += `    spr.setTextDatum(MC_DATUM);\n`;
                    code += `    spr.drawString("${w.text || 'LABEL'}", ${w.x}, ${w.y}, ${w.fontSize || 2});\n`;
                    break;

                case 'status-badge':
                    const flagExpr = w.binding ? `(telemetry.${w.binding} & ${w.flagMask || '0x01'})` : `true`;
                    code += `    if (${flagExpr}) {\n`;
                    code += `        spr.fillRoundRect(${w.x - 30}, ${w.y - 12}, 60, 24, 6, COLOR_WIDGET_${idx + 1});\n`;
                    code += `        spr.setTextColor(COLOR_BG);\n`;
                    code += `        spr.setTextDatum(MC_DATUM);\n`;
                    code += `        spr.drawString("${w.text || 'ALERT'}", ${w.x}, ${w.y}, 2);\n`;
                    code += `    }\n`;
                    break;

                case 'card-box':
                    code += `    spr.fillRoundRect(${w.x}, ${w.y}, ${w.w || 100}, ${w.h || 60}, ${w.borderRadius || 8}, COLOR_BG_${idx + 1});\n`;
                    code += `    spr.drawRoundRect(${w.x}, ${w.y}, ${w.w || 100}, ${w.h || 60}, ${w.borderRadius || 8}, COLOR_WIDGET_${idx + 1});\n`;
                    break;
            }
            code += `\n`;
        });

        code += `    // Push double-buffer sprite to physical TFT screen\n`;
        code += `    spr.pushSprite(0, 0);\n`;
        code += `}\n`;

        return code;
    }
};
