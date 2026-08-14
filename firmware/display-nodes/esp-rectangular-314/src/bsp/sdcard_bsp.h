#ifndef SDCARD_BSP_H
#define SDCARD_BSP_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// SD card on the Waveshare ESP32-S3-LCD-3.16 is wired for SDMMC 1-bit:
//   CLK = GPIO1, CMD = GPIO2, D0 = GPIO42
// GPIO1 and GPIO2 are shared with the ST7701 panel's 3-wire init SPI
// (SCK/SDO). The board is designed for that handoff - the panel only needs
// them during init - so sdcard_init() MUST be called after
// release_st7701_spi_pins(), or the mount fails with the pins still held.
#define SDCARD_MOUNT_POINT "/sdcard"

// Minimum free space required to begin a recording. At the measured ~57 MB
// per hour of driving this is roughly an hour of headroom; starting a drive
// on a nearly-full card and failing 10 minutes in is worse than refusing up
// front, where it can still be fixed.
#define SDCARD_MIN_FREE_MB 64

// Why a recording cannot start / why writing stopped. The distinction
// matters because these are diagnosed in a car with no serial monitor
// attached - "SD ERROR" alone leaves you guessing between a missing card, an
// exFAT card that needs reformatting, and a full one.
typedef enum {
    SD_STATUS_OK = 0,
    SD_STATUS_NO_CARD,     // card init failed: absent, unseated, or wiring
    SD_STATUS_BAD_FORMAT,  // card present but no mountable FAT (exFAT? blank?)
    SD_STATUS_FULL,        // mounted, but less than SDCARD_MIN_FREE_MB left
    SD_STATUS_WRITE_FAIL   // mounted and started, then a write failed
} SdStatus;

#ifdef __cplusplus
extern "C" {
#endif

// Mounts the card. Returns ESP_OK on success. Safe to call when no card is
// inserted - it fails cleanly rather than blocking or aborting.
esp_err_t sdcard_init(void);

bool     sdcard_is_mounted(void);
void     sdcard_unmount(void);

SdStatus    sdcard_status(void);
const char *sdcard_status_str(void);      // short, display-sized text
void        sdcard_set_status(SdStatus s);

// Writes, reads back and deletes a small file. Mounting only proves the FAT
// is readable; counterfeit and failing cards routinely mount and then throw
// away writes. Catching that at boot beats discovering it after a drive.
bool sdcard_selftest(void);

// Capacity in MB, 0 if not mounted.
uint32_t sdcard_capacity_mb(void);

// Free space in MB, 0 if not mounted. Uses statvfs, which walks the FAT and
// is slow (tens of ms) - call it once per second at most, never per write.
uint32_t sdcard_free_mb(void);

#ifdef __cplusplus
}
#endif

#endif  // SDCARD_BSP_H
