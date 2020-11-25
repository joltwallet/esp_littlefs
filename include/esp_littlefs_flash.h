#ifndef ESP_LITTLEFS_FLASH_H
#define ESP_LITTLEFS_FLASH_H

#include "littlefs/lfs.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

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
 * Format the littlefs partition
 *
 * @param partition_label  Label of the partition to format.
 * @return
 *          - ESP_OK      if successful
 *          - ESP_FAIL    on error
 */
esp_err_t esp_littlefs_flash_format(const char* partition_label);
/**
 * Get information for littlefs
 *
 * @param partition_label           Optional, label of the partition to get info for.
 * @param[out] total_bytes          Size of the file system
 * @param[out] used_bytes           Current used bytes in the file system
 *
 * @return
 *          - ESP_OK                  if success
 *          - ESP_ERR_INVALID_STATE   if not a flashfs
 */
esp_err_t esp_littlefs_flash_info(lfs_t *lfs, size_t *total_bytes, size_t *used_bytes);

#ifdef __cplusplus
} // extern "C"
#endif

#endif //ESP_LITTLEFS_FLASH_H
