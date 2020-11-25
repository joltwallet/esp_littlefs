#include "esp_littlefs_vfs_priv.h"

#include "esp_log.h"

static const char *const TAG = ESP_LITTLEFS_VFS_TAG;

static SemaphoreHandle_t vlfs_list_lock = NULL;
/**
 * Sparse global list of all mounted filesystems.
 */
static esp_littlefs_vlfs_t **vlfs_list = NULL;
static size_t vlfs_list_size = 0;
static size_t vlfs_list_cap = 0;

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
 * Grows the efs_list to be able to store at least newCap elements.
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
 * Before calling this function ensure that you hold the vlfs_list_lock.
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

static esp_err_t create_vlfs(const esp_littlefs_vfs_mount_conf_t *conf, esp_littlefs_vlfs_t **vlfsArg) {
    esp_littlefs_vlfs_t *vlfs = malloc(sizeof(esp_littlefs_vlfs_t));
    if (vlfs == NULL)
        return ESP_ERR_NO_MEM;
    vlfs->conf = *conf;

    // TODO: figure out what the cache does
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
            .write_p     = &vfs_littlefs_write,
            .pwrite_p    = &vfs_littlefs_pwrite,
            .lseek_p     = &vfs_littlefs_lseek,
            .read_p      = &vfs_littlefs_read,
            .pread_p     = &vfs_littlefs_pread,
            .open_p      = &vfs_littlefs_open,
            .close_p     = &vfs_littlefs_close,
#ifndef CONFIG_LITTLEFS_USE_ONLY_HASH
            .fstat_p     = &vfs_littlefs_fstat,
#else
            .fstat_p     = NULL, /* Not supported */
#endif
            .stat_p      = &vfs_littlefs_stat,
            .link_p      = NULL, /* Not Supported */
            .unlink_p    = &vfs_littlefs_unlink,
            .rename_p    = &vfs_littlefs_rename,
            .opendir_p   = &vfs_littlefs_opendir,
            .closedir_p  = &vfs_littlefs_closedir,
            .readdir_p   = &vfs_littlefs_readdir,
            .readdir_r_p = &vfs_littlefs_readdir_r,
            .seekdir_p   = &vfs_littlefs_seekdir,
            .telldir_p   = &vfs_littlefs_telldir,
            .mkdir_p     = &vfs_littlefs_mkdir,
            .rmdir_p     = &vfs_littlefs_rmdir,
            .fsync_p     = &vfs_littlefs_fsync,
#if CONFIG_LITTLEFS_USE_MTIME
            .utime_p     = &vfs_littlefs_utime,
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

    // TODO: IMPL

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

    // TODO: IMPL

    ret:
    xSemaphoreGive(vlfs_list_lock);
    return ret;
}
