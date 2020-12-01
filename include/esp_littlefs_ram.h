#ifndef ESP_LITTLEFS_RAM_H
#define ESP_LITTLEFS_RAM_H

#include "littlefs/lfs.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ESP_LITTLEFS_RAM_TAG "LFS_RAM"

/**
* @param[out] lfs The newly created little fs.
* @param size The amount of ram to reserve.
* @return ESP_OK on success.
*/
esp_err_t esp_littlefs_ram_create(lfs_t **lfs, size_t size);

esp_err_t esp_littlefs_ram_delete(lfs_t **lfs);

#ifdef __cplusplus
} // extern "C"
#endif

#endif //ESP_LITTLEFS_RAM_H
