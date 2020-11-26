#include "esp_littlefs_flash_priv.h"

#include <string.h>

#include "freertos/semphr.h"

#include "esp_partition.h"
#include "esp_log.h"

static const char *const TAG = ESP_LITTLEFS_FLASH_TAG;

// region public api

esp_err_t esp_littlefs_flash_create(const char * partition_label, lfs_t ** lfs, bool format_on_error) {

    // get partition details
    if ( NULL == conf->flash_conf.partition_label ) {
        ESP_LOGE(TAG, "Partition label must be provided.");
        err = ESP_ERR_INVALID_ARG;
        goto exit;
    }

    partition = esp_partition_find_first(
            ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY,
            conf->flash_conf.partition_label);

    if (!partition) {
        ESP_LOGE(TAG, "partition \"%s\" could not be found", conf->flash_conf.partition_label);
        err = ESP_ERR_NOT_FOUND;
        goto exit;
    }
}
esp_err_t esp_littlefs_flash_delete(lfs_t * lfs) {

}
esp_err_t esp_littlefs_flash_is(lfs_t * lfs) {

}
esp_err_t esp_littlefs_flash_format(const char * partition_label) {
    ESP_LOGV(TAG, "Erasing partition...");

    const esp_partition_t* partition = esp_partition_find_first(
            ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY,
            partition_label);
    if (!partition) {
        ESP_LOGE(TAG, "partition \"%s\" could not be found", partition_label);
        return ESP_ERR_NOT_FOUND;
    }

    if( esp_partition_erase_range(partition, 0, partition->size) != ESP_OK ) {
        ESP_LOGE(TAG, "Failed to erase partition");
        return ESP_FAIL;
    }

    return ESP_OK;
}

// endregion
