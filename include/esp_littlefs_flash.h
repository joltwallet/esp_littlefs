#ifndef ESP_LITTLEFS_FLASH_H
#define ESP_LITTLEFS_FLASH_H

#include "littlefs/lfs.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ESP_LITTLEFS_FLASH_TAG "LFS_FLASH"

/**
 * @param partition_label Label of the partition to use.
 * @param[out] lfs The newly created little fs.
 * @param format_on_error Format the partiton on error.
 * @return A littlefs instance.
 */
esp_err_t esp_littlefs_flash_create(const char *partition_label, lfs_t ** lfs, bool format_on_error);
esp_err_t esp_littlefs_flash_delete(lfs_t * lfs);
esp_err_t esp_littlefs_flash_is(lfs_t * lfs);
/**
 * @brief erase a partition; make sure LittleFS is unmounted first.
 *
 * @param partition_label  Label of the partition to format.
 * @return
 *          - ESP_OK      if successful
 *          - ESP_FAIL    on error
 */
esp_err_t esp_littlefs_flash_format(const char* partition_label);

#ifdef __cplusplus
} // extern "C"
#endif

#endif //ESP_LITTLEFS_FLASH_H
