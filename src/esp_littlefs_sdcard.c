#include "esp_littlefs_sdcard.h"

#include <memory.h>

#include "esp_littlefs_abs.h"
#include "esp_log.h"
#include "sdmmc_cmd.h"

static const char *const TAG = ESP_LITTLEFS_SD_TAG;

// region little fs hooks

static int littlefs_api_read(const struct lfs_config *c, lfs_block_t block,
                             lfs_off_t off, void *buffer, lfs_size_t size) {
    sdmmc_card_t *sd = c->context;
    esp_err_t err = sdmmc_read_sectors(sd, buffer, block + off / c->block_size, size / c->block_size);
    if (err) {
        size_t part_off = (block * c->block_size) + off;
        ESP_LOGE(TAG, "failed to read addr %08zx, size %08x, err %d", part_off, size, err);
        return LFS_ERR_IO;
    }
    return 0;
}

static int littlefs_api_prog(const struct lfs_config *c, lfs_block_t block,
                             lfs_off_t off, const void *buffer, lfs_size_t size) {
    sdmmc_card_t *sd = c->context;
    esp_err_t err = sdmmc_write_sectors(sd, buffer, block + off / c->block_size, size / c->block_size);
    if (err) {
        size_t part_off = (block * c->block_size) + off;
        ESP_LOGE(TAG, "failed to write addr %08zx, size %08x, err %d", part_off, size, err);
        return LFS_ERR_IO;
    }
    return 0;
}

static int littlefs_api_erase(const struct lfs_config *c, lfs_block_t block) {
    return 0;
}

static int littlefs_api_sync(const struct lfs_config *c) {
    /* Unnecessary for esp-idf */
    return 0;
}

// endregion

// region public api

esp_err_t esp_littlefs_sd_create(lfs_t **lfs, const esp_littlefs_sd_create_conf_t *conf) {
    if (conf->sd_card == NULL) {
        ESP_LOGE(TAG, "Sdcard must be provided.");
        return ESP_ERR_INVALID_ARG;
    }
    sdmmc_card_t * sd_storage = malloc(sizeof(sdmmc_card_t));
    if (sd_storage == NULL)
        return ESP_ERR_NO_MEM;
    *sd_storage = *conf->sd_card;
    struct lfs_config config = {0};
    {/* LittleFS Configuration */
        config.context = sd_storage;

        // block device operations
        config.read = littlefs_api_read;
        config.prog = littlefs_api_prog;
        config.erase = littlefs_api_erase;
        config.sync = littlefs_api_sync;

        // block device configuration
        config.read_size = conf->sd_card->csd.sector_size;
        config.prog_size = conf->sd_card->csd.sector_size;
        config.block_size = conf->sd_card->csd.sector_size;
        config.block_count = conf->sd_card->csd.capacity;
        config.cache_size = conf->lfs_cache_size;
        config.lookahead_size = conf->lfs_lookahead_size;
        config.block_cycles = conf->lfs_block_cycles;
    }
    return esp_littlefs_abs_create(lfs, &config, conf->format_on_error, free);
}

esp_err_t esp_littlefs_sd_delete(lfs_t **lfs) {
    return esp_littlefs_abs_delete(lfs);
}

esp_err_t esp_littlefs_sd_erase(sdmmc_card_t *sdCard) {
    ESP_LOGV(TAG, "Erasing sdcard...");
    void *tmp = malloc(sdCard->csd.sector_size);
    if (!tmp)
        return ESP_ERR_NO_MEM;
    memset(tmp, 0, sdCard->csd.sector_size);
    esp_err_t err = err = sdmmc_write_sectors(sdCard, tmp, 0, sdCard->csd.capacity);
    free(tmp);
    return err;
}

// endregion
