// =========================================================================
// espDash ESP-NOW WIRE PROTOCOL v2
// =========================================================================
// This header is the SINGLE SOURCE OF TRUTH for the ESP-NOW packet layout.
// It is included by the gateway and by every display node. Never copy this
// struct into a node - add the library instead, or the fleet will silently
// drift apart again.
//
// FORWARD/BACKWARD COMPATIBILITY RULES
// ------------------------------------
//   1. APPEND ONLY. New signals go at the END of EspDashTelemetry. Never
//      reorder, never change a field's type, never remove one (leave a hole
//      and rename it *_deprecated instead).
//   2. Receivers MUST gate on payload_len, never on `len == sizeof(...)`.
//      Use the ESPDASH_HAS() macro. That way an old node ignores trailing
//      fields from a new gateway, and a new node simply skips fields an old
//      gateway does not send. Neither goes dark.
//   3. Bump ESPDASH_PROTO_MINOR when appending fields (tolerated both ways).
//      Bump ESPDASH_PROTO_MAJOR only for a genuinely breaking change; a major
//      mismatch is rejected outright so a node fails loudly instead of
//      rendering garbage.
// =========================================================================

#ifndef ESPDASH_PROTO_H
#define ESPDASH_PROTO_H

#include <stdint.h>
#include <stddef.h>

#define ESPDASH_MAGIC       0xED
#define ESPDASH_PROTO_MAJOR 2
#define ESPDASH_PROTO_MINOR 1

// Message types. 1 is the only one implemented today; the rest are reserved so
// a future node-status or IMU packet does not need a major version bump.
enum {
    ESPDASH_MSG_TELEMETRY  = 1,
    ESPDASH_MSG_NODE_STATUS = 2,  // reserved: node -> gateway heartbeat
    ESPDASH_MSG_IMU        = 3    // reserved: GY-BNO08X orientation
};

typedef struct __attribute__((packed)) {
    uint8_t  magic;        // always ESPDASH_MAGIC (0xED)
    uint8_t  msg_type;     // ESPDASH_MSG_*
    uint8_t  proto_major;  // reject on mismatch
    uint8_t  proto_minor;  // informational; additive changes
    uint16_t payload_len;  // bytes following this header
    uint16_t seq;          // increments per packet, wraps; drop detection
} EspDashHeader;

typedef struct __attribute__((packed)) {
    // ---- v2.0 ---- APPEND ONLY BELOW, NEVER REORDER OR REMOVE ------------
    uint16_t rpm;            // 0-9000 RPM, 1 RPM resolution
    uint16_t speed_kmh_x10;  // 0-3000 = 0-300.0 km/h
    int16_t  water_temp_x10; // -400..+1500 = -40.0..+150.0 C
    int16_t  oil_temp_x10;   // -400..+1500; NOT available passively, stays 0
    uint16_t battery_mv;     // millivolts, e.g. 14200 = 14.2 V
    uint8_t  gear;           // 0=P 1=R 2=N 3=D 4=S
    uint8_t  fuel_pct;       // 0-100 %
    int16_t  steering_deg;   // signed degrees, negative = left
    int8_t   ambient_temp;   // whole degrees C
    uint8_t  flags;          // see ESPDASH_FLAG_* below
    uint8_t  throttle_pct;   // 0-100 %
    uint8_t  brake_pct;      // 0-100 % (was misnamed brake_bar in v1)
    uint32_t timestamp_ms;   // gateway uptime
    // ---- added in v2.1 --------------------------------------------------
    uint16_t wheel_fl_x10;   // per-wheel speed, 0.1 km/h
    uint16_t wheel_fr_x10;
    uint16_t wheel_rl_x10;
    uint16_t wheel_rr_x10;
} EspDashTelemetry;

// Flag bits. Keep in sync with the JSON booleans the web dashboard reads.
#define ESPDASH_FLAG_ENGINE_RUNNING 0x01
#define ESPDASH_FLAG_SHIFT_WARNING  0x02
#define ESPDASH_FLAG_OVERHEAT       0x04
#define ESPDASH_FLAG_ABS_ACTIVE     0x08  // UNMAPPED: no source ID found yet
#define ESPDASH_FLAG_TC_ACTIVE      0x10  // UNMAPPED: no source ID found yet
#define ESPDASH_FLAG_BRAKE_SWITCH   0x20
#define ESPDASH_FLAG_CEL            0x40  // UNMAPPED
#define ESPDASH_FLAG_VSA_WARNING    0x80

