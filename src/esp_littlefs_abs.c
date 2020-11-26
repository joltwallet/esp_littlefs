#include "esp_littlefs_abs_priv.h"

#include <string.h>

#include "freertos/semphr.h"

#include "esp_partition.h"
#include "esp_log.h"

static const char *const TAG = ESP_LITTLEFS_ABS_TAG;

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
 * Grows the vlfs_list to be able to store at least newCap elements.
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
    while (vlfs_index < vlfs_list_cap && &vlfs_list[vlfs_index]->lfs != lfs)
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

    // unmount the fs (release its allocated resources)
    if (lfs_unmount(&vlfs->lfs) < 0)
        ESP_LOGE(TAG, "Error unmounting littlefs!");

    // free the context
    if (vlfs->free_ctx != NULL)
        vlfs->free_ctx(&vlfs->cfg.context);

    // remove the vlfs from the vlfs_list
    vlfs_list_remove(vlfs);

    free(vlfs);

    *vlfsArg = NULL;
}

/**
 * @warning This must be called with the vlfs_list_lock taken
 */
static esp_err_t create_vlfs(esp_littlefs_vlfs_t **vlfsArg, struct lfs_config * config, bool format_on_error, void (*free_ctx)(void *)) {
    esp_littlefs_vlfs_t *vlfs = malloc(sizeof(esp_littlefs_vlfs_t));
    if (vlfs == NULL)
        return ESP_ERR_NO_MEM;

    vlfs->cfg = *config;
    vlfs->free_ctx = free_ctx;

    // init fs
    if (lfs_mount(&vlfs->lfs, &vlfs->cfg) < 0) {
        if (format_on_error) {
            if (lfs_format(&vlfs->lfs, &vlfs->cfg) < 0) {
                ESP_LOGE(TAG, "Failed to format!");
                free(vlfs);
                return ESP_FAIL;
            }
            if (lfs_mount(&vlfs->lfs, &vlfs->cfg) < 0) {
                ESP_LOGE(TAG, "Mount after format failed!");
                free(vlfs);
                return ESP_FAIL;
            }
        } else {
            free(vlfs);
            return ESP_FAIL;
        }
    }

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

// endregion

// region public api

esp_err_t esp_littlefs_abs_create(lfs_t ** lfs, struct lfs_config * config, bool format_on_error, void (*free_ctx)(void *)) {

}
esp_err_t esp_littlefs_abs_delete(lfs_t * lfs) {

}
esp_err_t esp_littlefs_abs_is(lfs_t * lfs) {

}
esp_err_t esp_littlefs_abs_info(lfs_t *lfs, size_t *total_bytes, size_t *used_bytes) {

}

// endregion
