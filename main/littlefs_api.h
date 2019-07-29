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

typedef struct {
    lfs_file_t file;
    char path[LFS_NAME_MAX]; // TODO: dynamically allocate
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
    vfs_littlefs_file_t *files;               /*!< Array of files */
    uint16_t fd_used;                         /*!< */
    uint8_t max_files;                        /*!< Maximum number of file descriptors */
    uint8_t mounted:1;                        /*!< littlefs is mounted */
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
