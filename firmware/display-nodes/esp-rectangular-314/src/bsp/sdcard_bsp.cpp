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
static SdStatus      s_status = SD_STATUS_NO_CARD;

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
        // esp_vfs_fat_sdmmc_mount returns ESP_FAIL specifically when the card
        // talked to us but held no mountable FAT volume, and a transport-level
        // error (timeout / not found) when card init itself failed. That
        // distinction is the difference between "insert a card" and "this card
        // needs reformatting to FAT32", which is worth surfacing: any card
        // larger than 32 GB is exFAT out of the box and will land here.
        s_status = (err == ESP_FAIL) ? SD_STATUS_BAD_FORMAT : SD_STATUS_NO_CARD;
        Serial.printf("[SD] mount failed: %s (%s)\n",
                      esp_err_to_name(err), sdcard_status_str());
        if (s_status == SD_STATUS_BAD_FORMAT) {
            Serial.println("[SD] card detected but no FAT volume - reformat as "
                           "FAT32 (exFAT and unformatted cards are not supported)");
        }
        return err;
    }

    s_status = SD_STATUS_OK;
    Serial.printf("[SD] mounted: %s, %lu MB total, %lu MB free\n",
                  s_card->cid.name,
                  (unsigned long)sdcard_capacity_mb(),
                  (unsigned long)sdcard_free_mb());
    return ESP_OK;
}

SdStatus sdcard_status(void) { return s_status; }

void sdcard_set_status(SdStatus s) { s_status = s; }

const char *sdcard_status_str(void) {
    switch (s_status) {
        case SD_STATUS_OK:         return "OK";
        case SD_STATUS_NO_CARD:    return "NO CARD";
        case SD_STATUS_BAD_FORMAT: return "FORMAT FAT32";
        case SD_STATUS_FULL:       return "CARD FULL";
        case SD_STATUS_WRITE_FAIL: return "WRITE FAIL";
    }
    return "?";
}

bool sdcard_selftest(void) {
    if (!s_card) return false;

    const char *path = SDCARD_MOUNT_POINT "/.espdash_test";
    static const char pattern[] = "espDash SD self-test 0123456789";

    FILE *f = fopen(path, "wb");
    if (!f) {
        Serial.println("[SD] self-test: cannot create file");
        s_status = SD_STATUS_WRITE_FAIL;
        return false;
    }
    size_t wrote = fwrite(pattern, 1, sizeof(pattern), f);
    // fflush+fsync before reading back, otherwise a successful compare could
    // be served from the stdio buffer and prove nothing about the card.
    fflush(f);
    fsync(fileno(f));
    fclose(f);

    if (wrote != sizeof(pattern)) {
        Serial.println("[SD] self-test: short write");
        s_status = SD_STATUS_WRITE_FAIL;
        remove(path);
        return false;
    }

    char back[sizeof(pattern)] = {0};
    f = fopen(path, "rb");
    if (!f) {
        Serial.println("[SD] self-test: cannot reopen");
        s_status = SD_STATUS_WRITE_FAIL;
        return false;
    }
    size_t got = fread(back, 1, sizeof(back), f);
    fclose(f);
    remove(path);

    if (got != sizeof(pattern) || memcmp(back, pattern, sizeof(pattern)) != 0) {
        Serial.println("[SD] self-test: READ-BACK MISMATCH - card is failing or counterfeit");
        s_status = SD_STATUS_WRITE_FAIL;
        return false;
    }

    Serial.println("[SD] self-test passed");
    return true;
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
