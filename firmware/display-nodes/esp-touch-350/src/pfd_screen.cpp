#include "pfd_screen.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

// ---- palette (matched to the reference PFD) -----------------------------
#define COL_TAPE    lv_color_hex(0x343650)
#define COL_SKY     lv_color_hex(0x0096C8)
#define COL_GROUND  lv_color_hex(0xAA6400)
#define COL_WHITE   lv_color_hex(0xFFFFFF)
#define COL_BLACK   lv_color_hex(0x000000)
#define COL_AMBER   lv_color_hex(0xFFB000)
#define COL_GREEN   lv_color_hex(0x30FF40)
#define COL_RED     lv_color_hex(0xFF2020)
#define COL_MAGENTA lv_color_hex(0xFF40D0)
#define COL_CYAN    lv_color_hex(0x00E5FF)
#define COL_DIM     lv_color_hex(0x9098B0)

// ---- layout (480 x 320) ---------------------------------------------------
#define ATT_X0   92
#define ATT_X1   335
#define ATT_Y0   30
#define ATT_Y1   249
#define ATT_CX   214
#define ATT_CY   140
#define PPD      4.4f      // attitude pixels per degree of pitch
#define SPD_X0   4
#define SPD_X1   84
#define RPM_X0   352
#define RPM_X1   432
#define G_X0     438
#define G_X1     476
#define TAPE_Y0  30
#define TAPE_Y1  249
#define TAPE_CY  140

static PfdData g;
static lv_obj_t *obj = NULL;
static float accel_ema = 0;     // km/h per second, for the speed trend vector
static float last_speed = 0;
static uint32_t last_trend_ms = 0;

// ---- small drawing helpers -------------------------------------------------
struct Clip {
    lv_draw_ctx_t *c; const lv_area_t *old; lv_area_t cur; bool ok;
    Clip(lv_draw_ctx_t *ctx, const lv_area_t &a) : c(ctx), old(ctx->clip_area) {
        ok = _lv_area_intersect(&cur, old, &a);
        if (ok) c->clip_area = &cur;
    }
    ~Clip() { c->clip_area = old; }
};

static void fill(lv_draw_ctx_t *c, int x1, int y1, int x2, int y2, lv_color_t col,
                 int radius = 0, int bw = 0, lv_color_t bc = COL_WHITE) {
    lv_draw_rect_dsc_t d; lv_draw_rect_dsc_init(&d);
    d.bg_color = col; d.bg_opa = LV_OPA_COVER;
    d.radius = radius;
    d.border_width = bw; d.border_color = bc; d.border_opa = bw ? LV_OPA_COVER : LV_OPA_TRANSP;
    lv_area_t a = {(lv_coord_t)x1, (lv_coord_t)y1, (lv_coord_t)x2, (lv_coord_t)y2};
    lv_draw_rect(c, &d, &a);
}

static void line(lv_draw_ctx_t *c, float x1, float y1, float x2, float y2, int w, lv_color_t col) {
    lv_draw_line_dsc_t d; lv_draw_line_dsc_init(&d);
    d.color = col; d.width = w; d.opa = LV_OPA_COVER;
    lv_point_t p1 = {(lv_coord_t)lroundf(x1), (lv_coord_t)lroundf(y1)};
    lv_point_t p2 = {(lv_coord_t)lroundf(x2), (lv_coord_t)lroundf(y2)};
    lv_draw_line(c, &d, &p1, &p2);
}

static void poly(lv_draw_ctx_t *c, const lv_point_t *p, int n, lv_color_t col) {
    lv_draw_rect_dsc_t d; lv_draw_rect_dsc_init(&d);
    d.bg_color = col; d.bg_opa = LV_OPA_COVER; d.border_width = 0;
    lv_draw_polygon(c, &d, p, n);
}

