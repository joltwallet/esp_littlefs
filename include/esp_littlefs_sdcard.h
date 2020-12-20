#ifndef ESP_LITTLEFS_SDCARD_H
#define ESP_LITTLEFS_SDCARD_H

#include "../src/littlefs/lfs.h"

#include "esp_err.h"
#include "driver/sdmmc_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ESP_LITTLEFS_SD_TAG "LFS_FLASH"

#define ESP_LITTLEFS_SD_CREATE_CONFIG_DEFAULT() { \
    .sd_card = NULL, \
    .format_on_error = true, \
    .lfs_cache_size = 512, \
    .lfs_lookahead_size = 128, \
    .lfs_block_cycles = 512 \
}

/**
* Configuration structure for esp_littlefs_sd_create.
* Always initialize this with the ESP_LITTLEFS_SD_CREATE_CONFIG_DEFAULT() macro to ensure that all fields are filled with valid values.
*/
typedef struct {
    /**
     * The sd card to use.
     */
    sdmmc_card_t *sd_card;
    bool format_on_error;
    /**
     * Size of block caches. Each cache buffers a portion of a block in RAM.
     * The littlefs needs a read cache, a program cache, and one additional
     * cache per file. Larger caches can improve performance by storing more
     * data and reducing the number of disk accesses. Must be a multiple of
     * the read and program sizes, and a factor of the block size (4096).
     */
    lfs_size_t lfs_cache_size;
    /**
     * Must be a multiple of 8.
     */
    lfs_size_t lfs_lookahead_size;
    /**
     * Number of erase cycles before littlefs evicts metadata logs and moves
     * the metadata to another block. Suggested values are in the
     * range 100-1000, with large values having better performance at the cost
     * of less consistent wear distribution.
     * Set to -1 to disable block-level wear-leveling.
     */
    int32_t lfs_block_cycles;
} esp_littlefs_sd_create_conf_t;

/**
 * @param[out] lfs The newly created little fs.
 * @param conf The config. Make sure to init the sd_card member.
 * @return ESP_OK on success.
 */
esp_err_t esp_littlefs_sd_create(lfs_t **lfs, const esp_littlefs_sd_create_conf_t *conf);

esp_err_t esp_littlefs_sd_delete(lfs_t **lfs);

/**
 * @brief erase the sdcard; make sure LittleFS is unmounted first.
 *
 * @param partition_label  Label of the partition to format.
 * @return
 *          - ESP_OK      if successful
 *          - ESP_FAIL    on error
 */
esp_err_t esp_littlefs_sd_erase(sdmmc_card_t *sdCard);

#ifdef __cplusplus
} // extern "C"
#endif

#endif //ESP_LITTLEFS_SDCARD_H
