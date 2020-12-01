#ifndef ESP_LITTLEFS_ABS_H
#define ESP_LITTLEFS_ABS_H

#include "littlefs/lfs.h"

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ESP_LITTLEFS_ABS_TAG "LFS_ABS"

/**
 * @param[out] lfs The newly created little fs.
 * @param[in] config The little fs configuration. Make sure to zero it out before filling it with values. The config will be copied.
 * @param[in] format_on_error If true the fs is formatted if an error occurs while mounting.
 * @param[in] free_ctx This function will be used to free the context of the lfs_config.
 * @return ESP_OK on success.
 */
esp_err_t
esp_littlefs_abs_create(lfs_t **lfs, struct lfs_config *config, bool format_on_error, void (*free_ctx)(void *));

esp_err_t esp_littlefs_abs_delete(lfs_t **lfs);

/**
 * Checks if a lfs is managed by the abs api.
 * @param lfs The lfs to check.
 * @return ESP_OK if this lfs is managed by the abs api. ESP_ERR_NOT_FOUND if this is not managed by the abs api.
 */
esp_err_t esp_littlefs_abs_is(lfs_t *lfs);

/**
 * Get information for littlefs
 *
 * @param[out] total_bytes          Size of the file system
 * @param[out] used_bytes           Current used bytes in the file system
 *
 * @return
 *          - ESP_OK                  if success
 *          - ESP_ERR_INVALID_STATE   if not a absfs
 */
esp_err_t esp_littlefs_abs_info(lfs_t *lfs, size_t *total_bytes, size_t *used_bytes);

#ifdef __cplusplus
} // extern "C"
#endif

#endif //ESP_LITTLEFS_ABS_H
