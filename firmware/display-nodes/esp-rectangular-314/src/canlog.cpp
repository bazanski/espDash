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
// If no canlog packet arrives for this long while recording, the gateway is
// gone (ignition off, out of range, rebooted). Auto-stop so the file is
// closed cleanly instead of being left open. The gateway sends an id-table
// heartbeat every 2 s even on a totally silent CAN bus, so this only trips
// when the gateway itself has actually stopped - not merely when the car is
// parked with the engine off.
#define RX_TIMEOUT_MS     8000

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
static uint16_t s_file_index = 0;

static volatile uint32_t s_frames = 0;
static volatile uint32_t s_bytes = 0;
static volatile uint32_t s_dropped = 0;
static volatile uint32_t s_gw_dropped = 0;
static volatile uint16_t s_seq_gaps = 0;
static uint16_t s_last_seq = 0;
static bool     s_have_seq = false;
static uint32_t s_start_ms = 0;
static volatile uint32_t s_last_rx_ms = 0;
static volatile uint32_t s_pkts_rx = 0;

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
    uint16_t plen = 0, seq = 0;
    const EspDashCanLogHdr *ch = espdash_parse_canlog(data, len, &plen, &seq);

    // Count first, gate second: observing the link while idle is the only way
    // to tell "gateway not streaming" from "node not recording".
    if (ch) s_pkts_rx++;
    if (s_state != CANLOG_RECORDING) return;

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
        s_last_rx_ms = millis();
        ring_write_record(REC_BATCH, (const uint8_t *)ch, plen);
        return;
    }

    uint16_t ilen = 0;
    const EspDashCanLogIdsHdr *ih = espdash_parse_canlog_ids(data, len, &ilen);
    if (ih) {
        s_last_rx_ms = millis();
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

// Set only by canlog_stop(), between "stop accepting new frames" and "close
// the file". The writer task must keep consuming during that window: without
// it, canlog_stop() told the writer to stand down and then waited 5 s for a
// drain that could never happen, silently discarding whatever was still in
// the ring - the tail of every single recording.
static volatile bool s_draining = false;

static void sdWriteTask(void *arg) {
    (void)arg;
    static uint8_t block[WRITE_BLOCK];
    uint32_t last_sync = 0;

    for (;;) {
        if ((s_state != CANLOG_RECORDING && !s_draining) || !s_file) {
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
            // Most likely the card was pulled mid-recording, or it is failing.
            Serial.println("[CANLOG] write failed - stopping");
            sdcard_set_status(SD_STATUS_WRITE_FAIL);
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

    if (sdcard_init() == ESP_OK) {
        // Prove the card actually retains data before trusting a drive to it.
        sdcard_selftest();
    } else {
        // Report what is actually on the card rather than leaving the user to
        // guess which of several formatting problems it is.
        sdcard_diagnose();
    }

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
        if (stat(out, &st) != 0) {
            s_file_index = (uint16_t)i;
            return true;
        }
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
    // Refuse up front rather than dying mid-drive: at ~57 MB/hr a nearly
    // full card would fail somewhere down the road, where it cannot be fixed.
    uint32_t freemb = sdcard_free_mb_refresh();   // must be accurate, not cached
    if (freemb < SDCARD_MIN_FREE_MB) {
        Serial.printf("[CANLOG] only %lu MB free, need %u - refusing to start\n",
                      (unsigned long)freemb, (unsigned)SDCARD_MIN_FREE_MB);
        sdcard_set_status(SD_STATUS_FULL);
        s_state = CANLOG_ERROR;
        return false;
    }

    if (!next_filename(s_filename, sizeof(s_filename))) {
        s_state = CANLOG_ERROR;
        return false;
    }
    s_file = fopen(s_filename, "wb");
    if (!s_file) {
        Serial.printf("[CANLOG] fopen failed: %s\n", s_filename);
        sdcard_set_status(SD_STATUS_WRITE_FAIL);
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
    s_last_rx_ms = s_start_ms;
    sdcard_set_status(SD_STATUS_OK);   // clear any stale error from a past attempt
    s_state = CANLOG_RECORDING;

    Serial.printf("[CANLOG] recording -> %s\n", s_filename);
    return true;
}

void canlog_stop(void) {
    if (s_state != CANLOG_RECORDING) {
        s_state = CANLOG_IDLE;
        return;
    }
    // Order matters. IDLE first, so canlog_on_packet() stops appending new
    // frames and the ring can actually reach empty; s_draining so the writer
    // keeps consuming what is already queued.
    s_state = CANLOG_IDLE;
    s_draining = true;

    // Drain whatever is still buffered before closing, so the tail of the
    // capture is not lost. Normally finishes in well under 100 ms (a full
    // 256 KB ring at ~1 MB/s is ~0.25 s); the guard is a backstop for a card
    // that has stopped responding, not the expected path.
    uint32_t guard = 0;
    while (ring_used() > 0 && guard++ < 500) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (ring_used() > 0) {
        Serial.printf("[CANLOG] WARNING: %lu bytes undrained at stop - card stalled\n",
                      (unsigned long)ring_used());
    }
    s_draining = false;
    bool empty = (s_frames == 0);
    if (s_file) {
        fflush(s_file);
        fsync(fileno(s_file));
        fclose(s_file);
        s_file = NULL;
    }
    // A recording that captured nothing is not worth keeping: it happens
    // whenever the button is pressed while the gateway is off or out of
    // range, and leaving those behind fills the card with junk and makes the
    // numbering useless for finding the drive you actually care about.
    if (empty) {
        remove(s_filename);
        Serial.printf("[CANLOG] %s captured 0 frames - deleted\n", s_filename);
        s_state = CANLOG_IDLE;
        return;
    }
    // Append to a manifest so several recordings from one session stay
    // tellable apart without opening each file. No RTC on this board, so
    // the gateway uptime at start is the closest thing to a wall clock -
    // it at least orders them and shows how far into the drive each began.
    FILE *mf = fopen(SDCARD_MOUNT_POINT "/canlog_index.csv", "a");
    if (mf) {
        // Header only once, when the file is new (ftell==0 after append open).
        if (ftell(mf) == 0) {
            fprintf(mf, "file,start_uptime_ms,duration_s,frames,bytes,node_dropped,gw_dropped,seq_gaps\n");
        }
        fprintf(mf, "canlog_%04u.bin,%lu,%lu,%lu,%lu,%lu,%lu,%u\n",
                (unsigned)s_file_index,
                (unsigned long)s_start_ms,
                (unsigned long)((millis() - s_start_ms) / 1000),
                (unsigned long)s_frames,
                (unsigned long)s_bytes,
                (unsigned long)s_dropped,
                (unsigned long)s_gw_dropped,
                (unsigned)s_seq_gaps);
        fclose(mf);
    }

    Serial.printf("[CANLOG] stopped: %s, %lu frames, %lu KB, dropped %lu, gaps %u\n",
                  s_filename, (unsigned long)s_frames,
                  (unsigned long)(s_bytes / 1024),
                  (unsigned long)s_dropped, (unsigned)s_seq_gaps);
}

void canlog_tick(uint32_t now) {
    if (s_state != CANLOG_RECORDING) return;
    // Signed compare, deliberately. loop() samples `now` once at the top, but
    // canlog_start() stamps s_last_rx_ms from a later millis() inside the same
    // pass - so s_last_rx_ms is legitimately a few ms AHEAD of `now` on the
    // first tick of a recording. Unsigned arithmetic turned that into ~4.29e9
    // and auto-stopped the recording microseconds after it began. Signed also
    // keeps the 49-day millis() rollover correct.
    int32_t idle = (int32_t)(now - s_last_rx_ms);
    if (idle > (int32_t)RX_TIMEOUT_MS) {
        Serial.println("[CANLOG] gateway silent - auto-stopping, file closed cleanly");
        canlog_stop();
    }
}

// Nothing buffered and no file open => every byte is on the card.
bool canlog_safe_to_remove(void) {
    return s_state != CANLOG_RECORDING && s_file == NULL && ring_used() == 0;
}

CanLogState canlog_state(void)        { return s_state; }
uint16_t    canlog_file_index(void)    { return s_file_index; }
uint32_t    canlog_frames_written(void){ return s_frames; }
uint32_t    canlog_bytes_written(void) { return s_bytes; }
uint32_t    canlog_elapsed_ms(void)    { return s_state == CANLOG_RECORDING ? millis() - s_start_ms : 0; }
uint32_t    canlog_dropped(void)       { return s_dropped; }
uint32_t    canlog_gw_dropped(void)    { return s_gw_dropped; }
uint16_t    canlog_seq_gaps(void)      { return s_seq_gaps; }
uint32_t    canlog_packets_rx(void)    { return s_pkts_rx; }
const char *canlog_filename(void)      { return s_filename; }
