#include "esp_littlefs_vfs_priv.h"

#include <fcntl.h>
#include <sys/param.h>
#include <errno.h>

#include "esp_log.h"

#define CONFIG_LITTLEFS_SPIFFS_COMPAT 1

static const char *const TAG = ESP_LITTLEFS_VFS_TAG;

// region helpers

// region vlfs_list

static SemaphoreHandle_t vlfs_list_lock = NULL;
/**
 * Sparse global list of all mounted filesystems.
 */
static esp_littlefs_vlfs_t **vlfs_list = NULL;
static size_t vlfs_list_size = 0;
static size_t vlfs_list_cap = 0;

/**
 * Grows the efs_list to be able to store at least newCap elements.
 * @warning This must be called with the vlfs_list_lock taken
 */
static esp_err_t vlfs_list_grow_to(size_t newCap) {
    if (newCap <= vlfs_list_cap)
        return ESP_OK;
    esp_littlefs_vlfs_t **tmp = realloc(vlfs_list, newCap);
    if (tmp == NULL)
        return ESP_ERR_NO_MEM;
    vlfs_list = tmp;
    // zero new elements oldCap -> newCap-oldCap
    memset(vlfs_list + vlfs_list_cap, 0, sizeof(esp_littlefs_vlfs_t *) * (newCap - vlfs_list_cap));
    vlfs_list_cap = newCap;
    return ESP_OK;
}

/**
 * @warning This must be called with the vlfs_list_lock taken
 */
static esp_err_t vlfs_list_insert(esp_littlefs_vlfs_t *vlfs) {
    // ensure that the list has enough slots
    esp_err_t err = vlfs_list_grow_to(vlfs_list_size + 1);
    if (err != ESP_OK)
        return err;
    // find the first empty slot
    size_t vlfs_index = 0;
    while (vlfs_list[vlfs_index] != NULL)
        vlfs_index++;
    // add the vlfs to the list
    vlfs_list[vlfs_index] = vlfs;
    vlfs_list_size++;
}

/**
 * @warning This must be called with the vlfs_list_lock taken
 */
static esp_err_t vlfs_list_remove(esp_littlefs_vlfs_t *vlfs) {
    // find the first matching vlfs
    size_t vlfs_index = 0;
    while (vlfs_index < vlfs_list_cap && vlfs_list[vlfs_index] != vlfs)
        vlfs_index++;
    if (vlfs_index >= vlfs_list_cap)
        return ESP_ERR_NOT_FOUND;
    // remove the vlfs from the list
    vlfs_list[vlfs_index] = NULL;
    vlfs_list_size--;
    return ESP_OK;
}

/**
 * @warning This must be called with the vlfs_list_lock taken
 */
static esp_littlefs_vlfs_t *vlfs_list_find_by_lfs(lfs_t *lfs) {
    // find the first matching vlfs
    size_t vlfs_index = 0;
    while (vlfs_index < vlfs_list_cap && vlfs_list[vlfs_index]->conf.lfs != lfs)
        vlfs_index++;
    if (vlfs_index >= vlfs_list_cap)
        return NULL;
    return vlfs_list[vlfs_index];
}

/**
 * @warning This must be called with the vlfs_list_lock taken
 */
static void free_vlfs(esp_littlefs_vlfs_t **vlfsArg) {
    esp_littlefs_vlfs_t *vlfs = *vlfsArg;
    free_vlfs_fds(vlfs);

    // remove the vlfs from the efs_list
    vlfs_list_remove(vlfs);

    vSemaphoreDelete(vlfs->lock);
    free(vlfs);

    *vlfsArg = NULL;
}

/**
 * @warning This must be called with the vlfs_list_lock taken
 */
static esp_err_t create_vlfs(const esp_littlefs_vfs_mount_conf_t *conf, esp_littlefs_vlfs_t **vlfsArg) {
    esp_littlefs_vlfs_t *vlfs = malloc(sizeof(esp_littlefs_vlfs_t));
    if (vlfs == NULL)
        return ESP_ERR_NO_MEM;
    vlfs->conf = *conf;

    vlfs->cache_size = 4;
    vlfs->cache = calloc(sizeof(esp_littlefs_vfs_file_t *), vlfs->cache_size);
    if (vlfs->cache == NULL) {
        free(vlfs);
        return ESP_ERR_NO_MEM;
    }

    vlfs->lock = xSemaphoreCreateMutex();
    if (vlfs->lock == NULL) {
        free(vlfs->cache);
        free(vlfs);
        return ESP_ERR_NO_MEM;
    }

    vlfs->fd_count = 0;
    vlfs->file = NULL;

    *vlfsArg = vlfs;
    // insert it into the vlfs_list
    esp_err_t err = vlfs_list_insert(vlfs);
    if (err != ESP_OK) {
        free_vlfs(vlfsArg);
        return err;
    }

    return ESP_OK;
}

// endregion

// region helpers

#if CONFIG_LITTLEFS_HUMAN_READABLE
/**
 * @brief converts an enumerated lfs error into a string.
 * @param lfs_error The littlefs error.
 */
const char * esp_littlefs_errno(enum lfs_error lfs_errno) {
    switch(lfs_errno){
        case LFS_ERR_OK: return "LFS_ERR_OK";
        case LFS_ERR_IO: return "LFS_ERR_IO";
        case LFS_ERR_CORRUPT: return "LFS_ERR_CORRUPT";
        case LFS_ERR_NOENT: return "LFS_ERR_NOENT";
        case LFS_ERR_EXIST: return "LFS_ERR_EXIST";
        case LFS_ERR_NOTDIR: return "LFS_ERR_NOTDIR";
        case LFS_ERR_ISDIR: return "LFS_ERR_ISDIR";
        case LFS_ERR_NOTEMPTY: return "LFS_ERR_NOTEMPTY";
        case LFS_ERR_BADF: return "LFS_ERR_BADF";
        case LFS_ERR_FBIG: return "LFS_ERR_FBIG";
        case LFS_ERR_INVAL: return "LFS_ERR_INVAL";
        case LFS_ERR_NOSPC: return "LFS_ERR_NOSPC";
        case LFS_ERR_NOMEM: return "LFS_ERR_NOMEM";
        case LFS_ERR_NOATTR: return "LFS_ERR_NOATTR";
        case LFS_ERR_NAMETOOLONG: return "LFS_ERR_NAMETOOLONG";
        default: return "LFS_ERR_UNDEFINED";
    }
    return "";
}
#else
#define esp_littlefs_errno(x) ""
#endif

/**
 * @brief Compute the 32bit DJB2 hash of the given string.
 * @param[in]   path the path to hash
 * @returns the hash for this path
 */
