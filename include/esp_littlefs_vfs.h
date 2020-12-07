#ifndef ESP_LITTLEFS_VFS_H
#define ESP_LITTLEFS_VFS_H

#include "../src/littlefs/lfs.h"

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ESP_LITTLEFS_VFS_TAG "LFS_VFS"

/**
 * @brief Last Modified Time
 *
 * Use 't' for LITTLEFS_ATTR_MTIME to match example:
 *     https://github.com/ARMmbed/littlefs/issues/23#issuecomment-482293539
 * And to match other external tools such as:
 *     https://github.com/earlephilhower/mklittlefs
 */
#define LITTLEFS_ATTR_MTIME ((uint8_t) 't')

#define ESP_LITTLEFS_VFS_MOUNT_CONFIG_DEFAULT() { \
    .mount_point = "/littlefs", \
    .lfs = NULL, \
    .fd_cache_realloc_factor = 2, \
    .fd_cache_min_size = 4, \
    .fd_cache_hyst = 4 \
}

/**
 * Configuration structure for esp_littlefs_vfs_mount.
 * Always initialize this with the ESP_LITTLEFS_VFS_MOUNT_CONFIG_DEFAULT() macro to ensure that all fields are filled with valid values.
 */
typedef struct {
    /**
     * The path to mount the fs into.
     */
    const char *mount_point;
    /**
     * The littlefs to mount. The lfs_t must be valid until esp_littlefs_vfs_unmount is called.
     */
    lfs_t *lfs;
    /**
     * Amount to resize FD cache by
     */
    uint8_t fd_cache_realloc_factor;
    /**
     * Minimum size of FD cache
     */
    uint8_t fd_cache_min_size;
    /**
     * When shrinking, leave this many trailing FD slots available
     */
    uint8_t fd_cache_hyst;
} esp_littlefs_vfs_mount_conf_t;

/**
 * Mounts a littlefs into the esp vfs. After this function has been called the provided littlefs api must not be used directly on the lfs_t instance. Instead use the vfs.
 */
esp_err_t esp_littlefs_vfs_mount(const esp_littlefs_vfs_mount_conf_t *conf);

/**
 * Unmounts a littlefs from the esp vfs. After this function the littlefs api can be used on the lfs_t instance again.
 * @param lfs The littlefs to unmount.
 */
esp_err_t esp_littlefs_vfs_unmount(lfs_t *lfs);

/**
 * @return This function returns the the path the littlefs was mounted under. If this littlefs is not mounted NULL is returned.
 */
const char *esp_littlefs_vfs_mount_point(lfs_t *lfs);

/**
 * This function can be used to acquire a lock on the lfs_t instance to use the littlefs api directly while still leaving the lfs_t mounted in the vfs. NOT RECOMMENDED.
 */
esp_err_t esp_littlefs_vfs_lock(lfs_t *lfs);

esp_err_t esp_littlefs_vfs_unlock(lfs_t *lfs);

#if CONFIG_LITTLEFS_HUMAN_READABLE
/**
 * @brief converts an enumerated lfs error into a string.
 * @param lfs_errno The enumerated littlefs error.
 */
const char * esp_littlefs_errno(enum lfs_error lfs_errno);
#endif

#ifdef __cplusplus
} // extern "C"
#endif

#endif //ESP_LITTLEFS_VFS_H
