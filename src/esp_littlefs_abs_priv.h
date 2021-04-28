#ifndef ESP_LITTLEFS_ABS_PRIV_H
#define ESP_LITTLEFS_ABS_PRIV_H

#include "esp_littlefs_abs.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * This structure stores the filesystem and its config.
 */
typedef struct {
    lfs_t lfs;
    /**
     * littlefs mount configuration
     */
    struct lfs_config cfg;

    void (*free_ctx)(void *);
} esp_littlefs_vlfs_t;

#ifdef __cplusplus
} // extern "C"
#endif

#endif //ESP_LITTLEFS_ABS_PRIV_H