static uint32_t compute_hash(const char * path) {
    uint32_t hash = 5381u;
    char c;

    while ((c = *path++))
        hash = ((hash << 5u) + hash) + c; /* hash * 33 + c */
    return hash;
}

/**
 * @brief Convert fcntl flags to littlefs flags
 * @param m fcntl flags
 * @return lfs flags
 */
static int fcntl_flags_to_lfs_flag(int m) {
    int lfs_flags = 0;
    if (m == O_APPEND) {ESP_LOGV(TAG, "O_APPEND"); lfs_flags |= LFS_O_APPEND;}
    if (m == O_RDONLY) {ESP_LOGV(TAG, "O_RDONLY"); lfs_flags |= LFS_O_RDONLY;}
    if (m & O_WRONLY)  {ESP_LOGV(TAG, "O_WRONLY"); lfs_flags |= LFS_O_WRONLY;}
    if (m & O_RDWR)    {ESP_LOGV(TAG, "O_RDWR");   lfs_flags |= LFS_O_RDWR;}
    if (m & O_EXCL)    {ESP_LOGV(TAG, "O_EXCL");   lfs_flags |= LFS_O_EXCL;}
    if (m & O_CREAT)   {ESP_LOGV(TAG, "O_CREAT");  lfs_flags |= LFS_O_CREAT;}
    if (m & O_TRUNC)   {ESP_LOGV(TAG, "O_TRUNC");  lfs_flags |= LFS_O_TRUNC;}
    return lfs_flags;
}

// endregion

// region vlfs functions

/**
 * @brief Release a file descriptor
 * @param[in,out] vlfs      File system context
 * @param[in]     fd        File Descriptor to release
 * @return 0 on success. -1 if the FD cannot be freed.
 * @warning This must be called with the vlfs lock taken
 */
static int free_vlfs_fd(esp_littlefs_vlfs_t * vlfs, int fd) {
    esp_littlefs_vfs_file_t * file, * head;

    if((uint32_t)fd >= vlfs->cache_size) {
        ESP_LOGE(TAG, "FD %d must be < %d", fd, vlfs->cache_size);
        return -1;
    }

    /* Get the file descriptor to free it */
    file = vlfs->cache[fd];
    head = vlfs->file;
    /* Search for file in SLL to remove it */
    if (file == head)
        /* Last file, can't fail */
        vlfs->file = vlfs->file->next;
    else {
        while (head && head->next != file)
            head = head->next;
        if (!head) {
            ESP_LOGE(TAG, "Inconsistent list");
            return -1;
        }
        /* Transaction starts here and can't fail anymore */
        head->next = file->next;
    }
    vlfs->cache[fd] = NULL;
    vlfs->fd_count--;

    ESP_LOGV(TAG, "Clearing FD");
    free(file);

#if 0
    /* Realloc smaller if its possible
     *     * Find and realloc based on number of trailing NULL ptrs in cache
     *     * Leave some hysteris to prevent thrashing around resize points
     * This is disabled for now because it adds unnecessary complexity
     * and binary size increase that outweights its ebenfits.
     */
    if(vlfs->cache_size > vlfs->conf.fd_cache_min_size) {
        uint16_t n_free;
        uint16_t new_size = vlfs->cache_size / vlfs->conf.fd_cache_realloc_factor;

        if(new_size >= vlfs->conf.fd_cache_min_size) {
            /* Count number of trailing NULL ptrs */
            for(n_free=0; n_free < vlfs->cache_size; n_free++) {
                if(vlfs->cache[vlfs->cache_size - n_free - 1] != NULL) {
                    break;
                }
            }

            if(n_free >= (vlfs->cache_size - new_size)){
                new_size += vlfs->conf.fd_cache_hyst;
                ESP_LOGV(TAG, "Reallocating cache %i -> %i", vlfs->cache_size, new_size);
                esp_littlefs_vfs_file_t ** new_cache;
                new_cache = realloc(vlfs->cache, new_size * sizeof(esp_littlefs_vfs_file_t *));
                /* No harm on realloc failure, continue using the oversized cache */
                if(new_cache != NULL) {
                    vlfs->cache = new_cache;
                    vlfs->cache_size = new_size;
                }
            }
        }
    }
#endif

    return 0;
}

static void free_vlfs_fds(esp_littlefs_vlfs_t *vlfs) {
    /* Need to free all files that were opened */
    while (vlfs->file) {
        esp_littlefs_vfs_file_t *next = vlfs->file->next;
        free(vlfs->file);
        vlfs->file = next;
    }
    if (vlfs->cache != NULL)
        free(vlfs->cache);
    vlfs->cache = NULL;
    vlfs->cache_size = vlfs->fd_count = 0;
}

/**
 * @brief Create a file descriptor
 * @param[in,out] vlfs      File system context
 * @param[out]    file      Pointer to a file that'll be filled with a file object
 * @param[in]     path_len  The length of the filepath in bytes (including terminating zero byte)
 * @returns integer file descriptor. Returns -1 if no FD could be obtained.
 * @warning This must be called with the vlfs lock taken
 */
static int vlfs_create_fd(esp_littlefs_vlfs_t * vlfs, esp_littlefs_vfs_file_t ** file
#ifndef CONFIG_LITTLEFS_USE_ONLY_HASH
        , const size_t path_len
#endif
) {
    int i = -1;

    assert(vlfs->fd_count < UINT16_MAX );
    assert(vlfs->cache_size < UINT16_MAX );

    /* Make sure there is enough space in the cache to store new fd */
    if (vlfs->fd_count + 1 > vlfs->cache_size) {
        uint16_t new_size = (uint16_t)MIN(UINT16_MAX, vlfs->conf.fd_cache_realloc_factor * vlfs->cache_size);
        /* Resize the cache */
        esp_littlefs_vfs_file_t ** new_cache = realloc(vlfs->cache, new_size * sizeof(esp_littlefs_vfs_file_t *));
        if (new_cache == NULL)
            // try again with just one element
            new_cache = realloc(vlfs->cache, (uint16_t)MIN(UINT16_MAX, vlfs->cache_size + 1));
        if (new_cache == NULL) {
            ESP_LOGE(TAG, "Unable to allocate file cache");
            return -1; /* If it fails here, no harm is done to the filesystem, so it's safe */
        }
        /* Zero out the new portions of the cache */
        memset(new_cache + vlfs->cache_size, 0, (new_size - vlfs->cache_size) * sizeof(esp_littlefs_vfs_file_t *));
        vlfs->cache = new_cache;
        vlfs->cache_size = new_size;
    }


    /* Allocate file descriptor here now */
#ifndef CONFIG_LITTLEFS_USE_ONLY_HASH
    *file = calloc(1, sizeof(**file) + path_len);
#else
    *file = calloc(1, sizeof(**file));
#endif

    if (*file == NULL) {
        /* If it fails here, the file system might have a larger cache, but it's harmless, no need to reverse it */
        ESP_LOGE(TAG, "Unable to allocate FD");
        return -1;
    }

    /* Starting from here, nothing can fail anymore */

#ifndef CONFIG_LITTLEFS_USE_ONLY_HASH
    /* The trick here is to avoid dual allocation so the path pointer
        should point to the next byte after it:
        file => [ lfs_file | # | next | path | free_space ]
                                            |  /\
                                            |__/
    */
    (*file)->path = (char*)(*file) + sizeof(**file);
#endif

    /* Now find a free place in cache */
    for(i=0; i < vlfs->cache_size; i++) {
        if (vlfs->cache[i] == NULL) {
            vlfs->cache[i] = *file;
            break;
        }
    }
    /* Save file in the list */
    (*file)->next = vlfs->file;
    vlfs->file = *file;
    vlfs->fd_count++;
    return i;
}