// align: -1 left (x = left edge), 0 centre, 1 right (x = right edge); y = vertical centre
static void text(lv_draw_ctx_t *c, const char *s, const lv_font_t *f, lv_color_t col, int x, int y, int align) {
    lv_point_t sz;
    lv_txt_get_size(&sz, s, f, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    int x0 = (align < 0) ? x : (align == 0 ? x - sz.x / 2 : x - sz.x);
    int y0 = y - sz.y / 2;
    lv_draw_label_dsc_t d; lv_draw_label_dsc_init(&d);
    d.color = col; d.font = f; d.opa = LV_OPA_COVER;
    lv_area_t a = {(lv_coord_t)x0, (lv_coord_t)y0, (lv_coord_t)(x0 + sz.x + 2), (lv_coord_t)(y0 + sz.y + 2)};
    lv_draw_label(c, &d, &a, s, NULL);
}

static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// "Red X" flag centred in a box.
static void flag(lv_draw_ctx_t *c, const char *s, int cx, int cy) {
    fill(c, cx - 22, cy - 12, cx + 22, cy + 12, COL_BLACK, 2, 2, COL_RED);
    text(c, s, &lv_font_montserrat_14, COL_RED, cx, cy, 0);
}

// ---------------------------------------------------------------------------
// ATTITUDE BALL
// ---------------------------------------------------------------------------
static void draw_attitude(lv_draw_ctx_t *c) {
    lv_area_t A = {ATT_X0, ATT_Y0, ATT_X1, ATT_Y1};
    Clip clip(c, A);
    if (!clip.ok) return;

    lv_draw_mask_radius_param_t mp;
    lv_draw_mask_radius_init(&mp, &A, 20, false);
    int16_t mid = lv_draw_mask_add(&mp, NULL);

    fill(c, ATT_X0, ATT_Y0, ATT_X1, ATT_Y1, COL_SKY);

    if (!g.imu_ok) {
        fill(c, ATT_X0, ATT_Y0, ATT_X1, ATT_Y1, lv_color_hex(0x101820));
        lv_draw_mask_free_param(lv_draw_mask_remove_id(mid));
        flag(c, "ATT", ATT_CX, ATT_CY);
        return;
    }

    const float DEG = 0.017453292f;
    float phi = g.roll * DEG;                 // horizon turns counter-clockwise on a right bank
    float cs = cosf(phi), sn = sinf(phi);
    float hx = cs, hy = -sn;                  // along the horizon
    float nx = sn, ny = cs;                   // "down", perpendicular to it
    float pitch = clampf(g.pitch_up, -60.f, 60.f);
    float c0x = ATT_CX + nx * pitch * PPD;
    float c0y = ATT_CY + ny * pitch * PPD;

    // ground = half plane below the horizon
    const float L = 420.0f;
    lv_point_t gp[4] = {
        {(lv_coord_t)lroundf(c0x - hx * L),           (lv_coord_t)lroundf(c0y - hy * L)},
        {(lv_coord_t)lroundf(c0x + hx * L),           (lv_coord_t)lroundf(c0y + hy * L)},
        {(lv_coord_t)lroundf(c0x + hx * L + nx * L),  (lv_coord_t)lroundf(c0y + hy * L + ny * L)},
        {(lv_coord_t)lroundf(c0x - hx * L + nx * L),  (lv_coord_t)lroundf(c0y - hy * L + ny * L)},
    };
    poly(c, gp, 4, COL_GROUND);
    line(c, c0x - hx * L, c0y - hy * L, c0x + hx * L, c0y + hy * L, 2, COL_WHITE);

    // pitch ladder, every 5 deg, numbered every 10
    for (int k = -30; k <= 30; k += 5) {
        if (k == 0) continue;
        float off = (pitch - k) * PPD;                 // distance of this rung from the centre, along n
        if (fabsf(off) > 135.0f) continue;
        float px = c0x - nx * k * PPD, py = c0y - ny * k * PPD;
        bool major = (k % 10 == 0);
        float half = major ? 34.0f : 17.0f;
        line(c, px - hx * half, py - hy * half, px + hx * half, py + hy * half, 2, COL_WHITE);
        if (major) {
            char b[8]; snprintf(b, sizeof(b), "%d", abs(k));
            text(c, b, &lv_font_montserrat_14, COL_WHITE, lroundf(px - hx * (half + 14)), lroundf(py - hy * (half + 14)), 0);
            text(c, b, &lv_font_montserrat_14, COL_WHITE, lroundf(px + hx * (half + 14)), lroundf(py + hy * (half + 14)), 0);
        }
    }

    // roll scale: fixed ticks on an arc at the top
    const float R = 100.0f;
    static const int ticks[] = {-45, -30, -20, -10, 10, 20, 30, 45};
    for (int a : ticks) {
        float ar = a * DEG;
        float len = (abs(a) >= 30) ? 11.f : 6.f;
        line(c, ATT_CX + R * sinf(ar), ATT_CY - R * cosf(ar),
                ATT_CX + (R - len) * sinf(ar), ATT_CY - (R - len) * cosf(ar), 2, COL_WHITE);
    }
    // fixed zero marker
    lv_point_t zp[3] = {{ATT_CX - 7, ATT_Y0 + 2}, {ATT_CX + 7, ATT_Y0 + 2}, {ATT_CX, ATT_Y0 + 14}};
    poly(c, zp, 3, COL_WHITE);

    // sky pointer + skid bar ride on the rotating horizon
    auto rot = [&](float x, float y, lv_point_t &o) {
        o.x = (lv_coord_t)lroundf(ATT_CX + x * cs + y * sn);
        o.y = (lv_coord_t)lroundf(ATT_CY - x * sn + y * cs);
    };
    lv_point_t pp[3];
    rot(0, -(R - 3), pp[0]); rot(-7, -(R - 15), pp[1]); rot(7, -(R - 15), pp[2]);
    poly(c, pp, 3, COL_AMBER);
    // skid: bar moves toward the outside of a turn (= toward +left when lin_y > 0 ... screen -x)
    float dx = clampf(g.lin_y, -0.6f, 0.6f) * 60.0f;
    lv_point_t sp[4];
    rot(dx - 9, -(R - 18), sp[0]); rot(dx + 9, -(R - 18), sp[1]);
    rot(dx + 7, -(R - 24), sp[2]); rot(dx - 7, -(R - 24), sp[3]);
    poly(c, sp, 4, COL_AMBER);

    // fixed aircraft symbol
    for (int side = -1; side <= 1; side += 2) {
        int xin = ATT_CX + side * 30, xout = ATT_CX + side * 82;
        int xa = side < 0 ? xout : xin, xb = side < 0 ? xin : xout;
        fill(c, xa, ATT_CY - 3, xb, ATT_CY + 3, COL_BLACK, 0, 1, COL_WHITE);
        int vx1 = side < 0 ? xin - 6 : xin, vx2 = side < 0 ? xin : xin + 6;
        fill(c, vx1, ATT_CY - 3, vx2, ATT_CY + 13, COL_BLACK, 0, 1, COL_WHITE);
    }
    fill(c, ATT_CX - 3, ATT_CY - 3, ATT_CX + 3, ATT_CY + 3, COL_BLACK, 0, 1, COL_WHITE);

    lv_draw_mask_free_param(lv_draw_mask_remove_id(mid));
}

// ---------------------------------------------------------------------------
// SPEED TAPE (left)
// ---------------------------------------------------------------------------
static void draw_speed(lv_draw_ctx_t *c) {
    lv_area_t T = {SPD_X0, TAPE_Y0, SPD_X1, TAPE_Y1};
    {
        Clip clip(c, T);
        if (!clip.ok) return;
        fill(c, SPD_X0, TAPE_Y0, SPD_X1, TAPE_Y1, COL_TAPE);
        if (g.link) {
            const float PX = 2.2f;
            float v = clampf(g.speed_kmh, 0, 400);
            int s0 = ((int)floorf((v - 60.f) / 5.f)) * 5;
            for (int s = s0; s <= v + 60; s += 5) {
                if (s < 0) continue;
                float y = TAPE_CY - (s - v) * PX;
                bool major = (s % 10 == 0);
                line(c, SPD_X1 - (major ? 14 : 7), y, SPD_X1, y, 2, COL_WHITE);
                if (s % 20 == 0) {
                    char b[8]; snprintf(b, sizeof(b), "%d", s);
                    text(c, b, &lv_font_montserrat_16, COL_WHITE, SPD_X1 - 20, lroundf(y), 1);
                }
            }
            // trend vector (where the speed will be in ~8 s)
            float trend = clampf(accel_ema * 8.0f, -40.f, 40.f);
            if (fabsf(trend) > 3.0f) {
                float yt = TAPE_CY - trend * PX;
                line(c, SPD_X1 - 3, TAPE_CY, SPD_X1 - 3, yt, 3, COL_GREEN);
                int dir = trend > 0 ? 1 : -1;
                lv_point_t ah[3] = {{SPD_X1 - 3, (lv_coord_t)lroundf(yt)},
                                    {SPD_X1 - 10, (lv_coord_t)lroundf(yt + dir * 9)},
                                    {SPD_X1 + 4, (lv_coord_t)lroundf(yt + dir * 9)}};
                poly(c, ah, 3, COL_GREEN);
            }
        }
        fill(c, SPD_X0 + 1, TAPE_Y0 + 1, SPD_X0 + 36, TAPE_Y0 + 17, COL_BLACK);
        text(c, "KM/H", &lv_font_montserrat_12, COL_DIM, SPD_X0 + 4, TAPE_Y0 + 9, -1);
    }
    if (!g.link) { flag(c, "SPD", (SPD_X0 + SPD_X1) / 2, TAPE_CY); return; }
    char b[8]; snprintf(b, sizeof(b), "%d", (int)lroundf(g.speed_kmh));
    fill(c, 6, TAPE_CY - 17, 72, TAPE_CY + 17, COL_BLACK, 0, 2, COL_WHITE);
    text(c, b, &lv_font_montserrat_28, COL_WHITE, 39, TAPE_CY, 0);
    line(c, 72, TAPE_CY, SPD_X1, TAPE_CY, 3, COL_AMBER);
}

// ---------------------------------------------------------------------------
// RPM TAPE (right) + LONGITUDINAL G SCALE
// ---------------------------------------------------------------------------
static float rpm_y(float s, float r) { return TAPE_CY - (s - r) * 0.07f; }   // 7 px / 100 rpm

static void draw_rpm(lv_draw_ctx_t *c) {
    lv_area_t T = {RPM_X0, TAPE_Y0, RPM_X1, TAPE_Y1};
    {
        Clip clip(c, T);
        if (!clip.ok) return;
        fill(c, RPM_X0, TAPE_Y0, RPM_X1, TAPE_Y1, COL_TAPE);
        if (g.link) {
            float r = clampf(g.rpm, 0, 9000);
            // zones along the right edge: amber from 4500, red from 6200
            float y45 = rpm_y(4500, r), y62 = rpm_y(6200, r), y90 = rpm_y(9000, r);
            fill(c, RPM_X1 - 5, (int)y62, RPM_X1, (int)y45, COL_AMBER);
            fill(c, RPM_X1 - 5, (int)y90, RPM_X1, (int)y62, COL_RED);
            int s0 = ((int)floorf((r - 1700.f) / 100.f)) * 100;
            for (int s = s0; s <= r + 1700; s += 100) {
                if (s < 0 || s > 9000) continue;
                float y = rpm_y(s, r);
                bool major = (s % 500 == 0);
                line(c, RPM_X0, y, RPM_X0 + (major ? 14 : 7), y, 2, COL_WHITE);
                if (major) {
                    char b[8]; snprintf(b, sizeof(b), "%d", s / 100);
                    text(c, b, &lv_font_montserrat_16, COL_WHITE, RPM_X0 + 20, lroundf(y), -1);
                }
            }
        }
        fill(c, RPM_X0 + 1, TAPE_Y0 + 1, RPM_X0 + 62, TAPE_Y0 + 17, COL_BLACK);
        text(c, "RPM x100", &lv_font_montserrat_12, COL_DIM, RPM_X0 + 4, TAPE_Y0 + 9, -1);
    }
    if (!g.link) { flag(c, "RPM", (RPM_X0 + RPM_X1) / 2, TAPE_CY); return; }
    char b[8]; snprintf(b, sizeof(b), "%d", (int)lroundf(g.rpm));
    lv_color_t col = g.rpm > 6200 ? COL_RED : (g.rpm > 4500 ? COL_AMBER : COL_WHITE);
    fill(c, RPM_X0 + 6, TAPE_CY - 15, RPM_X1 - 8, TAPE_CY + 15, COL_BLACK, 0, 2, COL_WHITE);
    text(c, b, &lv_font_montserrat_24, col, (RPM_X0 + RPM_X1) / 2 - 1, TAPE_CY, 0);
    lv_point_t ptr[3] = {{RPM_X0 - 1, TAPE_CY}, {RPM_X0 - 12, TAPE_CY - 7}, {RPM_X0 - 12, TAPE_CY + 7}};
    poly(c, ptr, 3, COL_AMBER);
}

static void draw_gscale(lv_draw_ctx_t *c) {
    lv_area_t T = {G_X0, TAPE_Y0, G_X1, TAPE_Y1};
    Clip clip(c, T);
    if (!clip.ok) return;
    fill(c, G_X0, TAPE_Y0, G_X1, TAPE_Y1, COL_TAPE);
    text(c, "G", &lv_font_montserrat_12, COL_DIM, G_X1 - 12, TAPE_Y0 + 9, 0);
    for (int i = -4; i <= 4; i++) {
        float y = TAPE_CY - i * 20.0f;
        bool major = (i % 2 == 0);
        line(c, G_X0, y, G_X0 + (major ? 10 : 5), y, 2, COL_WHITE);
        if (i != 0 && major) {
            if (abs(i) == 4) text(c, "1", &lv_font_montserrat_14, COL_WHITE, G_X0 + 17, lroundf(y), 0);
            else             text(c, ".5", &lv_font_montserrat_12, COL_WHITE, G_X0 + 20, lroundf(y), 0);
        }
    }
    line(c, G_X0, TAPE_CY, G_X1, TAPE_CY, 1, COL_DIM);
    if (!g.imu_ok) { flag(c, "G", (G_X0 + G_X1) / 2, TAPE_CY); return; }
    float gl = clampf(g.lin_x, -1.3f, 1.3f);
    float y = TAPE_CY - gl * 80.0f;
    line(c, G_X0 + 2, TAPE_CY, G_X1 - 2, y, 3, COL_WHITE);
    char b[10]; snprintf(b, sizeof(b), "%+.2f", g.lin_x);
    fill(c, G_X0 + 1, TAPE_Y1 - 17, G_X1 - 1, TAPE_Y1 - 1, COL_BLACK);
    text(c, b, &lv_font_montserrat_12, COL_WHITE, (G_X0 + G_X1) / 2, TAPE_Y1 - 9, 0);
}

// ---------------------------------------------------------------------------
// HEADING ARC (bottom)
// ---------------------------------------------------------------------------
static void draw_heading(lv_draw_ctx_t *c) {
    const int cx = ATT_CX, R = 190, cyc = 264 + R;
    lv_point_t hp[3] = {{cx - 7, 251}, {cx + 7, 251}, {cx, 263}};
    poly(c, hp, 3, COL_WHITE);
    lv_area_t B = {ATT_X0, 254, ATT_X1, 319};
    Clip clip(c, B);
    if (!clip.ok) return;
    fill(c, cx - R, cyc - R, cx + R, cyc + R, COL_TAPE, LV_RADIUS_CIRCLE);

    if (g.imu_ok) {
        const float DEG = 0.017453292f;
        float hd = g.heading;
        int h0 = ((int)floorf((hd - 40.f) / 5.f)) * 5;
        for (int h = h0; h <= hd + 40; h += 5) {
            float d = (h - hd) * DEG;
            bool ten = (h % 10 == 0);
            float r2 = R - (ten ? 14 : 8);
            line(c, cx + R * sinf(d), cyc - R * cosf(d), cx + r2 * sinf(d), cyc - r2 * cosf(d), 2, COL_WHITE);
            if (ten && fabsf(h - hd) <= 32) {
                int v = ((h % 360) + 360) % 360;
                char b[4];
                if (v == 0) strcpy(b, "N"); else if (v == 90) strcpy(b, "E");
                else if (v == 180) strcpy(b, "S"); else if (v == 270) strcpy(b, "W");
                else snprintf(b, sizeof(b), "%d", v / 10);
                float rl = R - 30;
                text(c, b, &lv_font_montserrat_16, COL_WHITE, lroundf(cx + rl * sinf(d)), lroundf(cyc - rl * cosf(d)), 0);
            }
        }
        char hb[16]; snprintf(hb, sizeof(hb), "HDG %03d", (int)lroundf(hd) % 360);
        text(c, hb, &lv_font_montserrat_12, COL_WHITE, ATT_X0 + 2, 263, -1);
        text(c, g.heading_mag ? "MAG" : "REL", &lv_font_montserrat_12,
             g.heading_mag ? COL_GREEN : COL_AMBER, cx, 311, 0);
        char lb[16]; snprintf(lb, sizeof(lb), "LAT %.2f", fabsf(g.lin_y));
        text(c, lb, &lv_font_montserrat_12, COL_MAGENTA, ATT_X1 - 2, 263, 1);
    } else {
        flag(c, "HDG", cx, 292);
    }
}

// ---------------------------------------------------------------------------
// TOP BAR + SIDE READOUTS
// ---------------------------------------------------------------------------
static void draw_topbar(lv_draw_ctx_t *c) {
    fill(c, ATT_X0, 2, ATT_X1, 26, COL_TAPE);
    int dx1 = ATT_X0 + 81, dx2 = ATT_X0 + 162;
    line(c, dx1, 2, dx1, 26, 2, COL_WHITE);
    line(c, dx2, 2, dx2, 26, 2, COL_WHITE);

    static const char sel[] = {'P', 'R', 'N', 'D', 'S'};
    char gb[12];
    char s = sel[g.gear_sel < 5 ? g.gear_sel : 2];
    if (!g.link) snprintf(gb, sizeof(gb), "--");
    else if (g.gear_num > 0) snprintf(gb, sizeof(gb), "%c  %d", s, g.gear_num);
    else snprintf(gb, sizeof(gb), "%c", s);
    text(c, gb, &lv_font_montserrat_16, g.link ? COL_WHITE : COL_DIM, ATT_X0 + 40, 14, 0);

    const char *mode = !g.link ? "--" : g.econ ? "ECON" : g.sport ? "SPORT" : "NORMAL";
    lv_color_t mc = !g.link ? COL_DIM : g.econ ? COL_GREEN : g.sport ? COL_AMBER : COL_WHITE;
    text(c, mode, &lv_font_montserrat_14, mc, ATT_X0 + 121, 14, 0);

    text(c, g.link_text ? g.link_text : "", &lv_font_montserrat_14, lv_color_hex(g.link_color), ATT_X0 + 203, 14, 0);
}

static void draw_readouts(lv_draw_ctx_t *c) {
    char b[24];
    // left: coolant + battery
    text(c, "COOLANT", &lv_font_montserrat_12, COL_DIM, 6, 262, -1);
    if (g.link) {
        snprintf(b, sizeof(b), "%d\xC2\xB0""C", (int)lroundf(g.coolant_c));
        lv_color_t col = g.coolant_c > 105 ? COL_RED : (g.coolant_c < 70 ? COL_CYAN : COL_GREEN);
        text(c, b, &lv_font_montserrat_20, col, 6, 281, -1);
        snprintf(b, sizeof(b), "BATT %.1fV", g.batt_v);
        text(c, b, &lv_font_montserrat_14, (g.batt_v < 12.2f && g.batt_v > 5.f) ? COL_AMBER : COL_GREEN, 6, 304, -1);
    } else {
        text(c, "--", &lv_font_montserrat_20, COL_DIM, 6, 281, -1);
    }
    // right: fuel
    text(c, "FUEL", &lv_font_montserrat_12, COL_DIM, RPM_X0 + 4, 262, -1);
    if (g.link && g.fuel_valid) {
        snprintf(b, sizeof(b), "%d %%", g.fuel_pct);
        text(c, b, &lv_font_montserrat_20, g.low_fuel ? COL_RED : COL_GREEN, RPM_X0 + 4, 281, -1);
        if (g.low_fuel) text(c, "LOW FUEL", &lv_font_montserrat_14, COL_RED, RPM_X0 + 4, 304, -1);
    } else {
        text(c, "--", &lv_font_montserrat_20, COL_DIM, RPM_X0 + 4, 281, -1);
    }
}

static void pfd_draw(lv_event_t *e) {
    lv_draw_ctx_t *c = lv_event_get_draw_ctx(e);
    draw_attitude(c);
    draw_speed(c);
    draw_rpm(c);
    draw_gscale(c);
    draw_heading(c);
    draw_topbar(c);
    draw_readouts(c);
}

// ---------------------------------------------------------------------------
static lv_obj_t *corner_button(lv_obj_t *p, const char *txt, int x, lv_event_cb_t cb) {
    lv_obj_t *b = lv_btn_create(p);
    lv_obj_set_size(b, 96, 24);
    lv_obj_set_pos(b, x, 2);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x1B2430), 0);
    lv_obj_set_style_border_color(b, lv_color_hex(0x00E5FF), 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_radius(b, 12, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_color(l, lv_color_hex(0x00E5FF), 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
    lv_obj_center(l);
    return b;
}

lv_obj_t *pfd_screen_build(lv_event_cb_t on_next) {
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, COL_BLACK, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    obj = lv_obj_create(scr);
    lv_obj_set_size(obj, 480, 320);
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_style_bg_color(obj, COL_BLACK, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(obj, pfd_draw, LV_EVENT_DRAW_MAIN, NULL);

    corner_button(scr, "NEXT: CAR >", 380, on_next);
    memset(&g, 0, sizeof(g));
    return scr;
}

void pfd_screen_update(const PfdData &d) {
    uint32_t now = lv_tick_get();
    if (d.link) {
        if (last_trend_ms == 0) { last_speed = d.speed_kmh; last_trend_ms = now; }
        uint32_t dt = now - last_trend_ms;
        if (dt >= 100) {
            float inst = (d.speed_kmh - last_speed) / (dt * 0.001f);
            accel_ema = 0.75f * accel_ema + 0.25f * inst;
            last_speed = d.speed_kmh; last_trend_ms = now;
        }
    } else { accel_ema = 0; last_trend_ms = 0; }
    g = d;
    if (obj) lv_obj_invalidate(obj);
}

void pfd_demo_fill(PfdData &d, uint32_t ms) {
    float t = ms * 0.001f;
    memset(&d, 0, sizeof(d));
    d.link = true; d.imu_ok = true;
    d.speed_kmh = 70 + 55 * sinf(t * 0.35f);
    d.rpm = 2200 + d.speed_kmh * 28 + 300 * sinf(t * 1.7f);
    d.gear_sel = 3; d.gear_num = d.speed_kmh < 25 ? 1 : d.speed_kmh < 50 ? 2 : d.speed_kmh < 80 ? 3 : d.speed_kmh < 110 ? 4 : 5;
    d.coolant_c = 88; d.batt_v = 14.1f; d.fuel_pct = 61; d.fuel_valid = true; d.econ = true;
    d.roll = 12 * sinf(t * 0.8f);
    d.pitch_up = 5 * sinf(t * 0.5f);
    d.heading = fmodf(t * 8.0f, 360.0f);
    d.heading_mag = true;
    d.lin_x = 0.25f * cosf(t * 0.35f);
    d.lin_y = -0.3f * sinf(t * 0.8f);
    d.link_text = "GW LIVE"; d.link_color = 0x30FF40;
}
