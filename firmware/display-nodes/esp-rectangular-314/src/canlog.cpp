#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"

#include "EspDashProto.h"
#include "bsp/sdcard_bsp.h"
#include "canlog.h"

// =========================================================================
// Tunables
// =========================================================================
// 256 KB in PSRAM. At the measured ~15 KB/s payload rate this is >15 s of
// buffer - far more than any realistic SD stall (50-250 ms typical, rare
// multi-second worst cases). PSRAM is 8 MB here so this costs nothing that
// matters, and being generous is what makes the recorder trustworthy.
#define RING_BYTES        (256 * 1024)
#define WRITE_BLOCK       (8 * 1024)   // fwrite granularity
#define SYNC_INTERVAL_MS  2000         // fsync cadence

// File format:
//   magic "ESPDASHCANLOG\0" + u8 version + u16 reserved
//   then a stream of records, each:
//     u8 type  (1 = frame batch, 2 = id table)
//     u16 len
//     payload
// Keeping the on-card format record-oriented (rather than a bare frame dump)
// means a truncated file from a power cut is still parseable up to the last
// complete record.
#define CANLOG_FILE_MAGIC "ESPDASHCANLOG"
#define CANLOG_FILE_VERSION 1
#define REC_BATCH 1
#define REC_IDS   2

// =========================================================================
// PSRAM ring buffer
// =========================================================================
static uint8_t *s_ring = NULL;
static volatile uint32_t s_head = 0;   // written by ISR/callback context
static volatile uint32_t s_tail = 0;   // written by writer task
static portMUX_TYPE s_ring_mux = portMUX_INITIALIZER_UNLOCKED;

static volatile CanLogState s_state = CANLOG_IDLE;
static FILE *s_file = NULL;
static char  s_filename[48] = {0};

static volatile uint32_t s_frames = 0;
static volatile uint32_t s_bytes = 0;
static volatile uint32_t s_dropped = 0;
static volatile uint32_t s_gw_dropped = 0;
static volatile uint16_t s_seq_gaps = 0;
static uint16_t s_last_seq = 0;
static bool     s_have_seq = false;
static uint32_t s_start_ms = 0;

static inline uint32_t ring_used(void) {
    uint32_t h = s_head, t = s_tail;
    return (h >= t) ? (h - t) : (RING_BYTES - t + h);
}

static inline uint32_t ring_free(void) {
    return RING_BYTES - ring_used() - 1;
}

// Append one record. Called from the ESP-NOW callback - must not block.
static void ring_write_record(uint8_t type, const uint8_t *payload, uint16_t len) {
    uint32_t need = 3 + len;

    portENTER_CRITICAL(&s_ring_mux);
    if (ring_free() < need) {
        // Drop the incoming record rather than overwriting unread data: with
        // a log, the oldest bytes are the ones already committed to a
        // coherent file, so discarding the newest keeps the file valid and
        // makes the loss explicit via the counter.
        s_dropped++;
        portEXIT_CRITICAL(&s_ring_mux);
        return;
    }
    uint32_t h = s_head;
    uint8_t hdr[3] = { type, (uint8_t)(len & 0xFF), (uint8_t)(len >> 8) };
    for (int i = 0; i < 3; i++) {
        s_ring[h] = hdr[i];
        h = (h + 1) % RING_BYTES;
    }
    // memcpy in at most two spans (wrap)
    uint32_t first = RING_BYTES - h;
    if (first > len) first = len;
    memcpy(s_ring + h, payload, first);
    if (len > first) memcpy(s_ring, payload + first, len - first);
    h = (h + len) % RING_BYTES;
    s_head = h;
    portEXIT_CRITICAL(&s_ring_mux);
}

// =========================================================================
// ESP-NOW receive
// =========================================================================
void canlog_on_packet(const uint8_t *data, int len) {
    if (s_state != CANLOG_RECORDING) return;

    uint16_t plen = 0, seq = 0;
    const EspDashCanLogHdr *ch = espdash_parse_canlog(data, len, &plen, &seq);
    if (ch) {
        if (s_have_seq && seq != (uint16_t)(s_last_seq + 1)) {
            // Count actual packets missed, not just discontinuity events.
            uint16_t missed = (uint16_t)(seq - s_last_seq - 1);
            if (missed && missed < 1000) s_seq_gaps += missed;
        }
        s_last_seq = seq;
        s_have_seq = true;
        s_gw_dropped = ch->gw_dropped;
        s_frames += ch->count;
        ring_write_record(REC_BATCH, (const uint8_t *)ch, plen);
        return;
    }

    uint16_t ilen = 0;
    const EspDashCanLogIdsHdr *ih = espdash_parse_canlog_ids(data, len, &ilen);
    if (ih) {
        ring_write_record(REC_IDS, (const uint8_t *)ih, ilen);
    }
}

// =========================================================================
// SD writer task
// =========================================================================
static void write_file_header(void) {
    uint8_t hdr[16] = {0};
    memcpy(hdr, CANLOG_FILE_MAGIC, sizeof(CANLOG_FILE_MAGIC) - 1);
    hdr[13] = CANLOG_FILE_VERSION;
    fwrite(hdr, 1, sizeof(hdr), s_file);
}

