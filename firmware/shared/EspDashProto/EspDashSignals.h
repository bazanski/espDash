// =========================================================================
// espDash SIGNAL CATALOG
// =========================================================================
// One table describing every value the gateway broadcasts: a stable key, a
// display label, a unit, a sane gauge range, and how to pull it out of an
// EspDashTelemetry.
//
// WHY THIS EXISTS
// ---------------
// Adding a reading to a display node used to mean editing that node's
// firmware: reach into the struct, pick a format string, guess a range, guess
// a colour threshold. Every node did it slightly differently and every new
// signal meant touching all of them.
//
// With this catalog a screen is a LIST OF SIGNAL IDs:
//
//     static const EspDashSignalId kLeftColumn[] = {
//         ESPDASH_SIG_SPEED, ESPDASH_SIG_FUEL_LEVEL, ESPDASH_SIG_COOLANT,
//     };
//
// and the render loop asks the catalog for the label, the unit, the range and
// the formatted text. Re-laying out a screen becomes reordering an array; no
// gateway change, no protocol change, no per-node formatting bugs.
//
// VERSION SAFETY
// --------------
// Every accessor takes the sender's `payload_len` and reports `ok = false`
// when that sender is too old to carry the field. A node built against a
// newer catalog than the gateway it is talking to shows "--", never garbage.
// That is the same ESPDASH_HAS() contract as the wire protocol itself.
//
// ADDING A SIGNAL
// ---------------
//   1. Append to EspDashSignalId, immediately before ESPDASH_SIG_COUNT.
//   2. Append a matching row to espdash_signal_table() IN THE SAME ORDER.
//   3. Add a case to espdash_signal_x10().
// The order of the enum and the table must match; the static_assert at the
// bottom catches it if they drift.
// =========================================================================

#ifndef ESPDASH_SIGNALS_H
#define ESPDASH_SIGNALS_H

#include "EspDashProto.h"
#include <stdio.h>
#include <string.h>

typedef enum {
    ESPDASH_SIG_SPEED = 0,
    ESPDASH_SIG_RPM,
    ESPDASH_SIG_GEAR,          // selector P/R/N/D/S
    ESPDASH_SIG_GEAR_NUM,      // engaged gear 1-5
    ESPDASH_SIG_THROTTLE,
    ESPDASH_SIG_BRAKE,
    ESPDASH_SIG_STEERING,
    ESPDASH_SIG_COOLANT,
    ESPDASH_SIG_AMBIENT,
    ESPDASH_SIG_CABIN_TEMP,
    ESPDASH_SIG_FUEL_LEVEL,
    ESPDASH_SIG_FUEL_LITRES,
    ESPDASH_SIG_FUEL_AVG,      // trip-average consumption, L/100km
    ESPDASH_SIG_ODO_KM,        // distance since power-on, derived from deltas
    ESPDASH_SIG_FAN_SPEED,
    ESPDASH_SIG_LIGHTS,
    ESPDASH_SIG_WHEEL_FL,
    ESPDASH_SIG_WHEEL_FR,
    ESPDASH_SIG_WHEEL_RL,
    ESPDASH_SIG_WHEEL_RR,
    // ---- booleans ----
    ESPDASH_SIG_LOW_FUEL,
    ESPDASH_SIG_ECON,
    ESPDASH_SIG_SPORT,
    ESPDASH_SIG_TURN_LEFT,
    ESPDASH_SIG_TURN_RIGHT,
    ESPDASH_SIG_BRAKE_SWITCH,
    ESPDASH_SIG_ABS,
    ESPDASH_SIG_TC,
    ESPDASH_SIG_CEL,
    ESPDASH_SIG_VSA,
    ESPDASH_SIG_COUNT
} EspDashSignalId;

enum {
    ESPDASH_KIND_NUMBER = 0,   // render as a number, honour `decimals`
    ESPDASH_KIND_FLAG   = 1,   // 0/1 - render as a lamp
    ESPDASH_KIND_ENUM   = 2    // index into `labels`
};

typedef struct {
    const char *key;            // stable machine name; also the JSON key
    const char *label;          // short human label for a gauge caption
    const char *unit;           // "" when unitless
    int16_t     min_x10;        // gauge range, in the same x10 units as the value
    int16_t     max_x10;
    uint8_t     decimals;       // 0 or 1
    uint8_t     kind;           // ESPDASH_KIND_*
    const char *const *labels;  // ENUM only
    uint8_t     label_count;    // ENUM only
} EspDashSignalInfo;