/**
 * @brief Finds an open file descriptor by file name.
 * @param[in,out] vlfs      File system context
 * @param[in]     path      File path to check
 * @returns integer file descriptor. Returns -1 if not found.
 * @warning This must be called with the vlfs lock taken
 * @warning If CONFIG_LITTLEFS_USE_ONLY_HASH, there is a slim chance an
 *          erroneous FD may be returned on hash collision.
 */
static int vlfs_get_fd_by_name(esp_littlefs_vlfs_t * vlfs, const char *path){
    uint32_t hash = compute_hash(path);

    for(uint16_t i=0, j=0; i < vlfs->cache_size && j < vlfs->fd_count; i++){
        if (vlfs->cache[i]) {
            ++j;

            if (
                    vlfs->cache[i]->hash == hash  // Faster than strcmp
                    #ifndef CONFIG_LITTLEFS_USE_ONLY_HASH
                    && strcmp(path, vlfs->cache[i]->path) == 0  // May as well check in case of hash collision. Usually short-circuited.
                    #endif
                    ) {
                ESP_LOGV(TAG, "Found \"%s\" at FD %d.", path, i);
                return i;
            }
        }
    }
    ESP_LOGV(TAG, "Unable to get a find FD for \"%s\"", path);
    return -1;
}

// region spiffs compat

#if CONFIG_LITTLEFS_SPIFFS_COMPAT
/**
 * @brief Recursively make all parent directories for a file.
 * @param[in] dir Path of directories to make up to. The last element
 * of the path is assumed to be the file and IS NOT created.
 *   e.g.
 *       "foo/bar/baz"
 *   will create directories "foo" and "bar"
 */
static void mkdirs(esp_littlefs_vlfs_t * vlfs, const char *dir) {
    char tmp[CONFIG_LITTLEFS_OBJ_NAME_LEN];
    char *p = NULL;

    strlcpy(tmp, dir, sizeof(tmp));
    for(p = tmp + 1; *p; p++) {
        if(*p == '/') {
            *p = '\0';
            vfs_mkdir((void*)vlfs, tmp, S_IRWXU);
            *p = '/';
        }
    }
}

/**
 * @brief Recursively attempt to delete all empty directories for a file.
 * @param[in] dir Path of directories to delete. The last element of the path
 * is assumed to be the file and IS NOT deleted.
 *   e.g.
 *       "foo/bar/baz"
 *   will attempt to delete directories (in order):
 *       1. "foo/bar/baz"
 *       2. "foo/bar"
 *       3. "foo"
 */

static void rmdirs(esp_littlefs_vlfs_t * vlfs, const char *dir) {
    char tmp[CONFIG_LITTLEFS_OBJ_NAME_LEN];
    char *p = NULL;

    strlcpy(tmp, dir, sizeof(tmp));
    for(p = tmp + strlen(tmp) - 1; p != tmp; p--) {
        if(*p == '/') {
            *p = '\0';
            vfs_rmdir((void*)vlfs, tmp);
            *p = '/';
        }
    }
}
#endif //CONFIG_LITTLEFS_SPIFFS_COMPAT

// endregion

// endregion

/**
 * @brief Free a esp_littlefs_vfs_dir_t struct.
 */
static void free_vfs_dir_t(esp_littlefs_vfs_dir_t *dir){
    if(dir == NULL) return;
    if(dir->path) free(dir->path);
    free(dir);
}

// endregion

// region filesystem hooks

// region time

#if CONFIG_LITTLEFS_USE_MTIME
/**
 * Sets the mtime attr to t.
 */
static int vfs_update_mtime_value(esp_littlefs_vlfs_t * vlfs, const char *path, time_t t)
{
    int res;
    res = lfs_setattr(vlfs->conf.lfs, path, LITTLEFS_ATTR_MTIME,
                      &t, sizeof(t));
    if( res < 0 ) {
        errno = -res;
        ESP_LOGV(TAG, "Failed to update mtime (%d)", res);
    }

    return res;
}

static int vfs_utime(void *ctx, const char *path, const struct utimbuf *times)
{
    esp_littlefs_vlfs_t * efs = (esp_littlefs_vlfs_t *)ctx;
    time_t t;

    assert(path);

    if (times)
        t = times->modtime;
    else {
#if CONFIG_LITTLEFS_MTIME_USE_SECONDS
        // use current time
        t = time(NULL);
#elif CONFIG_LITTLEFS_MTIME_USE_NONCE
        assert( sizeof(time_t) == 4 );
        t = vfs_littlefs_get_mtime(vlfs, path);
        if( 0 == t ) t = esp_random();
        else t += 1;

        if( 0 == t ) t = 1;
#else
#error "Invalid MTIME configuration"
#endif
    }

    int ret = vfs_update_mtime_value(efs, path, t);
    return ret;
}

/**
 * Sets the mtime attr to an appropriate value
 */
static void vfs_littlefs_update_mtime(esp_littlefs_vlfs_t * vlfs, const char *path)
{
    vfs_utime(vlfs, path, NULL);
}



static time_t vfs_littlefs_get_mtime(esp_littlefs_vlfs_t * vlfs, const char *path)
{
    time_t t = 0;
    int size = lfs_getattr(vlfs->conf.lfs, path, LITTLEFS_ATTR_MTIME,
                           &t, sizeof(t));
    if( size < 0 ) {
        errno = -size;
#ifndef CONFIG_LITTLEFS_USE_ONLY_HASH
        ESP_LOGV(TAG, "Failed to get mtime attribute %s (%d)",
                 esp_littlefs_errno(size), size);
#else
        ESP_LOGV(TAG, "Failed to get mtime attribute %d", size);
#endif
    }
    return t;
}
#endif //CONFIG_LITTLEFS_USE_MTIME

