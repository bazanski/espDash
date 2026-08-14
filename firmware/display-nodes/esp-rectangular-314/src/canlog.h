#ifndef CANLOG_H
#define CANLOG_H

#include <stdint.h>
#include <stdbool.h>

// =========================================================================
// CAN LOG RECORDER - receives batched raw CAN over ESP-NOW, writes to SD
// =========================================================================
// Why binary and not CSV: at the measured 1399 frames/s, formatting text on
// the node would burn CPU and roughly triple the bytes written for no gain.
// tools/canlog_decode.py converts the .bin back to the CSV schema the
// existing analysis scripts already consume, so nothing downstream changes.
//
// Threading: canlog_on_packet() runs in the ESP-NOW receive callback (Wi-Fi
// task context) and must never block - it only memcpy's into a PSRAM ring
// and returns. A separate low-priority task drains that ring to the card.
// That decoupling is what absorbs SD write stalls (cards routinely pause
// 50-250 ms for internal block erase); the ring is sized for >10 s of stall.

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CANLOG_IDLE = 0,      // not recording
    CANLOG_RECORDING,     // actively writing
    CANLOG_ERROR          // no card, open failed, or ring overflowed
} CanLogState;

// Call once in setup(), AFTER release_st7701_spi_pins() (the SD shares
// GPIO1/2 with the panel's init SPI).
void canlog_init(void);

// Feed a raw ESP-NOW packet. Ignores anything that is not a canlog message,
// so it is safe to call for every received packet.
void canlog_on_packet(const uint8_t *data, int len);

bool canlog_start(void);   // opens a new file; false if SD unavailable
void canlog_stop(void);    // flushes, closes, returns to IDLE

CanLogState canlog_state(void);
uint32_t    canlog_frames_written(void);
uint32_t    canlog_bytes_written(void);
uint32_t    canlog_elapsed_ms(void);
uint32_t    canlog_dropped(void);      // ring overflows on this node
uint32_t    canlog_gw_dropped(void);   // drops reported by the gateway
uint16_t    canlog_seq_gaps(void);     // ESP-NOW packets lost in transit
const char *canlog_filename(void);

#ifdef __cplusplus
}
#endif

#endif  // CANLOG_H
