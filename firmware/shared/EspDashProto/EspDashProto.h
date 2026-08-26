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

// Fixed 2.4 GHz channel for ESP-NOW when a device never associates to Wi-Fi.
// Only channels 1/6/11 are non-overlapping (WiFi channels are ~22 MHz wide on
// a 5 MHz grid, so anything in between catches interference splash from two
// neighbors at once). 1 is used because it's what every device in this
// project already defaulted to and it's the value proven on the bench
// (100% delivery, 0 gaps, 20 Hz over 244+ packets) - switch to 6 or 11 here,
// in one place, if a specific location ever shows contention on 1.
#define ESPDASH_ESPNOW_CHANNEL 1

// Message types. 1 and 4 are implemented; 2 and 3 are reserved so a future
// node-status or IMU packet does not need a major version bump.
enum {
    ESPDASH_MSG_TELEMETRY   = 1,
    ESPDASH_MSG_NODE_STATUS = 2,  // reserved: node -> gateway heartbeat
    ESPDASH_MSG_IMU         = 3,  // reserved: GY-BNO08X orientation
    ESPDASH_MSG_CANLOG      = 4,  // batched raw CAN frames -> SD recorder
    ESPDASH_MSG_CANLOG_IDS  = 5,  // ID index table (makes a log self-describing)
    ESPDASH_MSG_NODE_CMD    = 6   // node -> gateway request (first upstream msg)
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
    uint8_t  fuel_consumption_x10; // instant consumption, L/100km x10 (94=9.4).
                                    // Renamed from fuel_pct 2026-08-15: that
                                    // field was fuel LEVEL % (d[1]/2), which
                                    // was wrong (see CAN_PROTOCOL_MAP.md).
                                    // Same byte, same offset, corrected value
                                    // and meaning. Tank level is unmapped.
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
static_assert(offsetof(EspDashTelemetry, fuel_consumption_x10) == 11, "v2.0 layout frozen");
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

// =========================================================================
// RAW CAN LOG STREAM (msg_type 4/5)
// =========================================================================
// Streams raw CAN frames from the gateway to a node with an SD card, so a
// drive can be captured without a laptop tethered over USB. Measured need on
// this car: 1399 frames/s mean, 45 unique IDs, avg DLC 6.63 -> ~14.5 KB/s
// with the variable-length encoding below, about 78 ESP-NOW packets/s.
//
// Frames are packed variable-length rather than fixed 13 bytes because the
// DLC distribution is uneven (avg 6.63, not 8); that alone saves ~18% of
// airtime, which matters when this shares a radio with 20 Hz telemetry.

#define ESPDASH_CANLOG_MAX_FRAMES 18   // fits in 250 B with worst-case DLC=8
#define ESPDASH_CANLOG_ID_ESCAPE  0xFF // id_idx value meaning "raw 11-bit id follows"

typedef struct __attribute__((packed)) {
    uint32_t base_ms;     // gateway millis() of the first frame in this batch
    uint8_t  count;       // number of frames packed after this header
    uint8_t  flags;       // reserved for future use
    uint16_t gw_dropped;  // running gateway-side drop count at time of send
} EspDashCanLogHdr;

// Each frame follows as:
//   uint8_t  id_idx       index into the ID table, or ESPDASH_CANLOG_ID_ESCAPE
//   uint8_t  dlc          low nibble = DLC (0-8), high nibble reserved
//   uint16_t ts_delta_ms  offset from base_ms
//   [uint16_t raw_id]     present ONLY when id_idx == ESPDASH_CANLOG_ID_ESCAPE
//   uint8_t  data[DLC]

// ID index table, sent periodically as msg_type 5 so a capture that starts
// mid-stream is still decodable on its own.
typedef struct __attribute__((packed)) {
    uint8_t  count;       // number of ids that follow
    uint8_t  reserved;
    // uint16_t ids[count]
} EspDashCanLogIdsHdr;

#if defined(__cplusplus) && __cplusplus >= 201103L
static_assert(sizeof(EspDashCanLogHdr) == 8, "canlog header must stay 8 bytes");
#endif

// Deliberately a SEPARATE entry point from espdash_parse(): that function
// hard-rejects any msg_type other than telemetry, and it must keep doing so.
// Relaxing it would let a display node interpret a log batch as telemetry and
// render garbage. Nodes that do not record simply never call this.
static inline const EspDashCanLogHdr *espdash_parse_canlog(const uint8_t *data, int len,
                                                           uint16_t *out_len,
                                                           uint16_t *out_seq) {
    if (data == NULL || len < (int)(sizeof(EspDashHeader) + sizeof(EspDashCanLogHdr))) return NULL;
    const EspDashHeader *h = (const EspDashHeader *)data;
    if (h->magic != ESPDASH_MAGIC) return NULL;
    if (h->msg_type != ESPDASH_MSG_CANLOG) return NULL;
    if (h->proto_major != ESPDASH_PROTO_MAJOR) return NULL;
    uint16_t avail = (uint16_t)(len - sizeof(EspDashHeader));
    uint16_t plen = h->payload_len < avail ? h->payload_len : avail;
    if (out_len) *out_len = plen;
    if (out_seq) *out_seq = h->seq;
    return (const EspDashCanLogHdr *)(data + sizeof(EspDashHeader));
}

// =========================================================================
// NODE -> GATEWAY COMMAND (msg_type 6)
// =========================================================================
// The first upstream message in this project; everything else flows gateway
// -> nodes. It exists so the SD recorder can arm the gateway by itself, with
// no laptop in the car.
//
// Deliberately a REPEATED STATE ADVERTISEMENT, not an edge-triggered
// on/off command. ESP-NOW is fire-and-forget: a single dropped "start" would
// mean recording an empty file, and a dropped "stop" would leave the gateway
// transmitting forever. Instead the node repeats what it *wants* a few times
// a second, and the gateway holds that state only while requests keep
// arriving (see ESPDASH_NODE_CMD_TIMEOUT_MS). That makes the link
// self-healing in every direction:
//   - lost packet          -> next one lands ~500 ms later
//   - node unplugged/off   -> gateway stops on its own, no wasted airtime
//   - gateway rebooted     -> node's next advert re-arms it automatically
#define ESPDASH_NODE_CMD_INTERVAL_MS 500   // node re-advertises this often
#define ESPDASH_NODE_CMD_TIMEOUT_MS  3000  // gateway forgets after this long

typedef struct __attribute__((packed)) {
    uint8_t  want_canlog;  // 1 = please stream raw CAN, 0 = stop
    uint8_t  node_id;      // which node is asking (room for several later)
    uint16_t reserved;
} EspDashNodeCmd;

#if defined(__cplusplus) && __cplusplus >= 201103L
static_assert(sizeof(EspDashNodeCmd) == 4, "node cmd must stay 4 bytes");
#endif

static inline const EspDashNodeCmd *espdash_parse_nodecmd(const uint8_t *data, int len) {
    if (data == NULL || len < (int)(sizeof(EspDashHeader) + sizeof(EspDashNodeCmd))) return NULL;
    const EspDashHeader *h = (const EspDashHeader *)data;
    if (h->magic != ESPDASH_MAGIC) return NULL;
    if (h->msg_type != ESPDASH_MSG_NODE_CMD) return NULL;
    if (h->proto_major != ESPDASH_PROTO_MAJOR) return NULL;
    return (const EspDashNodeCmd *)(data + sizeof(EspDashHeader));
}

// Same shape, for the ID table message.
static inline const EspDashCanLogIdsHdr *espdash_parse_canlog_ids(const uint8_t *data, int len,
                                                                  uint16_t *out_len) {
    if (data == NULL || len < (int)(sizeof(EspDashHeader) + sizeof(EspDashCanLogIdsHdr))) return NULL;
    const EspDashHeader *h = (const EspDashHeader *)data;
    if (h->magic != ESPDASH_MAGIC) return NULL;
    if (h->msg_type != ESPDASH_MSG_CANLOG_IDS) return NULL;
    if (h->proto_major != ESPDASH_PROTO_MAJOR) return NULL;
    uint16_t avail = (uint16_t)(len - sizeof(EspDashHeader));
    uint16_t plen = h->payload_len < avail ? h->payload_len : avail;
    if (out_len) *out_len = plen;
    return (const EspDashCanLogIdsHdr *)(data + sizeof(EspDashHeader));
}

#endif  // ESPDASH_PROTO_H