// endregion

/**
 * @brief
 * @parameter efs file system context
 */
static inline BaseType_t sem_take(esp_littlefs_vlfs_t *vlfs) {
    int res;
#if LOG_LOCAL_LEVEL >= 5
    ESP_LOGV(TAG, "------------------------ Sem Taking [%s]", pcTaskGetTaskName(NULL));
#endif
    res = xSemaphoreTakeRecursive(vlfs->lock, portMAX_DELAY);
#if LOG_LOCAL_LEVEL >= 5
    ESP_LOGV(TAG, "--------------------->>> Sem Taken [%s]", pcTaskGetTaskName(NULL));
#endif
    return res;
}

/**
 * @brief
 * @parameter efs file system context
 */
static inline BaseType_t sem_give(esp_littlefs_vlfs_t *vlfs) {
#if LOG_LOCAL_LEVEL >= 5
    ESP_LOGV(TAG, "---------------------<<< Sem Give [%s]", pcTaskGetTaskName(NULL));
#endif
    return xSemaphoreGiveRecursive(vlfs->lock);
}

static int vfs_open(void* ctx, const char * path, int flags, int mode) {
    /* Note: mode is currently unused */
    int fd=-1, lfs_flags, res;
    esp_littlefs_vlfs_t * vlfs = (esp_littlefs_vlfs_t *)ctx;
    esp_littlefs_vfs_file_t * file = NULL;
    assert(path);
#ifndef CONFIG_LITTLEFS_USE_ONLY_HASH
    size_t path_len = strlen(path) + 1;  // include NULL terminator
#endif

    ESP_LOGV(TAG, "Opening %s", path);

    /* Convert flags to lfs flags */
    lfs_flags = fcntl_flags_to_lfs_flag(flags);

    /* Get a FD */
    sem_take(vlfs);
    fd = vlfs_create_fd(vlfs, &file
#ifndef CONFIG_LITTLEFS_USE_ONLY_HASH
            , path_len
#endif
    );

    if(fd < 0) {
        errno = -fd;
        sem_give(vlfs);
        ESP_LOGV(TAG, "Error obtaining FD");
        return LFS_ERR_INVAL;
    }

#if CONFIG_LITTLEFS_SPIFFS_COMPAT
    /* Create all parent directories (if necessary) */
    ESP_LOGV(TAG, "LITTLEFS_SPIFFS_COMPAT attempting to create all directories for %s", path);
    mkdirs(vlfs, path);
#endif // CONFIG_LITTLEFS_SPIFFS_COMPAT

    /* Open File */
    res = lfs_file_open(vlfs->conf.lfs, &file->file, path, lfs_flags);

    if( res < 0 ) {
        errno = -res;
        free_vlfs_fd(vlfs, fd);
        sem_give(vlfs);
#ifndef CONFIG_LITTLEFS_USE_ONLY_HASH
        ESP_LOGV(TAG, "Failed to open file %s. Error %s (%d)",
                 path, esp_littlefs_errno(res), res);
#else
        ESP_LOGV(TAG, "Failed to open file. Error %s (%d)",
                esp_littlefs_errno(res), res);
#endif
        return LFS_ERR_INVAL;
    }

    /* Sync after opening. If we are overwriting a file, this will free that
     * file's blocks in storage, prevent OOS errors.
     * See TEST_CASE:
     *     "Rewriting file frees space immediately (#7426)"
     */
    res = lfs_file_sync(vlfs->conf.lfs, &file->file);
    if(res < 0){
        errno = -res;
#ifndef CONFIG_LITTLEFS_USE_ONLY_HASH
        ESP_LOGV(TAG, "Failed to sync at opening file \"%s\". Error %s (%d)",
                 file->path, esp_littlefs_errno(res), res);
#else
        ESP_LOGV(TAG, "Failed to sync at opening file %d. Error %d", fd, res);
#endif
    }

    file->hash = compute_hash(path);
#ifndef CONFIG_LITTLEFS_USE_ONLY_HASH
    memcpy(file->path, path, path_len);
#endif

#if CONFIG_LITTLEFS_USE_MTIME
    if (lfs_flags != LFS_O_RDONLY) {
        /* If this is being opened as not read-only */
        vfs_littlefs_update_mtime(vlfs, path);
    }
#endif

    sem_give(vlfs);
    ESP_LOGV(TAG, "Done opening %s", path);
    return fd;
}

static ssize_t vfs_write(void* ctx, int fd, const void * data, size_t size) {
    esp_littlefs_vlfs_t * vlfs = (esp_littlefs_vlfs_t *)ctx;
    ssize_t res;
    esp_littlefs_vfs_file_t * file = NULL;

    sem_take(vlfs);
    if((uint32_t)fd > vlfs->cache_size) {
        sem_give(vlfs);
        ESP_LOGE(TAG, "FD %d must be <%d.", fd, vlfs->cache_size);
        return LFS_ERR_BADF;
    }
    file = vlfs->cache[fd];
    res = lfs_file_write(vlfs->conf.lfs, &file->file, data, size);
    sem_give(vlfs);

    if(res < 0){
        errno = -res;
#ifndef CONFIG_LITTLEFS_USE_ONLY_HASH
        ESP_LOGV(TAG, "Failed to write FD %d; path \"%s\". Error %s (%d)",
                 fd, file->path, esp_littlefs_errno(res), res);
#else
        ESP_LOGV(TAG, "Failed to write FD %d. Error %s (%d)",
                fd, esp_littlefs_errno(res), res);
#endif
        return res;
    }

    return res;
}

static ssize_t vfs_read(void* ctx, int fd, void * dst, size_t size) {
    esp_littlefs_vlfs_t * vlfs = (esp_littlefs_vlfs_t *)ctx;
    ssize_t res;
    esp_littlefs_vfs_file_t * file = NULL;


    sem_take(vlfs);
    if((uint32_t)fd > vlfs->cache_size) {
        sem_give(vlfs);
        ESP_LOGE(TAG, "FD %d must be <%d.", fd, vlfs->cache_size);
        return LFS_ERR_BADF;
    }
    file = vlfs->cache[fd];
    res = lfs_file_read(vlfs->conf.lfs, &file->file, dst, size);
    sem_give(vlfs);

    if(res < 0){
        errno = -res;
#ifndef CONFIG_LITTLEFS_USE_ONLY_HASH
        ESP_LOGV(TAG, "Failed to read file \"%s\". Error %s (%d)",
                 file->path, esp_littlefs_errno(res), res);
#else
        ESP_LOGV(TAG, "Failed to read FD %d. Error %s (%d)",
                fd, esp_littlefs_errno(res), res);
#endif
        return res;
    }

    return res;
}