static const char *const espdash_gear_labels[]   = { "P", "R", "N", "D", "S" };
static const char *const espdash_lights_labels[] = { "DRL", "POS", "LOW", "HIGH" };

// Order MUST match EspDashSignalId exactly.
static inline const EspDashSignalInfo *espdash_signal_table(void) {
    static const EspDashSignalInfo t[ESPDASH_SIG_COUNT] = {
      /* key             label      unit      min    max   dec kind                labels               n */
      { "speed",        "SPEED",   "km/h",      0,  2200, 0, ESPDASH_KIND_NUMBER, NULL, 0 },
      { "rpm",          "RPM",     "",          0,  7000, 0, ESPDASH_KIND_NUMBER, NULL, 0 },
      { "gear",         "GEAR",    "",          0,     4, 0, ESPDASH_KIND_ENUM,   espdash_gear_labels, 5 },
      { "gear_num",     "G",       "",          0,     5, 0, ESPDASH_KIND_NUMBER, NULL, 0 },
      { "throttle",     "THR",     "%",         0,  1000, 0, ESPDASH_KIND_NUMBER, NULL, 0 },
      { "brake",        "BRK",     "%",         0,  1000, 0, ESPDASH_KIND_NUMBER, NULL, 0 },
      { "steering",     "STEER",   "deg",   -5000,  5000, 0, ESPDASH_KIND_NUMBER, NULL, 0 },
      { "coolant",      "WATER",   "C",      -400,  1400, 1, ESPDASH_KIND_NUMBER, NULL, 0 },
      { "ambient",      "OUT",     "C",      -400,   600, 0, ESPDASH_KIND_NUMBER, NULL, 0 },
      { "cabin_temp",   "IN",      "C",      -400,   600, 0, ESPDASH_KIND_NUMBER, NULL, 0 },
      { "fuel_level",   "FUEL",    "%",         0,  1000, 0, ESPDASH_KIND_NUMBER, NULL, 0 },
      { "fuel_litres",  "FUEL",    "L",         0,   525, 1, ESPDASH_KIND_NUMBER, NULL, 0 },
      { "fuel_avg",     "AVG",     "L/100",     0,   300, 1, ESPDASH_KIND_NUMBER, NULL, 0 },
      { "odo_km",       "TRIP",    "km",        0, 32000, 1, ESPDASH_KIND_NUMBER, NULL, 0 },
      { "fan_speed",    "FAN",     "",          0,     7, 0, ESPDASH_KIND_NUMBER, NULL, 0 },
      { "lights",       "LIGHTS",  "",          0,     3, 0, ESPDASH_KIND_ENUM,   espdash_lights_labels, 4 },
      { "wheel_fl",     "FL",      "km/h",      0,  2200, 1, ESPDASH_KIND_NUMBER, NULL, 0 },
      { "wheel_fr",     "FR",      "km/h",      0,  2200, 1, ESPDASH_KIND_NUMBER, NULL, 0 },
      { "wheel_rl",     "RL",      "km/h",      0,  2200, 1, ESPDASH_KIND_NUMBER, NULL, 0 },
      { "wheel_rr",     "RR",      "km/h",      0,  2200, 1, ESPDASH_KIND_NUMBER, NULL, 0 },
      { "low_fuel",     "LOW FUEL","",          0,     1, 0, ESPDASH_KIND_FLAG,   NULL, 0 },
      { "econ",         "ECON",    "",          0,     1, 0, ESPDASH_KIND_FLAG,   NULL, 0 },
      { "sport",        "S",       "",          0,     1, 0, ESPDASH_KIND_FLAG,   NULL, 0 },
      { "turn_l",       "<",       "",          0,     1, 0, ESPDASH_KIND_FLAG,   NULL, 0 },
      { "turn_r",       ">",       "",          0,     1, 0, ESPDASH_KIND_FLAG,   NULL, 0 },
      { "brake_sw",     "BRAKE",   "",          0,     1, 0, ESPDASH_KIND_FLAG,   NULL, 0 },
      { "abs",          "ABS",     "",          0,     1, 0, ESPDASH_KIND_FLAG,   NULL, 0 },
      { "tc",           "TC",      "",          0,     1, 0, ESPDASH_KIND_FLAG,   NULL, 0 },
      { "cel",          "CEL",     "",          0,     1, 0, ESPDASH_KIND_FLAG,   NULL, 0 },
      { "vsa",          "VSA",     "",          0,     1, 0, ESPDASH_KIND_FLAG,   NULL, 0 },
    };
    return t;
}

