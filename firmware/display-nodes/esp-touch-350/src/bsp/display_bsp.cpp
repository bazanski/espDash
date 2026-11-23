#include "display_bsp.h"
#include "board_config.h"
#include <Arduino_GFX_Library.h>
#include <databus/Arduino_ESP32QSPI.h>
#include <esp_heap_caps.h>

#define LANDSCAPE_WIDTH  480
#define LANDSCAPE_HEIGHT 320

static lv_color_t *disp_buf1 = nullptr;
static uint16_t *rotated_buf = nullptr;
static lv_disp_draw_buf_t draw_buf;
static lv_disp_drv_t disp_drv;

static bool rotation_inverted = false;

static Arduino_ESP32QSPI *bus = new Arduino_ESP32QSPI(
    LCD_PIN_CS, LCD_PIN_CLK, LCD_PIN_D0, LCD_PIN_D1, LCD_PIN_D2, LCD_PIN_D3
);

struct InitCmd {
    uint8_t cmd;
    const uint8_t data[16];
    uint8_t len;
    uint16_t delay_ms;
};

static const InitCmd lcd_init_cmds[] = {
    {0xF1, {0x00}, 1, 0},
    {0x60, {0x00, 0x00, 0x00}, 3, 0},
    {0x65, {0x80}, 1, 0},
    {0x79, {0x06}, 1, 0},
    {0x7B, {0x00, 0x08, 0x08}, 3, 0},
    {0x80, {0x55, 0x62, 0x2F, 0x17, 0xF0, 0x52, 0x70, 0xD2, 0x52, 0x62, 0xEA}, 11, 0},
    {0x81, {0x26, 0x52, 0x72, 0x27}, 4, 0},
    {0x84, {0x92, 0x25}, 2, 0},
    {0x87, {0x10, 0x10, 0x58, 0x00, 0x02, 0x3A}, 6, 0},
    {0x88, {0x00, 0x00, 0x2C, 0x10, 0x04, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x06}, 15, 0},
    {0x89, {0x00, 0x00, 0x00}, 3, 0},
    {0x8A, {0x13, 0x00, 0x2C, 0x00, 0x00, 0x2C, 0x10, 0x10, 0x00, 0x3E, 0x19}, 11, 0},
    {0x8B, {0x15, 0xB1, 0xB1, 0x44, 0x96, 0x2C, 0x10, 0x97, 0x8E}, 9, 0},
    {0x8C, {0x1D, 0xB1, 0xB1, 0x44, 0x96, 0x2C, 0x10, 0x50, 0x0F, 0x01, 0xC5, 0x12, 0x09}, 13, 0},
    {0x8D, {0x0C}, 1, 0},
    {0x8E, {0x33, 0x01, 0x0C, 0x13, 0x01, 0x01}, 6, 0},
    {0xB3, {0x00, 0x30}, 2, 0},
    {0xF1, {0x00}, 1, 0},
    {0x71, {0xD0}, 1, 0},
    {0x66, {0x02, 0x3F}, 2, 0},
    {0xBE, {0x26, 0x00, 0x9D}, 3, 0},
    {0x70, {0x01, 0xA0, 0x11, 0x40, 0xE0, 0x00, 0x11, 0x69, 0x11, 0x00, 0x00, 0x1A}, 12, 0},
    {0x90, {0x04, 0x04, 0x55, 0x74, 0x00, 0x40, 0x43, 0x27, 0x27}, 9, 0},
    {0x91, {0x04, 0x04, 0x55, 0x75, 0x00, 0x40, 0x42, 0x27, 0x27}, 9, 0},
    {0x92, {0x04, 0x44, 0x55, 0xC0, 0x06, 0x00, 0x07, 0x05, 0x90, 0x27}, 10, 0},
    {0x93, {0x04, 0x43, 0x11, 0x00, 0x00, 0x00, 0x00, 0x05, 0x90, 0x27}, 10, 0},
    {0x94, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 6, 0},
    {0x95, {0x96, 0x16, 0x00, 0x00, 0xFF}, 5, 0},
    {0x96, {0x44, 0x53, 0x03, 0x12, 0x23, 0x24, 0x06, 0x05, 0x94, 0x27, 0x00, 0x44}, 12, 0},
    {0x97, {0x44, 0x53, 0x47, 0x56, 0x20, 0x20, 0x02, 0x01, 0x94, 0x27, 0x00, 0x44}, 12, 0},
    {0xBA, {0x55, 0x94, 0x2D, 0x94, 0x27}, 5, 0},
    {0x9A, {0x40, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00}, 7, 0},
    {0x9B, {0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00}, 7, 0},
    {0x9C, {0x5C, 0x12, 0x00, 0x00, 0x10, 0x12, 0x00, 0x00, 0x10, 0x02, 0x00, 0x00, 0x00}, 13, 0},
    {0x9D, {0x8A, 0x51, 0x00, 0x00, 0x00, 0x80, 0x1E, 0x01}, 8, 0},
    {0x9E, {0x51, 0x00, 0x00, 0x00, 0x80, 0x1E, 0x01}, 7, 0},
    {0xB4, {0x1D, 0x1C, 0x1E, 0x0B, 0x14, 0x02, 0x13, 0x09, 0x1E, 0x00, 0x1E, 0x10}, 12, 0},
    {0xB5, {0x1D, 0x1C, 0x1E, 0x0A, 0x15, 0x03, 0x11, 0x08, 0x1E, 0x01, 0x1E, 0x12}, 12, 0},
    {0xB6, {0x77, 0x77, 0x00, 0x0A, 0xFF, 0x0A, 0xFF}, 7, 0},
    {0x86, {0xCD, 0x04, 0xB1, 0x02, 0x58, 0x12, 0x58, 0x0C, 0x13, 0x01, 0xA5, 0x00, 0xA5, 0xA5}, 14, 0},
    {0xB7, {0x07, 0x0A, 0x0E, 0x06, 0x05, 0x03, 0x2B, 0x03, 0x03, 0x42, 0x07, 0x10, 0x10, 0x2E, 0x3F, 0x0D}, 16, 0},
    {0xB8, {0x07, 0x0A, 0x0D, 0x05, 0x05, 0x02, 0x2B, 0x02, 0x03, 0x42, 0x06, 0x10, 0x0F, 0x2E, 0x3F, 0x0D}, 16, 0},
    {0xB9, {0x23, 0x23}, 2, 0},
    {0xBF, {0x10, 0x14, 0x14, 0x0B, 0x0B, 0x0B}, 6, 0},
    {0xF2, {0x00}, 1, 0},
    {0x73, {0x04, 0xDA, 0x12, 0x54, 0x47}, 5, 0},
    {0x77, {0x6B, 0x5B, 0xFD, 0xC3, 0xC5}, 5, 0},
    {0x7A, {0x15, 0x27}, 2, 0},
    {0x7B, {0x04, 0x57}, 2, 0},
    {0x7E, {0x01, 0x0E}, 2, 0},
    {0xBF, {0x36}, 1, 0},
    {0xE3, {0x40, 0x40}, 2, 0},
    {0xF0, {0x00}, 1, 0},
    {0xD0, {0x00}, 1, 0},
    {0x2A, {0x00, 0x00, 0x01, 0x3F}, 4, 0},
    {0x2B, {0x00, 0x00, 0x01, 0xDF}, 4, 0},
    {0x21, {0x00}, 0, 0},
    {0x11, {0x00}, 0, 120},
    {0x29, {0x00}, 0, 0},
    {0x2C, {0x00}, 0, 0},
    {0x3A, {0x01}, 1, 0},
    {0x36, {0x00}, 1, 0},
    {0x35, {0x01}, 1, 20},
};

