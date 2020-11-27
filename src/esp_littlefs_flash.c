#include "esp_littlefs_flash.h"

#include "esp_littlefs_abs.h"

#include <string.h>

#include "esp_partition.h"
#include "esp_log.h"

/** ESP32 can only operate at 4kb */
#define CONFIG_LITTLEFS_BLOCK_SIZE 4096

static const char *const TAG = ESP_LITTLEFS_FLASH_TAG;

// region little fs hooks

static int littlefs_api_read(const struct lfs_config *c, lfs_block_t block,
                      lfs_off_t off, void *buffer, lfs_size_t size) {
    esp_partition_t * partition = c->context;
    size_t part_off = (block * c->block_size) + off;
    esp_err_t err = esp_partition_read(partition, part_off, buffer, size);
    if (err) {
        ESP_LOGE(TAG, "failed to read addr %08zx, size %08x, err %d", part_off, size, err);
        return LFS_ERR_IO;
    }
    return 0;
}

static int littlefs_api_prog(const struct lfs_config *c, lfs_block_t block,
                      lfs_off_t off, const void *buffer, lfs_size_t size) {
    esp_partition_t * partition = c->context;
    size_t part_off = (block * c->block_size) + off;
    esp_err_t err = esp_partition_write(partition, part_off, buffer, size);
    if (err) {
        ESP_LOGE(TAG, "failed to write addr %08zx, size %08x, err %d", part_off, size, err);
        return LFS_ERR_IO;
    }
    return 0;
}

static int littlefs_api_erase(const struct lfs_config *c, lfs_block_t block) {
    esp_partition_t * partition = c->context;
    size_t part_off = block * c->block_size;
    esp_err_t err = esp_partition_erase_range(partition, part_off, c->block_size);
    if (err) {
        ESP_LOGE(TAG, "failed to erase addr %08zx, size %08x, err %d", part_off, c->block_size, err);
        return LFS_ERR_IO;
    }
    return 0;

}

static int littlefs_api_sync(const struct lfs_config *c) {
    /* Unnecessary for esp-idf */
    return 0;
}

// endregion

// region public api

esp_err_t esp_littlefs_flash_create(const char * partition_label, lfs_t ** lfs, bool format_on_error) {
    // get partition details
    if (partition_label == NULL) {
        ESP_LOGE(TAG, "Partition label must be provided.");
        return ESP_ERR_INVALID_ARG;
    }

    esp_partition_t * partition = esp_partition_find_first(
            ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY,
            partition_label);

    if (!partition) {
        ESP_LOGE(TAG, "partition \"%s\" could not be found", partition_label);
        return ESP_ERR_NOT_FOUND;
    }

    struct lfs_config config = { 0 };
    {/* LittleFS Configuration */
        config.context = partition;

        // block device operations
        config.read  = littlefs_api_read;
        config.prog  = littlefs_api_prog;
        config.erase = littlefs_api_erase;
        config.sync  = littlefs_api_sync;

        // block device configuration
        config.read_size = CONFIG_LITTLEFS_READ_SIZE;
        config.prog_size = CONFIG_LITTLEFS_WRITE_SIZE;
        config.block_size = CONFIG_LITTLEFS_BLOCK_SIZE;;
        config.block_count = partition->size / config.block_size;
        config.cache_size = CONFIG_LITTLEFS_CACHE_SIZE;
        config.lookahead_size = CONFIG_LITTLEFS_LOOKAHEAD_SIZE;
        config.block_cycles = CONFIG_LITTLEFS_BLOCK_CYCLES;
    }
    esp_littlefs_abs_create(lfs, &config, format_on_error, NULL);
}
esp_err_t esp_littlefs_flash_delete(lfs_t ** lfs) {
    esp_littlefs_abs_delete(lfs);
}
esp_err_t esp_littlefs_flash_format(const char * partition_label) {
    ESP_LOGV(TAG, "Erasing partition...");

    const esp_partition_t* partition = esp_partition_find_first(
            ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY,
            partition_label);
    if (!partition) {
        ESP_LOGE(TAG, "partition \"%s\" could not be found", partition_label);
        return ESP_ERR_NOT_FOUND;
    }

    if( esp_partition_erase_range(partition, 0, partition->size) != ESP_OK ) {
        ESP_LOGE(TAG, "Failed to erase partition");
        return ESP_FAIL;
    }

    return ESP_OK;
}

// endregion
