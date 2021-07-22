#include "esp_littlefs_sdcard.h"

#include <memory.h>

#include "esp_littlefs_abs.h"
#include "esp_log.h"
#include "sdmmc_cmd.h"

static const char *const TAG = ESP_LITTLEFS_SD_TAG;

struct esp_littlefs_sd_ctx {
  sdmmc_card_t sd;
  void * dma_buf;
};

// region little fs hooks

// access the hidden esp-idf functions
esp_err_t sdmmc_read_sectors_dma(sdmmc_card_t* card, void* dst,
                                 size_t start_block, size_t block_count);
esp_err_t sdmmc_write_sectors_dma(sdmmc_card_t* card, const void* src,
                                  size_t start_block, size_t block_count);

// in the following functions the offset can be ignored. It will always be 0 since the minimum read/write size is equal to the block size.
static int littlefs_api_read(const struct lfs_config *c, lfs_block_t block,
                             lfs_off_t off, void *buffer, lfs_size_t size) {
    struct esp_littlefs_sd_ctx *ctx = c->context;

    lfs_size_t blk_count = size / c->block_size;
    for (size_t blk_off = 0; blk_off < blk_count; blk_off++) {
      esp_err_t err = sdmmc_read_sectors_dma(&ctx->sd, ctx->dma_buf, block + blk_off, 1);
      if (err) {
        size_t part_off = (block * c->block_size) + off;
        ESP_LOGE(TAG, "failed to read addr %08zx, size %08x, err %d", part_off, size, err);
        return LFS_ERR_IO;
      }
      memcpy(buffer + blk_off * c->block_size, ctx->dma_buf, c->block_size);
    }
    return 0;
}

static int littlefs_api_prog(const struct lfs_config *c, lfs_block_t block,
                             lfs_off_t off, const void *buffer, lfs_size_t size) {
    struct esp_littlefs_sd_ctx *ctx = c->context;

    lfs_size_t blk_count = size / c->block_size;
    for (size_t blk_off = 0; blk_off < blk_count; blk_off++) {
      memcpy(ctx->dma_buf, buffer + blk_off * c->block_size, c->block_size);
      esp_err_t err = sdmmc_write_sectors_dma(&ctx->sd, ctx->dma_buf, block + blk_off, 1);
      if (err) {
        size_t part_off = (block * c->block_size) + off;
        ESP_LOGE(TAG, "failed to write addr %08zx, size %08x, err %d", part_off, size, err);
        return LFS_ERR_IO;
      }
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

static void esp_littlefs_sd_free(void * ctx_void) {
  struct esp_littlefs_sd_ctx * ctx = ctx_void;
  if (ctx != NULL) {
    if (ctx->dma_buf != NULL)
      free(ctx->dma_buf);
    free(ctx);
  }
}

// region public api

esp_err_t esp_littlefs_sd_create(lfs_t **lfs, const esp_littlefs_sd_create_conf_t *conf) {
    if (conf->sd_card == NULL) {
        ESP_LOGE(TAG, "Sdcard must be provided.");
        return ESP_ERR_INVALID_ARG;
    }
    struct esp_littlefs_sd_ctx * sd_storage = malloc(sizeof(struct esp_littlefs_sd_ctx));
    if (sd_storage == NULL)
        return ESP_ERR_NO_MEM;
    sd_storage->sd = *conf->sd_card;
    sd_storage->dma_buf = heap_caps_malloc(conf->sd_card->csd.sector_size, MALLOC_CAP_DMA);
    if (sd_storage->dma_buf == NULL) {
      esp_littlefs_sd_free(sd_storage);
      return ESP_ERR_NO_MEM;
    }
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
    return esp_littlefs_abs_create(lfs, &config, conf->format_on_error, esp_littlefs_sd_free);
}

esp_err_t esp_littlefs_sd_delete(lfs_t **lfs) {
    return esp_littlefs_abs_delete(lfs);
}

esp_err_t esp_littlefs_sd_erase(sdmmc_card_t *sdCard) {
    ESP_LOGV(TAG, "Erasing sdcard...");
    void *tmp = heap_caps_malloc(sdCard->csd.sector_size, MALLOC_CAP_DMA);
    if (!tmp)
        return ESP_ERR_NO_MEM;
    memset(tmp, 0, sdCard->csd.sector_size);
    esp_err_t err = sdmmc_write_sectors_dma(sdCard, tmp, 0, sdCard->csd.capacity);
    free(tmp);
    return err;
}

// endregion