static void sdWriteTask(void *arg) {
    (void)arg;
    static uint8_t block[WRITE_BLOCK];
    uint32_t last_sync = 0;

    for (;;) {
        if (s_state != CANLOG_RECORDING || !s_file) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        uint32_t used = ring_used();
        if (used == 0) {
            uint32_t now = millis();
            if (now - last_sync >= SYNC_INTERVAL_MS && s_file) {
                // Periodic flush so an ignition cut loses seconds, not the
                // whole file.
                fflush(s_file);
                fsync(fileno(s_file));
                last_sync = now;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        uint32_t n = used > WRITE_BLOCK ? WRITE_BLOCK : used;
        uint32_t t = s_tail;
        uint32_t first = RING_BYTES - t;
        if (first > n) first = n;
        memcpy(block, s_ring + t, first);
        if (n > first) memcpy(block + first, s_ring, n - first);

        size_t wrote = fwrite(block, 1, n, s_file);
        if (wrote != n) {
            Serial.println("[CANLOG] write failed - stopping");
            s_state = CANLOG_ERROR;
            continue;
        }
        s_bytes += n;

        portENTER_CRITICAL(&s_ring_mux);
        s_tail = (t + n) % RING_BYTES;
        portEXIT_CRITICAL(&s_ring_mux);

        uint32_t now = millis();
        if (now - last_sync >= SYNC_INTERVAL_MS) {
            fflush(s_file);
            fsync(fileno(s_file));
            last_sync = now;
        }
    }
}

// =========================================================================
// Public API
// =========================================================================
void canlog_init(void) {
    s_ring = (uint8_t *)heap_caps_malloc(RING_BYTES, MALLOC_CAP_SPIRAM);
    if (!s_ring) {
        // Fall back to internal RAM at a much smaller size rather than
        // failing outright - a short buffer still records, just with less
        // tolerance for SD stalls.
        s_ring = (uint8_t *)malloc(32 * 1024);
        Serial.println("[CANLOG] PSRAM alloc failed, using 32KB internal buffer");
    }

    sdcard_init();

    xTaskCreatePinnedToCore(sdWriteTask, "sdWrite", 4096, NULL, 2, NULL, 0);
    Serial.printf("[CANLOG] ready (ring %u KB, SD %s)\n",
                  (unsigned)(RING_BYTES / 1024),
                  sdcard_is_mounted() ? "mounted" : "ABSENT");
}

static bool next_filename(char *out, size_t outsz) {
    // Scan for the first unused index. Simple and RTC-free; at a few files
    // per drive this stays fast for a long time.
    for (int i = 0; i < 9999; i++) {
        snprintf(out, outsz, "%s/canlog_%04d.bin", SDCARD_MOUNT_POINT, i);
        struct stat st;
        if (stat(out, &st) != 0) return true;
    }
    return false;
}

bool canlog_start(void) {
    if (s_state == CANLOG_RECORDING) return true;

    if (!sdcard_is_mounted()) {
        if (sdcard_init() != ESP_OK) {
            s_state = CANLOG_ERROR;
            return false;
        }
    }
    if (!next_filename(s_filename, sizeof(s_filename))) {
        s_state = CANLOG_ERROR;
        return false;
    }
    s_file = fopen(s_filename, "wb");
    if (!s_file) {
        Serial.printf("[CANLOG] fopen failed: %s\n", s_filename);
        s_state = CANLOG_ERROR;
        return false;
    }
    // Bigger stdio buffer = fewer, larger physical writes.
    setvbuf(s_file, NULL, _IOFBF, WRITE_BLOCK);
    write_file_header();

    s_head = s_tail = 0;
    s_frames = s_bytes = s_dropped = s_gw_dropped = 0;
    s_seq_gaps = 0;
    s_have_seq = false;
    s_start_ms = millis();
    s_state = CANLOG_RECORDING;

    Serial.printf("[CANLOG] recording -> %s\n", s_filename);
    return true;
}

void canlog_stop(void) {
    if (s_state != CANLOG_RECORDING) {
        s_state = CANLOG_IDLE;
        return;
    }
    s_state = CANLOG_IDLE;   // stops the writer task from consuming further

    // Drain whatever is still buffered before closing, so the tail of the
    // capture is not lost.
    uint32_t guard = 0;
    while (ring_used() > 0 && guard++ < 500) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (s_file) {
        fflush(s_file);
        fsync(fileno(s_file));
        fclose(s_file);
        s_file = NULL;
    }
    Serial.printf("[CANLOG] stopped: %s, %lu frames, %lu KB, dropped %lu, gaps %u\n",
                  s_filename, (unsigned long)s_frames,
                  (unsigned long)(s_bytes / 1024),
                  (unsigned long)s_dropped, (unsigned)s_seq_gaps);
}

CanLogState canlog_state(void)        { return s_state; }
uint32_t    canlog_frames_written(void){ return s_frames; }
uint32_t    canlog_bytes_written(void) { return s_bytes; }
uint32_t    canlog_elapsed_ms(void)    { return s_state == CANLOG_RECORDING ? millis() - s_start_ms : 0; }
uint32_t    canlog_dropped(void)       { return s_dropped; }
uint32_t    canlog_gw_dropped(void)    { return s_gw_dropped; }
uint16_t    canlog_seq_gaps(void)      { return s_seq_gaps; }
const char *canlog_filename(void)      { return s_filename; }
