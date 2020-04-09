#ifndef ESP_LITTLEFS_API_H__
#define ESP_LITTLEFS_API_H__

#include <stdint.h>
#include <stddef.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_vfs.h"
#include "esp_partition.h"
#include "littlefs/lfs.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief a file descriptor
 * That's also a chained list used for keeping tracks of all opened file descriptor 
 */
typedef struct _vfs_littlefs_file_t {
    lfs_file_t file;
    uint32_t   hash;
    struct _vfs_littlefs_file_t * next;
    char     * path;
} vfs_littlefs_file_t;

/**
 * @brief littlefs definition structure
 */
typedef struct {
    lfs_t *fs;                                /*!< Handle to the underlying littlefs */
    SemaphoreHandle_t lock;                   /*!< FS lock */
    const esp_partition_t* partition;         /*!< The partition on which littlefs is located */
    char base_path[ESP_VFS_PATH_MAX+1];       /*!< Mount point */

    struct lfs_config cfg;                    /*!< littlefs Mount configuration */
    vfs_littlefs_file_t *file;                /*!< List of files */
    vfs_littlefs_file_t **cache;              /*!< A cache of pointers to the opened files */
    uint16_t             cache_size;          /*!< The cache allocated size (in pointers) */
    uint16_t             fd_count;            /*!< The count of opened file descriptor used to speed up computation */
} esp_littlefs_t;

/**
 * @brief Read a region in a block.
 *
 * Negative error codes are propogated to the user.
 *
 * @return errorcode. 0 on success.
 */
int littlefs_api_read(const struct lfs_config *c, lfs_block_t block,
        lfs_off_t off, void *buffer, lfs_size_t size);

/**
 * @brief Program a region in a block.
 *
 * The block must have previously been erased. 
 * Negative error codes are propogated to the user.
 * May return LFS_ERR_CORRUPT if the block should be considered bad.
 *
 * @return errorcode. 0 on success.
 */
int littlefs_api_prog(const struct lfs_config *c, lfs_block_t block,
        lfs_off_t off, const void *buffer, lfs_size_t size);

/**
 * @brief Erase a block.
 *
 * A block must be erased before being programmed.
 * The state of an erased block is undefined.
 * Negative error codes are propogated to the user.
 * May return LFS_ERR_CORRUPT if the block should be considered bad.
 * @return errorcode. 0 on success.
 */
int littlefs_api_erase(const struct lfs_config *c, lfs_block_t block);

/**
 * @brief Sync the state of the underlying block device.
 *
 * Negative error codes are propogated to the user.
 *
 * @return errorcode. 0 on success.
 */
int littlefs_api_sync(const struct lfs_config *c);

#ifdef __cplusplus
}
#endif

#endif