static ssize_t vfs_pwrite(void *ctx, int fd, const void *src, size_t size, off_t offset)
{
    esp_littlefs_vlfs_t * vlfs = (esp_littlefs_vlfs_t *)ctx;
    ssize_t res, save_res;
    esp_littlefs_vfs_file_t * file = NULL;

    sem_take(vlfs);
    if ((uint32_t)fd > vlfs->cache_size)
    {
        sem_give(vlfs);
        ESP_LOGE(TAG, "FD %d must be <%d.", fd, vlfs->cache_size);
        return LFS_ERR_BADF;
    }
    file = vlfs->cache[fd];

    off_t old_offset = lfs_file_seek(vlfs->conf.lfs, &file->file, 0, SEEK_CUR);
    if (old_offset < (off_t)0)
    {
        res = old_offset;
        goto exit;
    }

    /* Set to wanted position.  */
    res = lfs_file_seek(vlfs->conf.lfs, &file->file, offset, SEEK_SET);
    if (res < (off_t)0)
        goto exit;

    /* Write out the data.  */
    res = lfs_file_write(vlfs->conf.lfs, &file->file, src, size);

    /* Now we have to restore the position.  If this fails we have to
     return this as an error. But if the writing also failed we
     return writing error.  */
    save_res = lfs_file_seek(vlfs->conf.lfs, &file->file, old_offset, SEEK_SET);
    if (res >= (ssize_t)0 && save_res < (off_t)0)
        res = save_res;
    sem_give(vlfs);

    exit:
    if (res < 0)
    {
        errno = -res;
#ifndef CONFIG_LITTLEFS_USE_ONLY_HASH
        ESP_LOGV(TAG, "Failed to write FD %d; path \"%s\". Error %s (%d)",
                 fd, file->path, esp_littlefs_errno(res), res);
#else
        ESP_LOGV(TAG, "Failed to write FD %d. Error %s (%d)",
                fd, esp_littlefs_errno(res), res);
#endif
        return -1;
    }

    return res;
}

static ssize_t vfs_pread(void *ctx, int fd, void *dst, size_t size, off_t offset)
{
    esp_littlefs_vlfs_t *efs = (esp_littlefs_vlfs_t *)ctx;
    ssize_t res, save_res;
    esp_littlefs_vfs_file_t * file = NULL;

    sem_take(efs);
    if ((uint32_t)fd > efs->cache_size)
    {
        sem_give(efs);
        ESP_LOGE(TAG, "FD %d must be <%d.", fd, efs->cache_size);
        return LFS_ERR_BADF;
    }
    file = efs->cache[fd];

    off_t old_offset = lfs_file_seek(efs->conf.lfs, &file->file, 0, SEEK_CUR);
    if (old_offset < (off_t)0)
    {
        res = old_offset;
        goto exit;
    }

    /* Set to wanted position.  */
    res = lfs_file_seek(efs->conf.lfs, &file->file, offset, SEEK_SET);
    if (res < (off_t)0)
        goto exit;

    /* Read the data.  */
    res = lfs_file_read(efs->conf.lfs, &file->file, dst, size);

    /* Now we have to restore the position.  If this fails we have to
     return this as an error. But if the reading also failed we
     return reading error.  */
    save_res = lfs_file_seek(efs->conf.lfs, &file->file, old_offset, SEEK_SET);
    if (res >= (ssize_t)0 && save_res < (off_t)0)
        res = save_res;
    sem_give(efs);

    exit:
    if (res < 0)
    {
        errno = -res;
#ifndef CONFIG_LITTLEFS_USE_ONLY_HASH
        ESP_LOGV(TAG, "Failed to read file \"%s\". Error %s (%d)",
                 file->path, esp_littlefs_errno(res), res);
#else
        ESP_LOGV(TAG, "Failed to read FD %d. Error %s (%d)",
                 fd, esp_littlefs_errno(res), res);
#endif
        return -1;
    }

    return res;
}

static int vfs_close(void* ctx, int fd) {
    // TODO update mtime on close? SPIFFS doesn't do this
    esp_littlefs_vlfs_t * vlfs = (esp_littlefs_vlfs_t *)ctx;
    int res;
    esp_littlefs_vfs_file_t * file = NULL;

    sem_take(vlfs);
    if((uint32_t)fd > vlfs->cache_size) {
        sem_give(vlfs);
        ESP_LOGE(TAG, "FD %d must be <%d.", fd, vlfs->cache_size);
        return LFS_ERR_BADF;
    }
    file = vlfs->cache[fd];
    res = lfs_file_close(vlfs->conf.lfs, &file->file);
    if(res < 0) {
        errno = -res;
        sem_give(vlfs);
#ifndef CONFIG_LITTLEFS_USE_ONLY_HASH
        ESP_LOGV(TAG, "Failed to close file \"%s\". Error %s (%d)",
                 file->path, esp_littlefs_errno(res), res);
#else
        ESP_LOGV(TAG, "Failed to close Fd %d. Error %s (%d)",
                fd, esp_littlefs_errno(res), res);
#endif
        return res;
    }
    free_vlfs_fd(vlfs, fd);
    sem_give(vlfs);
    return res;
}

static off_t vfs_lseek(void* ctx, int fd, off_t offset, int mode) {
    esp_littlefs_vlfs_t * vlfs = (esp_littlefs_vlfs_t *)ctx;
    lfs_soff_t res;
    esp_littlefs_vfs_file_t * file = NULL;
    int whence;

    switch (mode) {
        case SEEK_SET: whence = LFS_SEEK_SET; break;
        case SEEK_CUR: whence = LFS_SEEK_CUR; break;
        case SEEK_END: whence = LFS_SEEK_END; break;
        default:
            ESP_LOGE(TAG, "Invalid mode");
            return -1;
    }

    sem_take(vlfs);
    if((uint32_t)fd > vlfs->cache_size) {
        sem_give(vlfs);
        ESP_LOGE(TAG, "FD %d must be <%d.", fd, vlfs->cache_size);
        return LFS_ERR_BADF;
    }
    file = vlfs->cache[fd];
    res = lfs_file_seek(vlfs->conf.lfs, &file->file, offset, whence);
    sem_give(vlfs);

    if(res < 0){
        errno = -res;
#ifndef CONFIG_LITTLEFS_USE_ONLY_HASH
        ESP_LOGV(TAG, "Failed to seek file \"%s\" to offset %08x. Error %s (%d)",
                 file->path, (unsigned int)offset, esp_littlefs_errno(res), res);
#else
        ESP_LOGV(TAG, "Failed to seek FD %d to offset %08x. Error (%d)",
                fd, (unsigned int)offset, res);
#endif
        return res;
    }

    return res;
}

