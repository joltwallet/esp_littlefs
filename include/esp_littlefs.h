#ifndef ESP_LITTLEFS_H__
#define ESP_LITTLEFS_H__

#include "sdkconfig.h"
#include "esp_err.h"
#include "esp_idf_version.h"
#include <stdbool.h>
#include "esp_partition.h"

#ifdef CONFIG_LITTLEFS_SDMMC_SUPPORT
#include <sdmmc_cmd.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define ESP_LITTLEFS_VERSION_NUMBER "1.20.2"
#define ESP_LITTLEFS_VERSION_MAJOR 1
#define ESP_LITTLEFS_VERSION_MINOR 20
#define ESP_LITTLEFS_VERSION_PATCH 2

#ifdef ESP8266
// ESP8266 RTOS SDK default enables VFS DIR support
#define CONFIG_VFS_SUPPORT_DIR 1
#endif

#if CONFIG_VFS_SUPPORT_DIR
#define ESP_LITTLEFS_ENABLE_FTRUNCATE
#endif // CONFIG_VFS_SUPPORT_DIR

/**
 *Configuration structure for esp_vfs_littlefs_register.
 */
typedef struct {
    const char *base_path;            /**< Mounting point. */
    const char *partition_label;      /**< Label of partition to use. If partition_label, partition, and sdcard are all NULL,
                                           then the first partition with data subtype 'littlefs' will be used. */
    const esp_partition_t* partition; /**< partition to use if partition_label is NULL */

#ifdef CONFIG_LITTLEFS_SDMMC_SUPPORT
    sdmmc_card_t *sdcard;       /**< SD card handle to use if both esp_partition handle & partition label is NULL */
#endif

    uint8_t format_if_mount_failed:1; /**< Format the file system if it fails to mount. */
    uint8_t read_only : 1;            /**< Mount the partition as read-only. */
    uint8_t dont_mount:1;             /**< Don't attempt to mount.*/
    uint8_t grow_on_mount:1;          /**< Grow filesystem to match partition size on mount.*/
} esp_vfs_littlefs_conf_t;


/**
 * Initialize and mount LittleFS for LVGL integration.
 *
 * This helper initializes a LittleFS instance using the given configuration
 * and returns a pointer to the underlying lfs_t object, allowing direct access
 * for LVGL file system operations.
 *
 * Unlike esp_vfs_littlefs_register(), this function does not register
 * the filesystem to ESP-IDF VFS. It is designed for use cases where LVGL
 * manages file access directly through lv_fs API.
 *
 * Typical usage:
 * @code
 * esp_vfs_littlefs_conf_t conf = {
 *     .base_path = BSP_LITTLEFS_MOUNT_POINT,
 *     .partition_label = BSP_LITTLEFS_PARTITION_LABEL,
 *     .format_if_mount_failed = true,
 *     .dont_mount = false,
 * };
 *
 * lv_fs_littlefs_init();
 * lfs_t *lfs = esp_littlefs_lvgl_port_init(&conf);
 * if (lfs != NULL) {
 *     lv_littlefs_set_handler(lfs);
 *     lv_fs_file_t file;
 *     lv_fs_res_t ret = lv_fs_open(&file, "A:/example.txt", LV_FS_MODE_RD);
 *     if (ret == LV_FS_RES_OK) {
 *         char buffer[100];
 *         uint32_t bytes;
 *         lv_fs_read(&file, buffer, sizeof(buffer)-1, &bytes);
 *         buffer[bytes] = '\0';
 *         printf("%s\n", buffer);
 *     }
 * }
 * @endcode
 *
 * @param   conf                      Pointer to esp_vfs_littlefs_conf_t configuration structure
 *
 * @return  
 *          - Pointer to lfs_t        if initialization and mount succeed  
 *          - NULL                    if initialization or mount fails
 */
lfs_t * esp_littlefs_lvgl_port_init(const esp_vfs_littlefs_conf_t * conf);

/**
 * Register and mount (if configured to) littlefs to VFS with given path prefix.
 *
 * @param   conf                      Pointer to esp_vfs_littlefs_conf_t configuration structure
 *
 * @return  
 *          - ESP_OK                  if success
 *          - ESP_ERR_NO_MEM          if objects could not be allocated
 *          - ESP_ERR_INVALID_STATE   if already mounted or partition is encrypted
 *          - ESP_ERR_NOT_FOUND       if partition for littlefs was not found
 *          - ESP_FAIL                if mount or format fails
 */
