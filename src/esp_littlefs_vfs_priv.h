#ifndef ESP_LITTLEFS_VFS_PRIV_H
#define ESP_LITTLEFS_VFS_PRIV_H

#include "esp_vfs.h"
#include "littlefs/lfs.h"
#include "freertos/semphr.h"

#include "esp_littlefs_vfs.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct esp_littlefs_vfs_file_t esp_littlefs_vfs_file_t;
/**
 * @brief a file descriptor
 * That's also a singly linked list used for keeping tracks of all opened file descriptors.
 *
 * Shortcomings/potential issues of 32-bit hash (when CONFIG_LITTLEFS_USE_ONLY_HASH) listed here:
 *     * unlink - If a different file is open that generates a hash collision, it will report an
 *                error that it cannot unlink an open file.
 *     * rename - If a different file is open that generates a hash collision with
 *                src or dst, it will report an error that it cannot rename an open file.
 * Potential consequences:
 *    1. A file cannot be deleted while a collision-generating file is open.
 *       Worst-case, if the other file is always open during the lifecycle
 *       of your app, it's collision file cannot be deleted, which in the
 *       worst-case could cause storage-capacity issues.
 *    2. Same as (1), but for renames
 */
struct esp_littlefs_vfs_file_t {
    lfs_file_t file;
    uint32_t hash;
    esp_littlefs_vfs_file_t *next;       /*!< Pointer to next file in Singly Linked List */
#ifndef CONFIG_LITTLEFS_USE_ONLY_HASH
    char *path;
#endif
};

/**
* @brief littlefs vfs definition structure
*/
typedef struct {
    esp_littlefs_vfs_mount_conf_t conf;
    /**
     * FS lock
     */
    SemaphoreHandle_t lock;
    /**
     * Singly Linked List of files
     */
    esp_littlefs_vfs_file_t *file;
    /**
     * A cache of pointers to the opened files
     */
    esp_littlefs_vfs_file_t **cache;

    /**
     * The cache allocated size (in pointers)
     */
    uint16_t cache_size;
    /**
     * The count of opened file descriptor used to speed up computation
     */
    uint16_t fd_count;
} esp_littlefs_vlfs_t;

/**
 * @brief littlefs DIR structure
 */
typedef struct {
    DIR dir;            /*!< VFS DIR struct */
    lfs_dir_t d;        /*!< littlefs DIR struct */
    struct dirent e;    /*!< Last open dirent */
    long offset;        /*!< Offset of the current dirent */
    char *path;         /*!< Requested directory name */
} esp_littlefs_vfs_dir_t;

static void free_vlfs_fds(esp_littlefs_vlfs_t *vlfs);

static int vfs_readdir_r(void *ctx, DIR *pdir, struct dirent *entry, struct dirent **out_dirent);

static int vfs_mkdir(void *ctx, const char *name, mode_t mode);

static int vfs_rmdir(void *ctx, const char *name);

#ifdef __cplusplus
} // extern "C"
#endif

#endif //ESP_LITTLEFS_VFS_PRIV_H