static int vfs_fsync(void* ctx, int fd)
{
    esp_littlefs_vlfs_t * vlfs = (esp_littlefs_vlfs_t *)ctx;
    ssize_t res;
    esp_littlefs_vfs_file_t * file = NULL;


    sem_take(vlfs);
    if((uint32_t)fd > vlfs->cache_size) {
        sem_give(vlfs);
        ESP_LOGE(TAG, "FD %d must be <%d.", fd, vlfs->cache_size);
        return LFS_ERR_BADF;
    }
    file = vlfs->cache[fd];
    res = lfs_file_sync(vlfs->conf.lfs, &file->file);
    sem_give(vlfs);

    if(res < 0){
        errno = -res;
#ifndef CONFIG_LITTLEFS_USE_ONLY_HASH
        ESP_LOGV(TAG, "Failed to sync file \"%s\". Error %s (%d)",
                 file->path, esp_littlefs_errno(res), res);
#else
        ESP_LOGV(TAG, "Failed to sync file %d. Error %d", fd, res);
#endif
        return res;
    }

    return res;
}


#ifndef CONFIG_LITTLEFS_USE_ONLY_HASH
static int vfs_fstat(void* ctx, int fd, struct stat * st) {
    esp_littlefs_vlfs_t * vlfs = (esp_littlefs_vlfs_t *)ctx;
    struct lfs_info info;
    int res;
    esp_littlefs_vfs_file_t * file = NULL;

    memset(st, 0, sizeof(struct stat));
    st->st_blksize = vlfs->conf.lfs->cfg->block_size;

    sem_take(vlfs);
    if((uint32_t)fd > vlfs->cache_size) {
        sem_give(vlfs);
        ESP_LOGE(TAG, "FD must be <%d.", vlfs->cache_size);
        return LFS_ERR_BADF;
    }
    file = vlfs->cache[fd];
    res = lfs_stat(vlfs->conf.lfs, file->path, &info);
    if (res < 0) {
        errno = -res;
        sem_give(vlfs);
        ESP_LOGV(TAG, "Failed to stat file \"%s\". Error %s (%d)",
                 file->path, esp_littlefs_errno(res), res);
        return res;
    }

#if CONFIG_LITTLEFS_USE_MTIME
    st->st_mtime = vfs_littlefs_get_mtime(vlfs, file->path);
#endif

    sem_give(vlfs);

    st->st_size = info.size;
    st->st_mode = ((info.type==LFS_TYPE_REG)?S_IFREG:S_IFDIR);
    return 0;
}
#endif

static int vfs_stat(void* ctx, const char * path, struct stat * st) {
    assert(path);
    esp_littlefs_vlfs_t * vlfs = (esp_littlefs_vlfs_t *)ctx;
    struct lfs_info info;
    int res;

    memset(st, 0, sizeof(struct stat));
    st->st_blksize = vlfs->conf.lfs->cfg->block_size;

    sem_take(vlfs);
    res = lfs_stat(vlfs->conf.lfs, path, &info);
    if (res < 0) {
        errno = -res;
        sem_give(vlfs);
        /* Not strictly an error, since stat can be used to check
         * if a file exists */
        ESP_LOGV(TAG, "Failed to stat path \"%s\". Error %s (%d)",
                 path, esp_littlefs_errno(res), res);
        return res;
    }
#if CONFIG_LITTLEFS_USE_MTIME
    st->st_mtime = vfs_littlefs_get_mtime(vlfs, path);
#endif
    sem_give(vlfs);
    st->st_size = info.size;
    st->st_mode = ((info.type==LFS_TYPE_REG)?S_IFREG:S_IFDIR);
    return 0;
}

static int vfs_unlink(void* ctx, const char *path) {
#define fail_str_1 "Failed to unlink path \"%s\"."
    assert(path);
    esp_littlefs_vlfs_t * vlfs = (esp_littlefs_vlfs_t *)ctx;
    struct lfs_info info;
    int res;

    sem_take(vlfs);
    res = lfs_stat(vlfs->conf.lfs, path, &info);
    if (res < 0) {
        errno = -res;
        sem_give(vlfs);
        ESP_LOGV(TAG, fail_str_1 " Error %s (%d)",
                 path, esp_littlefs_errno(res), res);
        return res;
    }

    if(vlfs_get_fd_by_name(vlfs, path) >= 0) {
        sem_give(vlfs);
        ESP_LOGE(TAG, fail_str_1 " Has open FD.", path);
        return -1;
    }

    if (info.type == LFS_TYPE_DIR) {
        sem_give(vlfs);
        ESP_LOGV(TAG, "Cannot unlink a directory.");
        return LFS_ERR_ISDIR;
    }

    res = lfs_remove(vlfs->conf.lfs, path);
    if (res < 0) {
        errno = -res;
        sem_give(vlfs);
        ESP_LOGV(TAG, fail_str_1 " Error %s (%d)",
                 path, esp_littlefs_errno(res), res);
        return res;
    }

#if CONFIG_LITTLEFS_SPIFFS_COMPAT
    /* Attempt to delete all parent directories that are empty */
    rmdirs(vlfs, path);
#endif  // CONFIG_LITTLEFS_SPIFFS_COMPAT

    sem_give(vlfs);

    return 0;
#undef fail_str_1
}

static int vfs_rename(void* ctx, const char *src, const char *dst) {
    esp_littlefs_vlfs_t * vlfs = (esp_littlefs_vlfs_t *)ctx;
    int res;

    sem_take(vlfs);

    if(vlfs_get_fd_by_name(vlfs, src) >= 0){
        sem_give(vlfs);
        ESP_LOGE(TAG, "Cannot rename; src \"%s\" is open.", src);
        return -1;
    }
    else if(vlfs_get_fd_by_name(vlfs, dst) >= 0){
        sem_give(vlfs);
        ESP_LOGE(TAG, "Cannot rename; dst \"%s\" is open.", dst);
        return -1;
    }

    res = lfs_rename(vlfs->conf.lfs, src, dst);
    sem_give(vlfs);
    if (res < 0) {
        errno = -res;
        ESP_LOGV(TAG, "Failed to rename \"%s\" -> \"%s\". Error %s (%d)",
                 src, dst, esp_littlefs_errno(res), res);
        return res;
    }

    return 0;
}