// ---------------------------------------------------------------------------
// Partial (stripe) rendering. LVGL draws into a small buffer in INTERNAL SRAM
// (a full-frame buffer in PSRAM made every pixel write a cache miss) and each
// flushed rectangle is transposed to the panel's portrait orientation while it
// is still in SRAM, then sent as its own QSPI window.
//
// The ST77922 needs every window to start/end on a 4-pixel boundary, so the
// rounder below snaps all dirty areas to multiples of 4 in both directions
// (which also keeps the transposed window aligned in both rotations).
// Measured before: 149 ms rotate + 20 ms write per full frame, 4.6 fps.
// ---------------------------------------------------------------------------
#define STRIPE_ROWS 40

static void my_rounder(lv_disp_drv_t *drv, lv_area_t *a) {
    a->x1 &= ~3; a->y1 &= ~3;
    a->x2 |= 3;  a->y2 |= 3;
    if (a->x2 > LANDSCAPE_WIDTH - 1)  a->x2 = LANDSCAPE_WIDTH - 1;
    if (a->y2 > LANDSCAPE_HEIGHT - 1) a->y2 = LANDSCAPE_HEIGHT - 1;
}

static void my_disp_flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p) {
    const int x1 = area->x1, x2 = area->x2, y1 = area->y1, y2 = area->y2;
    const int w = x2 - x1 + 1, h = y2 - y1 + 1;
    const uint16_t *src = (const uint16_t *)color_p;
    uint16_t *dst = rotated_buf;
    int px1, px2, py1, py2;

    if (rotation_inverted) {
        // physical px = ly, py = 479 - lx
        for (int lx = x2; lx >= x1; lx--) {
            uint16_t *drow = &dst[(x2 - lx) * h];
            const uint16_t *scol = &src[lx - x1];
            for (int ly = 0; ly < h; ly++) drow[ly] = scol[ly * w];
        }
        px1 = y1; px2 = y2;
        py1 = LCD_HEIGHT - 1 - x2; py2 = LCD_HEIGHT - 1 - x1;
    } else {
        // physical px = 319 - ly, py = lx
        for (int lx = x1; lx <= x2; lx++) {
            uint16_t *drow = &dst[(lx - x1) * h];
            const uint16_t *scol = &src[lx - x1];
            for (int ly = 0; ly < h; ly++) drow[h - 1 - ly] = scol[ly * w];
        }
        px1 = LCD_WIDTH - 1 - y2; px2 = LCD_WIDTH - 1 - y1;
        py1 = x1; py2 = x2;
    }

    bus->writeC8D16D16(0x2A, px1, px2);
    bus->writeC8D16D16(0x2B, py1, py2);
    bus->writePixels(rotated_buf, (uint32_t)w * h);
    lv_disp_flush_ready(drv);
}