// True when the sender's payload was long enough to contain `field`.
// Usage: if (ESPDASH_HAS(hdr->payload_len, wheel_fl_x10)) { ... }
#define ESPDASH_HAS(payload_len, field)                              \
    ((payload_len) >= (uint16_t)(offsetof(EspDashTelemetry, field) +  \
                                 sizeof(((EspDashTelemetry *)0)->field)))

#define ESPDASH_PACKET_SIZE (sizeof(EspDashHeader) + sizeof(EspDashTelemetry))

// The v2.0 field offsets are frozen: every node in the field decodes them at
// these positions. Appending is fine and moves only the total size; anything
// that shifts an offset below is a breaking change and needs a MAJOR bump.
#if defined(__cplusplus) && __cplusplus >= 201103L
static_assert(sizeof(EspDashHeader) == 8, "EspDashHeader must stay 8 bytes");
static_assert(offsetof(EspDashTelemetry, rpm)             ==  0, "v2.0 layout frozen");
static_assert(offsetof(EspDashTelemetry, speed_kmh_x10)   ==  2, "v2.0 layout frozen");
static_assert(offsetof(EspDashTelemetry, water_temp_x10)  ==  4, "v2.0 layout frozen");
static_assert(offsetof(EspDashTelemetry, oil_temp_x10)    ==  6, "v2.0 layout frozen");
static_assert(offsetof(EspDashTelemetry, battery_mv)      ==  8, "v2.0 layout frozen");
static_assert(offsetof(EspDashTelemetry, gear)            == 10, "v2.0 layout frozen");
static_assert(offsetof(EspDashTelemetry, fuel_pct)        == 11, "v2.0 layout frozen");
static_assert(offsetof(EspDashTelemetry, steering_deg)    == 12, "v2.0 layout frozen");
static_assert(offsetof(EspDashTelemetry, ambient_temp)    == 14, "v2.0 layout frozen");
static_assert(offsetof(EspDashTelemetry, flags)           == 15, "v2.0 layout frozen");
static_assert(offsetof(EspDashTelemetry, throttle_pct)    == 16, "v2.0 layout frozen");
static_assert(offsetof(EspDashTelemetry, brake_pct)       == 17, "v2.0 layout frozen");
static_assert(offsetof(EspDashTelemetry, timestamp_ms)    == 18, "v2.0 layout frozen");
// Appended in v2.1. Bump this deliberately when you append more.
static_assert(sizeof(EspDashTelemetry) == 30, "size changed - bump PROTO_MINOR");
#endif

// Validate a received ESP-NOW buffer. Returns a pointer to the telemetry body
// on success, or NULL if the frame is not ours / not compatible. `out_len`
// receives the sender's payload_len for use with ESPDASH_HAS.
static inline const EspDashTelemetry *espdash_parse(const uint8_t *data, int len,
                                                    uint16_t *out_len, uint16_t *out_seq) {
    if (data == NULL || len < (int)sizeof(EspDashHeader)) return NULL;
    const EspDashHeader *h = (const EspDashHeader *)data;
    if (h->magic != ESPDASH_MAGIC) return NULL;
    if (h->msg_type != ESPDASH_MSG_TELEMETRY) return NULL;
    if (h->proto_major != ESPDASH_PROTO_MAJOR) return NULL;  // breaking change
    // Trust the smaller of what the sender claims and what actually arrived,
    // so a truncated frame can never make a reader run off the end.
    uint16_t avail = (uint16_t)(len - sizeof(EspDashHeader));
    uint16_t plen = h->payload_len < avail ? h->payload_len : avail;
    if (out_len) *out_len = plen;
    if (out_seq) *out_seq = h->seq;
    return (const EspDashTelemetry *)(data + sizeof(EspDashHeader));
}

#endif  // ESPDASH_PROTO_H
