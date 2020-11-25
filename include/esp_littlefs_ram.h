#ifndef ESP_LITTLEFS_RAM_H
#define ESP_LITTLEFS_RAM_H

#include "littlefs/lfs.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

lfs_t * esp_littlefs_ram_create();
esp_err_t esp_littlefs_ram_delete(lfs_t * lfs);
esp_err_t esp_littlefs_ram_is(lfs_t * lfs);
esp_err_t esp_littlefs_ram_grow(lfs_t * lfs, uint16_t amount);

#ifdef __cplusplus
} // extern "C"
#endif

#endif //ESP_LITTLEFS_RAM_H