esp_err_t esp_vfs_littlefs_register(const esp_vfs_littlefs_conf_t * conf);

/**
 * Unregister and unmount littlefs from VFS
 *
 * @param partition_label  Label of the partition to unregister.
 *
 * @return  
 *          - ESP_OK if successful
 *          - ESP_ERR_INVALID_STATE already unregistered
 */
esp_err_t esp_vfs_littlefs_unregister(const char* partition_label);

#ifdef CONFIG_LITTLEFS_SDMMC_SUPPORT
/**
 * Unregister and unmount LittleFS from VFS for SD card
 *
 * @param sdcard  SD card to unregister.
 *
 * @return
 *          - ESP_OK if successful
 *          - ESP_ERR_INVALID_STATE already unregistered
 */
esp_err_t esp_vfs_littlefs_unregister_sdmmc(sdmmc_card_t *sdcard);
#endif

/**
 * Unregister and unmount littlefs from VFS
 *
 * @param partition  partition to unregister.
 *
 * @return  
 *          - ESP_OK if successful
 *          - ESP_ERR_INVALID_STATE already unregistered
 */
esp_err_t esp_vfs_littlefs_unregister_partition(const esp_partition_t* partition);

/**
 * Check if littlefs is mounted
 *
 * @param partition_label  Label of the partition to check.
 *
 * @return  
 *          - true    if mounted
 *          - false   if not mounted
 */
bool esp_littlefs_mounted(const char* partition_label);

/**
 * Check if littlefs is mounted
 *
 * @param partition  partition to check.
 *
 * @return  
 *          - true    if mounted
 *          - false   if not mounted
 */
bool esp_littlefs_partition_mounted(const esp_partition_t* partition);

#ifdef CONFIG_LITTLEFS_SDMMC_SUPPORT
/**
 * Check if littlefs is mounted
 *
 * @param sdcard  SD card to check.
 *
 * @return
 *          - true    if mounted
 *          - false   if not mounted
 */
bool esp_littlefs_sdmmc_mounted(sdmmc_card_t *sdcard);
#endif

/**
 * Format the littlefs partition
 *
 * @param partition_label  Label of the partition to format.
 * @return  
 *          - ESP_OK      if successful
 *          - ESP_FAIL    on error
 */
esp_err_t esp_littlefs_format(const char* partition_label);

/**
 * Format the littlefs partition
 *
 * @param partition  partition to format.
 * @return
 *          - ESP_OK      if successful
 *          - ESP_FAIL    on error
 */
esp_err_t esp_littlefs_format_partition(const esp_partition_t* partition);

#ifdef CONFIG_LITTLEFS_SDMMC_SUPPORT
/**
 * Format the LittleFS on a SD card
 *
 * @param sdcard SD card to format
 * @return
 *          - ESP_OK      if successful
 *          - ESP_FAIL    on error
 */
esp_err_t esp_littlefs_format_sdmmc(sdmmc_card_t *sdcard);
#endif

/**
 * Get information for littlefs
 *
 * @param partition_label           Optional, label of the partition to get info for.
 * @param[out] total_bytes          Size of the file system
 * @param[out] used_bytes           Current used bytes in the file system
 *
 * @return
 *          - ESP_OK                  if success
 *          - ESP_ERR_INVALID_STATE   if not mounted
 */
esp_err_t esp_littlefs_info(const char* partition_label, size_t* total_bytes, size_t* used_bytes);

/**
 * Get information for littlefs
 *
 * @param parition                  the partition to get info for.
 * @param[out] total_bytes          Size of the file system
 * @param[out] used_bytes           Current used bytes in the file system
 *
 * @return  
 *          - ESP_OK                  if success
 *          - ESP_ERR_INVALID_STATE   if not mounted
 */
esp_err_t esp_littlefs_partition_info(const esp_partition_t* partition, size_t *total_bytes, size_t *used_bytes);

#ifdef CONFIG_LITTLEFS_SDMMC_SUPPORT
/**
 * Get information for littlefs on SD card
 *
 * @param[in] sdcard                    the SD card to get info for.
 * @param[out] total_bytes          Size of the file system
 * @param[out] used_bytes           Current used bytes in the file system
 *
 * @return
 *          - ESP_OK                  if success
 *          - ESP_ERR_INVALID_STATE   if not mounted
 */
esp_err_t esp_littlefs_sdmmc_info(sdmmc_card_t *sdcard, size_t *total_bytes, size_t *used_bytes);
#endif

#ifdef __cplusplus
} // extern "C"
#endif

#endif