static DIR* vfs_opendir(void* ctx, const char* name) {
    esp_littlefs_vlfs_t * vlfs = (esp_littlefs_vlfs_t *)ctx;
    int res;
    esp_littlefs_vfs_dir_t * dir = NULL;

    dir = calloc(1, sizeof(esp_littlefs_vfs_dir_t));
    if(dir == NULL) {
        ESP_LOGE(TAG, "dir struct could not be malloced");
        goto exit;
    }

    dir->path = strdup(name);
    if(dir->path == NULL){
        ESP_LOGE(TAG, "dir path name could not be malloced");
        goto exit;
    }

    sem_take(vlfs);
    res = lfs_dir_open(vlfs->conf.lfs, &dir->d, dir->path);
    sem_give(vlfs);
    if (res < 0) {
        errno = -res;
#ifndef CONFIG_LITTLEFS_USE_ONLY_HASH
        ESP_LOGV(TAG, "Failed to opendir \"%s\". Error %s (%d)",
                 dir->path, esp_littlefs_errno(res), res);
#else
        ESP_LOGV(TAG, "Failed to opendir \"%s\". Error %d", dir->path, res);
#endif
        goto exit;
    }

    return (DIR *)dir;

    exit:
    free_vfs_dir_t(dir);
    return NULL;
}

static int vfs_closedir(void* ctx, DIR* pdir) {
    assert(pdir);
    esp_littlefs_vlfs_t * vlfs = (esp_littlefs_vlfs_t *)ctx;
    esp_littlefs_vfs_dir_t * dir = (esp_littlefs_vfs_dir_t *) pdir;
    int res;

    sem_take(vlfs);
    res = lfs_dir_close(vlfs->conf.lfs, &dir->d);
    sem_give(vlfs);
    if (res < 0) {
        errno = -res;
#ifndef CONFIG_LITTLEFS_USE_ONLY_HASH
        ESP_LOGV(TAG, "Failed to closedir \"%s\". Error %s (%d)",
                 dir->path, esp_littlefs_errno(res), res);
#else
        ESP_LOGV(TAG, "Failed to closedir \"%s\". Error %d", dir->path, res);
#endif
        return res;
    }

    free_vfs_dir_t(dir);
    return 0;
}

static struct dirent* vfs_readdir(void* ctx, DIR* pdir) {
    assert(pdir);
    esp_littlefs_vfs_dir_t * dir = (esp_littlefs_vfs_dir_t *) pdir;
    int res;
    struct dirent * out_dirent;

    res = vfs_readdir_r(ctx, pdir, &dir->e, &out_dirent);
    if (res != 0) return NULL;
    return out_dirent;
}

static int vfs_readdir_r(void* ctx, DIR* pdir, struct dirent* entry, struct dirent** out_dirent) {
    assert(pdir);
    esp_littlefs_vlfs_t * vlfs = (esp_littlefs_vlfs_t *)ctx;
    esp_littlefs_vfs_dir_t * dir = (esp_littlefs_vfs_dir_t *) pdir;
    int res;
    struct lfs_info info = { 0 };

    sem_take(vlfs);
    do{ /* Read until we get a real object name */
        res = lfs_dir_read(vlfs->conf.lfs, &dir->d, &info);
    }while( res>0 && (strcmp(info.name, ".") == 0 || strcmp(info.name, "..") == 0));
    sem_give(vlfs);
    if (res < 0) {
        errno = -res;
#ifndef CONFIG_LITTLEFS_USE_ONLY_HASH
        ESP_LOGV(TAG, "Failed to readdir \"%s\". Error %s (%d)",
                 dir->path, esp_littlefs_errno(res), res);
#else
        ESP_LOGV(TAG, "Failed to readdir \"%s\". Error %d", dir->path, res);
#endif
        return -1;
    }

    if(info.type == LFS_TYPE_REG) {
        ESP_LOGV(TAG, "readdir a file of size %d named \"%s\"",
                 info.size, info.name);
    }
    else {
        ESP_LOGV(TAG, "readdir a dir named \"%s\"", info.name);
    }

    if(res == 0) {
        /* End of Objs */
        ESP_LOGV(TAG, "Reached the end of the directory.");
        *out_dirent = NULL;
    }
    else {
        entry->d_ino = 0;
        entry->d_type = info.type == LFS_TYPE_REG ? DT_REG : DT_DIR;
        strncpy(entry->d_name, info.name, sizeof(entry->d_name));
        *out_dirent = entry;
    }
    dir->offset++;

    return 0;
}

static long vfs_telldir(void* ctx, DIR* pdir) {
    assert(pdir);
    esp_littlefs_vfs_dir_t * dir = (esp_littlefs_vfs_dir_t *) pdir;
    return dir->offset;
}

static void vfs_seekdir(void* ctx, DIR* pdir, long offset) {
    assert(pdir);
    esp_littlefs_vlfs_t * vlfs = (esp_littlefs_vlfs_t *)ctx;
    esp_littlefs_vfs_dir_t * dir = (esp_littlefs_vfs_dir_t *) pdir;
    int res;

    if (offset < dir->offset) {
        /* close and re-open dir to rewind to beginning */
        sem_take(vlfs);
        res = lfs_dir_rewind(vlfs->conf.lfs, &dir->d);
        sem_give(vlfs);
        if (res < 0) {
            errno = -res;
            ESP_LOGV(TAG, "Failed to rewind dir \"%s\". Error %s (%d)",
                     dir->path, esp_littlefs_errno(res), res);
            return;
        }
        dir->offset = 0;
    }

    while(dir->offset < offset){
        struct dirent *out_dirent;
        res = vfs_readdir_r(ctx, pdir, &dir->e, &out_dirent);
        if( res != 0 ){
            ESP_LOGE(TAG, "Error readdir_r");
            return;
        }
    }
}

static int vfs_mkdir(void* ctx, const char* name, mode_t mode) {
    /* Note: mode is currently unused */
    esp_littlefs_vlfs_t * vlfs = (esp_littlefs_vlfs_t *)ctx;
    int res;
    ESP_LOGV(TAG, "mkdir \"%s\"", name);

    sem_take(vlfs);
    res = lfs_mkdir(vlfs->conf.lfs, name);
    sem_give(vlfs);
    if (res < 0) {
        errno = -res;
        ESP_LOGV(TAG, "Failed to mkdir \"%s\". Error %s (%d)",
                 name, esp_littlefs_errno(res), res);
        return res;
    }
    return 0;
}