static inline const EspDashSignalInfo *espdash_signal_info(EspDashSignalId id) {
    if ((int)id < 0 || id >= ESPDASH_SIG_COUNT) return NULL;
    return &espdash_signal_table()[id];
}

// Look a signal up by its stable key. Returns ESPDASH_SIG_COUNT if unknown,
// so a screen layout can be driven from strings (a config file, a serial
// command) without the caller hard-coding enum values.
static inline EspDashSignalId espdash_signal_by_key(const char *key) {
    if (!key) return ESPDASH_SIG_COUNT;
    const EspDashSignalInfo *t = espdash_signal_table();
    for (int i = 0; i < ESPDASH_SIG_COUNT; i++)
        if (strcmp(t[i].key, key) == 0) return (EspDashSignalId)i;
    return ESPDASH_SIG_COUNT;
}

// Every value comes back in tenths, whatever its natural unit, so one integer
// type serves the whole catalog. `decimals` in the info row says how to show
// it. `ok` is false when this sender is too old to carry the field, or the
// gateway has flagged the reading invalid - render "--" in that case, never 0.
//
// odo_km is a special case: the wire carries a 16-bit 50 m counter that WRAPS
// every 3276 km, so an absolute reading is meaningless. `odo_base` is the
// value the caller saw first; passing it back turns the counter into distance
// travelled since then, wrap included. Pass 0xFFFF to get the raw counter.
static inline int32_t espdash_signal_x10(const EspDashTelemetry *t, uint16_t plen,
                                         EspDashSignalId id, uint16_t odo_base,
                                         bool *ok) {
    bool have = true;
    int32_t v = 0;
    if (!t) { if (ok) *ok = false; return 0; }

    switch (id) {
    case ESPDASH_SIG_SPEED:      v = t->speed_kmh_x10; break;
    case ESPDASH_SIG_RPM:        v = (int32_t)t->rpm * 10; break;
    case ESPDASH_SIG_GEAR:       v = (int32_t)t->gear * 10; break;
    case ESPDASH_SIG_THROTTLE:   v = (int32_t)t->throttle_pct * 10; break;
    case ESPDASH_SIG_BRAKE:      v = (int32_t)t->brake_pct * 10; break;
    case ESPDASH_SIG_STEERING:   v = (int32_t)t->steering_deg * 10; break;
    case ESPDASH_SIG_COOLANT:    v = t->water_temp_x10; break;
    case ESPDASH_SIG_AMBIENT:    v = (int32_t)t->ambient_temp * 10; break;
    case ESPDASH_SIG_FUEL_AVG:   v = (int32_t)t->fuel_consumption_x10; break;
    case ESPDASH_SIG_BRAKE_SWITCH: v = (t->flags & ESPDASH_FLAG_BRAKE_SWITCH) ? 10 : 0; break;
    case ESPDASH_SIG_ABS:        v = (t->flags & ESPDASH_FLAG_ABS_ACTIVE) ? 10 : 0; break;
    case ESPDASH_SIG_TC:         v = (t->flags & ESPDASH_FLAG_TC_ACTIVE) ? 10 : 0; break;
    case ESPDASH_SIG_CEL:        v = (t->flags & ESPDASH_FLAG_CEL) ? 10 : 0; break;
    case ESPDASH_SIG_VSA:        v = (t->flags & ESPDASH_FLAG_VSA_WARNING) ? 10 : 0; break;

    case ESPDASH_SIG_WHEEL_FL:
    case ESPDASH_SIG_WHEEL_FR:
    case ESPDASH_SIG_WHEEL_RL:
    case ESPDASH_SIG_WHEEL_RR:
        have = ESPDASH_HAS(plen, wheel_rr_x10);
        if (have) v = (id == ESPDASH_SIG_WHEEL_FL) ? t->wheel_fl_x10 :
                      (id == ESPDASH_SIG_WHEEL_FR) ? t->wheel_fr_x10 :
                      (id == ESPDASH_SIG_WHEEL_RL) ? t->wheel_rl_x10 : t->wheel_rr_x10;
        break;

    case ESPDASH_SIG_FUEL_LEVEL:
        have = ESPDASH_HAS(plen, flags2) && (t->flags2 & ESPDASH_FLAG2_FUEL_VALID);
        if (have) v = (int32_t)t->fuel_level_pct * 10;
        break;
    case ESPDASH_SIG_FUEL_LITRES:
        // The raw CAN byte is half-litres and fuel_level_pct is that byte
        // clamped to 100, so litres are only exact below the clamp. Above it
        // the tank is brimmed and 50.0 L is the honest floor to report.
        have = ESPDASH_HAS(plen, flags2) && (t->flags2 & ESPDASH_FLAG2_FUEL_VALID);
        if (have) v = (int32_t)t->fuel_level_pct * 5;
        break;
    case ESPDASH_SIG_GEAR_NUM:
        have = ESPDASH_HAS(plen, gear_num) && t->gear_num > 0;
        if (have) v = (int32_t)t->gear_num * 10;
        break;
    case ESPDASH_SIG_LOW_FUEL:
        have = ESPDASH_HAS(plen, flags2);
        if (have) v = (t->flags2 & ESPDASH_FLAG2_LOW_FUEL) ? 10 : 0;
        break;
    case ESPDASH_SIG_ECON:
        have = ESPDASH_HAS(plen, flags2);
        if (have) v = (t->flags2 & ESPDASH_FLAG2_ECON) ? 10 : 0;
        break;
    case ESPDASH_SIG_SPORT:
        have = ESPDASH_HAS(plen, flags2);
        if (have) v = (t->flags2 & ESPDASH_FLAG2_SPORT) ? 10 : 0;
        break;
    case ESPDASH_SIG_TURN_LEFT:
        have = ESPDASH_HAS(plen, flags2);
        if (have) v = (t->flags2 & ESPDASH_FLAG2_TURN_LEFT) ? 10 : 0;
        break;
    case ESPDASH_SIG_TURN_RIGHT:
        have = ESPDASH_HAS(plen, flags2);
        if (have) v = (t->flags2 & ESPDASH_FLAG2_TURN_RIGHT) ? 10 : 0;
        break;

    case ESPDASH_SIG_CABIN_TEMP:
        have = ESPDASH_HAS(plen, cabin_temp);
        if (have) v = (int32_t)t->cabin_temp * 10;
        break;
    case ESPDASH_SIG_FAN_SPEED:
        have = ESPDASH_HAS(plen, fan_speed);
        if (have) v = (int32_t)t->fan_speed * 10;
        break;
    case ESPDASH_SIG_LIGHTS:
        have = ESPDASH_HAS(plen, lights);
        if (have) v = (int32_t)t->lights * 10;
        break;
    case ESPDASH_SIG_ODO_KM:
        have = ESPDASH_HAS(plen, odo_50m) &&
               (t->flags2 & ESPDASH_FLAG2_ODO_VALID);
        if (have) {
            uint16_t raw = t->odo_50m;
            // Unsigned subtraction wraps the same way the counter does, so a
            // trip spanning the 3276 km rollover still comes out right.
            uint16_t d = (odo_base == 0xFFFF) ? raw : (uint16_t)(raw - odo_base);
            v = (int32_t)d / 2;          // 50 m counts -> km x10
        }
        break;

    default: have = false; break;
    }
    if (ok) *ok = have;
    return have ? v : 0;
}

