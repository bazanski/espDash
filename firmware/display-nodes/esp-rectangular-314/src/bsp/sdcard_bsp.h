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

#ifdef __cplusplus
extern "C" {
#endif

// Mounts the card. Returns ESP_OK on success. Safe to call when no card is
// inserted - it fails cleanly rather than blocking or aborting.
esp_err_t sdcard_init(void);

bool     sdcard_is_mounted(void);
void     sdcard_unmount(void);

// Capacity in MB, 0 if not mounted.
uint32_t sdcard_capacity_mb(void);

// Free space in MB, 0 if not mounted. Uses statvfs, which walks the FAT and
// is slow (tens of ms) - call it once per second at most, never per write.
uint32_t sdcard_free_mb(void);

#ifdef __cplusplus
}
#endif

#endif  // SDCARD_BSP_H
