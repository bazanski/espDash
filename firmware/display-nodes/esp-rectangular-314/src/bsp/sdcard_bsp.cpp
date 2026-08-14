#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <Arduino.h>

#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "driver/gpio.h"
#include "ff.h"

#include "sdcard_bsp.h"

// Pin assignment taken from the Waveshare vendor example for this exact
// board (examples/.../04_SD_Card/sdcard_bsp.cpp) - not guessed.
#define SDMMC_CLK_PIN (gpio_num_t)1
#define SDMMC_CMD_PIN (gpio_num_t)2
#define SDMMC_D0_PIN  (gpio_num_t)42

static sdmmc_card_t *s_card = NULL;

esp_err_t sdcard_init(void) {
    if (s_card) return ESP_OK;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {};
    mount_config.format_if_mount_failed = false;  // never reformat a user's card
    mount_config.max_files              = 4;
    // 16 KB allocation unit: we write in large sequential blocks, so a bigger
    // cluster means fewer FAT updates and fewer of the write stalls that this
    // whole design is built to absorb.
    mount_config.allocation_unit_size   = 16 * 1024;

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;
    // 1-bit mode: only D0 is wired on this board. ~1-2 MB/s, against a
    // measured need of ~15 KB/s - roughly 70x headroom, so the bottleneck is
    // write latency, not bandwidth.
    host.flags = SDMMC_HOST_FLAG_1BIT;

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 1;
    slot_config.clk   = SDMMC_CLK_PIN;
    slot_config.cmd   = SDMMC_CMD_PIN;
    slot_config.d0    = SDMMC_D0_PIN;

    esp_err_t err = esp_vfs_fat_sdmmc_mount(SDCARD_MOUNT_POINT, &host, &slot_config,
                                            &mount_config, &s_card);
    if (err != ESP_OK) {
        s_card = NULL;
        Serial.printf("[SD] mount failed: %s\n", esp_err_to_name(err));
        return err;
    }

    Serial.printf("[SD] mounted: %s, %lu MB\n",
                  s_card->cid.name, (unsigned long)sdcard_capacity_mb());
    return ESP_OK;
}

bool sdcard_is_mounted(void) {
    return s_card != NULL;
}

void sdcard_unmount(void) {
    if (!s_card) return;
    esp_vfs_fat_sdcard_unmount(SDCARD_MOUNT_POINT, s_card);
    s_card = NULL;
    Serial.println("[SD] unmounted");
}

uint32_t sdcard_capacity_mb(void) {
    if (!s_card) return 0;
    // capacity is in 512-byte sectors
    return (uint32_t)(((uint64_t)s_card->csd.capacity * 512ULL) / (1024ULL * 1024ULL));
}

uint32_t sdcard_free_mb(void) {
    if (!s_card) return 0;
    // FATFS directly rather than statvfs, which this IDF version's newlib
    // does not provide. f_getfree walks the FAT, so this is slow (tens of
    // ms) - call it about once a second, never per write.
    FATFS *fs;
    DWORD free_clusters;
    if (f_getfree("0:", &free_clusters, &fs) != FR_OK) return 0;
    uint64_t free_sectors = (uint64_t)free_clusters * fs->csize;
    return (uint32_t)((free_sectors * 512ULL) / (1024ULL * 1024ULL));
}
