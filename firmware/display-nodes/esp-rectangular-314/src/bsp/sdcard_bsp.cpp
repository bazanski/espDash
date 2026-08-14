#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <Arduino.h>

#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "driver/gpio.h"
#include "ff.h"
#include "esp_heap_caps.h"

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

// Decode the partition-type byte from an MBR entry.
static const char *mbr_type_name(uint8_t t) {
    switch (t) {
        case 0x00: return "empty";
        case 0x01: return "FAT12";
        case 0x04: case 0x06: case 0x0E: return "FAT16";
        case 0x0B: case 0x0C: return "FAT32";
        case 0x07: return "exFAT/NTFS";
        case 0xEE: return "GPT protective";
        case 0x83: return "Linux";
        case 0xAF: return "HFS/HFS+";
        default:   return "other";
    }
}

void sdcard_diagnose(void) {
    Serial.println("[SD] --- diagnosing card ---");

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    // Deliberately slower than the mount path: if the card is marginal at
    // high speed this still gets us readable data to report.
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;
    host.flags = SDMMC_HOST_FLAG_1BIT;

    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 1;
    slot.clk = SDMMC_CLK_PIN;
    slot.cmd = SDMMC_CMD_PIN;
    slot.d0  = SDMMC_D0_PIN;

    if (host.init() != ESP_OK) {
        Serial.println("[SD] diag: host init failed");
        return;
    }
    if (sdmmc_host_init_slot(host.slot, &slot) != ESP_OK) {
        Serial.println("[SD] diag: slot init failed");
        host.deinit();
        return;
    }

    sdmmc_card_t *card = (sdmmc_card_t *)malloc(sizeof(sdmmc_card_t));
    if (!card) { host.deinit(); return; }

    esp_err_t err = sdmmc_card_init(&host, card);
    if (err != ESP_OK) {
        // Card did not answer at all - this is a hardware/insertion problem,
        // not a formatting one, and the distinction matters.
        Serial.printf("[SD] diag: card init FAILED (%s) - card not detected, "
                      "reseat it or check wiring\n", esp_err_to_name(err));
        free(card);
        host.deinit();
        return;
    }

    Serial.printf("[SD] diag: card OK  name='%s'  %llu MB  type=%s  speed=%d kHz\n",
                  card->cid.name,
                  ((uint64_t)card->csd.capacity * card->csd.sector_size) / (1024ULL * 1024ULL),
                  (card->ocr & (1 << 30)) ? "SDHC/SDXC" : "SDSC",
                  card->max_freq_khz);

    uint8_t *sec = (uint8_t *)heap_caps_malloc(512, MALLOC_CAP_DMA);
    if (sec && sdmmc_read_sectors(card, sec, 0, 1) == ESP_OK) {
        bool sig = (sec[510] == 0x55 && sec[511] == 0xAA);
        Serial.printf("[SD] diag: sector0 boot signature %s\n", sig ? "present" : "MISSING");

        // exFAT and FAT put their identifier in the boot sector itself; an
        // MBR instead holds four 16-byte partition entries starting at 446.
        if (memcmp(sec + 3, "EXFAT", 5) == 0) {
            Serial.println("[SD] diag: filesystem = exFAT (superfloppy, no MBR)");
            Serial.println("[SD] diag: ==> NOT SUPPORTED. Reformat as FAT32.");
        } else if (memcmp(sec + 82, "FAT32", 5) == 0) {
            Serial.println("[SD] diag: filesystem = FAT32 in sector 0 (superfloppy, no MBR)");
            Serial.println("[SD] diag: ==> should mount; if it does not, try reformatting "
                           "with an MBR partition table");
        } else if (memcmp(sec + 54, "FAT", 3) == 0) {
            Serial.printf("[SD] diag: filesystem = %.8s in sector 0 (superfloppy)\n", sec + 54);
        } else if (sig) {
            Serial.println("[SD] diag: sector 0 looks like an MBR partition table:");
            bool any = false;
            for (int i = 0; i < 4; i++) {
                const uint8_t *e = sec + 446 + i * 16;
                uint8_t type = e[4];
                uint32_t lba = (uint32_t)e[8] | ((uint32_t)e[9] << 8) |
                               ((uint32_t)e[10] << 16) | ((uint32_t)e[11] << 24);
                uint32_t cnt = (uint32_t)e[12] | ((uint32_t)e[13] << 8) |
                               ((uint32_t)e[14] << 16) | ((uint32_t)e[15] << 24);
                if (type == 0x00) continue;
                any = true;
                Serial.printf("[SD] diag:   part%d type=0x%02X (%s) start=%lu size=%lu MB\n",
                              i, type, mbr_type_name(type),
                              (unsigned long)lba, (unsigned long)(cnt / 2048));
                if (type == 0x07)
                    Serial.println("[SD] diag:   ==> exFAT/NTFS is NOT SUPPORTED. Reformat as FAT32.");
                if (type == 0xEE)
                    Serial.println("[SD] diag:   ==> GPT is NOT SUPPORTED. Reformat with an MBR table.");
            }
            if (!any) Serial.println("[SD] diag:   (no partition entries - card may be unformatted)");
        } else {
            Serial.println("[SD] diag: sector 0 has no recognisable filesystem or partition table");
        }
    } else {
        Serial.println("[SD] diag: could not read sector 0");
    }

    if (sec) free(sec);
    free(card);
    host.deinit();
    Serial.println("[SD] --- end diagnosis ---");
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

// f_getfree() walks the whole FAT to count free clusters. On this 31.9 GB
// card (~1.9M clusters) that measured ~115 ms per call - and it was being
// called from the 20 Hz UI update, which dragged loop() from ~20 ms to
// ~135 ms. That was slow enough that a button press AND its release both
// landed between two samples, so the debounce never saw a settled level and
// most presses were silently lost.
//
// Free space changes slowly and is only ever displayed, so cache it and
// refresh at most every few seconds. sdcard_free_mb() is now cheap enough to
// call from anywhere; sdcard_free_mb_refresh() forces a fresh reading where
// accuracy actually matters (before starting a recording).
#define FREE_MB_CACHE_MS 5000

static uint32_t s_free_mb = 0;
static uint32_t s_free_mb_at = 0;

static uint32_t free_mb_read(void) {
    if (!s_card) return 0;
    FATFS *fs;
    DWORD free_clusters;
    if (f_getfree("0:", &free_clusters, &fs) != FR_OK) return 0;
    uint64_t free_sectors = (uint64_t)free_clusters * fs->csize;
    return (uint32_t)((free_sectors * 512ULL) / (1024ULL * 1024ULL));
}

uint32_t sdcard_free_mb_refresh(void) {
    s_free_mb = free_mb_read();
    s_free_mb_at = millis();
    return s_free_mb;
}

uint32_t sdcard_free_mb(void) {
    if (!s_card) return 0;
    if (s_free_mb_at == 0 || (millis() - s_free_mb_at) > FREE_MB_CACHE_MS) {
        return sdcard_free_mb_refresh();
    }
    return s_free_mb;
}