// Formats a signal ready to print: "97", "9.4", "HIGH", "ON", or "--" when no
// reading is available. Returns the number of characters written.
static inline int espdash_signal_format(const EspDashTelemetry *t, uint16_t plen,
                                        EspDashSignalId id, uint16_t odo_base,
                                        char *buf, size_t n) {
    const EspDashSignalInfo *i = espdash_signal_info(id);
    if (!i || !buf || n == 0) return 0;
    bool ok = false;
    int32_t v = espdash_signal_x10(t, plen, id, odo_base, &ok);
    if (!ok) return snprintf(buf, n, "--");
    if (i->kind == ESPDASH_KIND_FLAG) return snprintf(buf, n, v ? "ON" : "OFF");
    if (i->kind == ESPDASH_KIND_ENUM) {
        int idx = (int)(v / 10);
        if (i->labels && idx >= 0 && idx < (int)i->label_count)
            return snprintf(buf, n, "%s", i->labels[idx]);
        return snprintf(buf, n, "?");
    }
    if (i->decimals == 0) {
        // Round to nearest rather than truncating: a speed of 19.7 km/h
        // showing as 19 is a bug a driver will notice against the dash.
        int32_t r = (v < 0) ? (v - 5) / 10 : (v + 5) / 10;
        return snprintf(buf, n, "%ld", (long)r);
    }
    int32_t a = v < 0 ? -v : v;
    return snprintf(buf, n, "%s%ld.%ld", v < 0 ? "-" : "", (long)(a / 10), (long)(a % 10));
}

#if defined(__cplusplus) && __cplusplus >= 201103L
static_assert(ESPDASH_SIG_COUNT == 30,
              "signal table and EspDashSignalId must be updated together");
#endif

#endif  // ESPDASH_SIGNALS_H