static int vfs_rmdir(void* ctx, const char* name) {
    esp_littlefs_vlfs_t * vlfs = (esp_littlefs_vlfs_t *)ctx;
    struct lfs_info info;
    int res;

    /* Error Checking */
    sem_take(vlfs);
    res = lfs_stat(vlfs->conf.lfs, name, &info);
    if (res < 0) {
        errno = -res;
        sem_give(vlfs);
        ESP_LOGV(TAG, "\"%s\" doesn't exist.", name);
        return -1;
    }

    if (info.type != LFS_TYPE_DIR) {
        sem_give(vlfs);
        ESP_LOGV(TAG, "\"%s\" is not a directory.", name);
        return -1;
    }

    /* Unlink the dir */
    res = lfs_remove(vlfs->conf.lfs, name);
    sem_give(vlfs);
    if ( res < 0) {
        errno = -res;
        ESP_LOGV(TAG, "Failed to unlink path \"%s\". Error %s (%d)",
                 name, esp_littlefs_errno(res), res);
        return -1;
    }

    return 0;
}

// endregion

// region public api

esp_err_t esp_littlefs_vfs_mount(const esp_littlefs_vfs_mount_conf_t *conf) {
    assert(conf);
    assert(conf->mount_point);
    assert(conf->lfs);
    esp_err_t err = ESP_FAIL;

    if (vlfs_list_lock == NULL) {
        static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
        portENTER_CRITICAL(&mux);
        if (vlfs_list_lock == NULL) {
            vlfs_list_lock = xSemaphoreCreateMutex();
            assert(vlfs_list_lock);
        }
        portEXIT_CRITICAL(&mux);
    }
    xSemaphoreTake(vlfs_list_lock, portMAX_DELAY);

    // Check if the lfs_t is mounted
    if (vlfs_list_find_by_lfs(conf->lfs) != NULL) {
        err = ESP_ERR_INVALID_STATE;
        goto ret;
    }

    // create the vlfs structure
    esp_littlefs_vlfs_t *vlfs;
    err = create_vlfs(conf, &vlfs);
    if (err != ESP_OK)
        goto ret;


    // register the fs to vfs
    const esp_vfs_t vfs = {
            .flags       = ESP_VFS_FLAG_CONTEXT_PTR,
            .write_p     = &vfs_write,
            .pwrite_p    = &vfs_pwrite,
            .lseek_p     = &vfs_lseek,
            .read_p      = &vfs_read,
            .pread_p     = &vfs_pread,
            .open_p      = &vfs_open,
            .close_p     = &vfs_close,
#ifndef CONFIG_LITTLEFS_USE_ONLY_HASH
            .fstat_p     = &vfs_fstat,
#else // CONFIG_LITTLEFS_USE_ONLY_HASH
            .fstat_p     = NULL, /* Not supported */
#endif // CONFIG_LITTLEFS_USE_ONLY_HASH
            .stat_p      = &vfs_stat,
            .link_p      = NULL, /* Not Supported */
            .unlink_p    = &vfs_unlink,
            .rename_p    = &vfs_rename,
            .opendir_p   = &vfs_opendir,
            .closedir_p  = &vfs_closedir,
            .readdir_p   = &vfs_readdir,
            .readdir_r_p = &vfs_readdir_r,
            .seekdir_p   = &vfs_seekdir,
            .telldir_p   = &vfs_telldir,
            .mkdir_p     = &vfs_mkdir,
            .rmdir_p     = &vfs_rmdir,
            .fsync_p     = &vfs_fsync,
#if CONFIG_LITTLEFS_USE_MTIME
            .utime_p     = &vfs_utime,
#else
            .utime_p     = NULL,
#endif // CONFIG_LITTLEFS_USE_MTIME
    };


    err = esp_vfs_register(conf->mount_point, &vfs, vlfs);
    if (err != ESP_OK) {
        free_vlfs(&vlfs);
        ESP_LOGE(TAG, "Failed to mount Littlefs to \"%s\": %s", conf->mount_point, esp_err_to_name(err));
        goto ret;
    }

    ESP_LOGV(TAG, "Successfully mounted LittleFS to \"%s\"", conf->mount_point);

    ret:
    xSemaphoreGive(vlfs_list_lock);
    return err;
}

esp_err_t esp_littlefs_vfs_unmount(lfs_t *lfs) {
    assert(lfs);
    if (!vlfs_list_lock)
        return ESP_ERR_INVALID_STATE;
    esp_littlefs_vlfs_t *vlfs = vlfs_list_find_by_lfs(lfs);
    if (vlfs == NULL)
        return ESP_ERR_NOT_FOUND;
    esp_err_t ret = ESP_FAIL;
    xSemaphoreTake(vlfs_list_lock, portMAX_DELAY);

    ret = esp_vfs_unregister(vlfs->conf.mount_point);
    free_vlfs(&vlfs);

    xSemaphoreGive(vlfs_list_lock);
    return ret;
}

const char *esp_littlefs_vfs_mount_point(lfs_t *lfs) {
    assert(lfs);
    if (!vlfs_list_lock)
        return NULL;
    const char *ret = NULL;
    xSemaphoreTake(vlfs_list_lock, portMAX_DELAY);

    esp_littlefs_vlfs_t *vlfs = vlfs_list_find_by_lfs(lfs);
    if (vlfs == NULL)
        goto ret;
    ret = vlfs->conf.mount_point;

    ret:
    xSemaphoreGive(vlfs_list_lock);
    return ret;
}

esp_err_t esp_littlefs_vfs_lock(lfs_t *lfs) {
    assert(lfs);
    if (!vlfs_list_lock)
        return ESP_ERR_INVALID_STATE;
    esp_littlefs_vlfs_t *vlfs = vlfs_list_find_by_lfs(lfs);
    if (vlfs == NULL)
        return ESP_ERR_NOT_FOUND;
    esp_err_t ret = ESP_FAIL;
    xSemaphoreTake(vlfs_list_lock, portMAX_DELAY);

    xSemaphoreTake(vlfs->lock, portMAX_DELAY);

    ret:
    xSemaphoreGive(vlfs_list_lock);
    return ret;
}

esp_err_t esp_littlefs_vfs_unlock(lfs_t *lfs) {
    assert(lfs);
    if (!vlfs_list_lock)
        return ESP_ERR_INVALID_STATE;
    esp_littlefs_vlfs_t *vlfs = vlfs_list_find_by_lfs(lfs);
    if (vlfs == NULL)
        return ESP_ERR_NOT_FOUND;
    esp_err_t ret = ESP_FAIL;
    xSemaphoreTake(vlfs_list_lock, portMAX_DELAY);

    xSemaphoreGive(vlfs->lock);

    ret:
    xSemaphoreGive(vlfs_list_lock);
    return ret;
}

// endregion