void display_set_backlight(uint8_t brightness_pct) {
    digitalWrite(LCD_PIN_BL, (brightness_pct > 0) ? HIGH : LOW);
}

void display_init() {
    Serial.println("[Display] Initializing QSPI hardware bus...");
    bus->begin(80000000UL, 0);

    Serial.println("[Display] Sending ST77922 init commands...");
    for (size_t i = 0; i < sizeof(lcd_init_cmds) / sizeof(lcd_init_cmds[0]); i++) {
        uint8_t c = lcd_init_cmds[i].cmd;
        uint8_t len = lcd_init_cmds[i].len;
        const uint8_t *d = lcd_init_cmds[i].data;

        if (len == 0) {
            bus->writeCommand16(((uint16_t)c) << 8);
        } else if (len == 1) {
            bus->writeC8D8(c, d[0]);
        } else {
            bus->writeC8Bytes(c, (uint8_t *)d, len);
        }

        if (lcd_init_cmds[i].delay_ms > 0) {
            delay(lcd_init_cmds[i].delay_ms);
        }
    }

    // Turn on Backlight
    pinMode(LCD_PIN_BL, OUTPUT);
    digitalWrite(LCD_PIN_BL, HIGH);

    // Stripe buffers live in internal SRAM (fast); see the comment above flush.
    size_t stripe_px = (size_t)LANDSCAPE_WIDTH * STRIPE_ROWS;
    disp_buf1 = (lv_color_t *)heap_caps_malloc(stripe_px * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    rotated_buf = (uint16_t *)heap_caps_malloc(stripe_px * sizeof(uint16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!disp_buf1 || !rotated_buf) {
        Serial.println("[Display] FATAL: cannot allocate internal stripe buffers");
        return;
    }

    // Initialize LVGL natively in 480x320 landscape mode
    lv_init();
    lv_disp_draw_buf_init(&draw_buf, disp_buf1, NULL, LANDSCAPE_WIDTH * STRIPE_ROWS);

    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = LANDSCAPE_WIDTH;   // 480
    disp_drv.ver_res = LANDSCAPE_HEIGHT;  // 320
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;
    disp_drv.full_refresh = 0;            // partial: only dirty stripes are rendered and sent
    disp_drv.rounder_cb = my_rounder;     // 4-pixel aligned windows (panel requirement)
    disp_drv.rotated = LV_DISP_ROT_NONE;  // Native landscape rendering in LVGL
    disp_drv.sw_rotate = 0;
    lv_disp_drv_register(&disp_drv);

    Serial.println("[Display] ST77922 QSPI 480x320 partial-stripe SRAM pipeline initialized.");
}

void display_toggle_rotation() {
    rotation_inverted = !rotation_inverted;
    Serial.printf("[Display] Switched orientation to %s\n", rotation_inverted ? "270° (Inverted Landscape)" : "90° (Standard Landscape)");
    lv_obj_invalidate(lv_scr_act());
}

bool display_is_rotation_inverted() {
    return rotation_inverted;
}
