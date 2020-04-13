#include "esp_err.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_vfs.h"
#include "esp_vfs_fat.h"
#include "esp_littlefs.h"
#include "unity.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_partition.h"

static const char TAG[] = "[benchmark]";

// Handle of the wear levelling library instance
wl_handle_t s_wl_handle = WL_INVALID_HANDLE;
#define MAX_FILES 5

static void setup_spiffs(){
    ESP_LOGI(TAG, "Initializing SPIFFS");

    esp_vfs_spiffs_conf_t conf = {
      .base_path = "/spiffs",
      .partition_label = "spiffs_store",
      .max_files = MAX_FILES,
      .format_if_mount_failed = true
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        TEST_FAIL();
        return;
    }
}

static void setup_fat(){
    const esp_vfs_fat_mount_config_t conf = {
            .max_files = MAX_FILES,
            .format_if_mount_failed = true,
            .allocation_unit_size = CONFIG_WL_SECTOR_SIZE
    };
    esp_err_t err = esp_vfs_fat_spiflash_mount("/fat", "fat_store", &conf, &s_wl_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount FATFS (%s)", esp_err_to_name(err));
        TEST_FAIL();
        return;
    }
}

static void setup_littlefs() {
    esp_vfs_littlefs_conf_t conf = {
        .base_path = "/littlefs",
        .partition_label = "flash_test",
        .format_if_mount_failed = true
    };
    TEST_ESP_OK(esp_vfs_littlefs_register(&conf));
}

static void test_setup() {
    setup_fat();
    setup_spiffs();
    setup_littlefs();
    printf("Test setup complete.\n");
}

static void test_teardown()
{
    assert(ESP_OK == esp_vfs_fat_spiflash_unmount("/fat", s_wl_handle));
    TEST_ESP_OK(esp_vfs_spiffs_unregister("spiffs_store"));
    TEST_ESP_OK(esp_vfs_littlefs_unregister("flash_test"));
    printf("Test teardown complete.\n");
}

/**
 * @brief Fill partitions with dummy data
 */
static void fill_partitions()
{
    const esp_partition_t* part;
    const char dummy_data[] = {
        'D','U','M','M','Y','D','A','T','A','0','1','2', '3', '4', '5', '6',
        'D','U','M','M','Y','D','A','T','A','0','1','2', '3', '4', '5', '6',
        'D','U','M','M','Y','D','A','T','A','0','1','2', '3', '4', '5', '6',
        'D','U','M','M','Y','D','A','T','A','0','1','2', '3', '4', '5', '6',
        'D','U','M','M','Y','D','A','T','A','0','1','2', '3', '4', '5', '6',
        'D','U','M','M','Y','D','A','T','A','0','1','2', '3', '4', '5', '6',
        'D','U','M','M','Y','D','A','T','A','0','1','2', '3', '4', '5', '6',
        'D','U','M','M','Y','D','A','T','A','0','1','2', '3', '4', '5', '6'
    };

    ESP_LOGI(TAG, "Filling SPIFFS partition with dummy data");
    part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY,
            "spiffs_store");
    esp_partition_erase_range(part, 0, part->size);
    for(uint32_t i=0; i < part->size; i += sizeof(dummy_data))
        esp_partition_write(part, i, dummy_data, sizeof(dummy_data));

    ESP_LOGI(TAG, "Filling FAT partition with dummy data");
    part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY,
            "fat_store");
    esp_partition_erase_range(part, 0, part->size);
    for(uint32_t i=0; i < part->size; i += sizeof(dummy_data))
        esp_partition_write(part, i, dummy_data, sizeof(dummy_data));

    ESP_LOGI(TAG, "Filling LittleFS partition with dummy data");
    part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY,
            "flash_test");
    esp_partition_erase_range(part, 0, part->size);
    for(uint32_t i=0; i < part->size; i += sizeof(dummy_data))
        esp_partition_write(part, i, dummy_data, sizeof(dummy_data));
}

static size_t get_file_size(const char *fname) {
    struct stat sb;

    if( 0 != stat(fname, &sb) ) {
        return -1;
    }

    return sb.st_size;
}

/**
 * @brief Writes and deletes files
 * @param[in] mount_pt
 * @param[in] iter Number of files to write
 */
static void write_test_1(const char *mount_pt, uint32_t iter) {
    char fmt_fn[64] = { 0 };
    char fname[128] = { 0 };
    uint64_t t_write = 0;
    uint64_t t_delete = 0;

    strcat(fmt_fn, mount_pt);
    if(fmt_fn[strlen(fmt_fn)-1] != '/') strcat(fmt_fn, "/");
    strcat(fmt_fn, "%d.txt");

    printf("\n");

    for(uint8_t i=0; i < iter; i++){
        snprintf(fname, sizeof(fname), fmt_fn, i);
        uint64_t t_start = esp_timer_get_time();
        FILE* f = fopen(fname, "w");
        if (f == NULL) {
            ESP_LOGE(TAG, "Failed to open file for writing");
            return;
        }
        for(uint32_t j=0; j < 2000; j++) {
            fprintf(f, "All work and no play makes Jack a dull boy.\n");
        }
        fclose(f);
        uint64_t t_end = esp_timer_get_time();
        size_t fsize = get_file_size(fname);
        printf("%d bytes written in %lld us\n", fsize, (t_end - t_start));
        t_write += (t_end - t_start);
    }

    printf("------------\n");

    for(uint8_t i=0; i < iter; i++){
        uint64_t t_start = esp_timer_get_time();
        snprintf(fname, sizeof(fname), fmt_fn, i);
        unlink(fname);
        uint64_t t_end = esp_timer_get_time();
        printf("deleted file %d in %lld us\n", i, (t_end - t_start));
        t_delete += (t_end - t_start);
    }

    printf("------------\n");

    printf("Total Write: %lld us\n", t_write);
    printf("Total Delete: %lld us\n", t_delete);
    printf("\n");

}


TEST_CASE("Format", TAG){
    uint64_t t_fat, t_spiffs, t_littlefs, t_start;

    fill_partitions();

    t_start = esp_timer_get_time();
    esp_spiffs_format(NULL);
    t_spiffs = esp_timer_get_time() - t_start;
    printf("SPIFFS Formatted in %lld us\n", t_spiffs);

    t_start = esp_timer_get_time();
    setup_fat();
    assert(ESP_OK == esp_vfs_fat_spiflash_unmount("/fat", s_wl_handle));
    t_fat = esp_timer_get_time() - t_start;
    printf("FAT Formatted in %lld us\n", t_fat);

    t_start = esp_timer_get_time();
    esp_littlefs_format("flash_test");
    t_littlefs = esp_timer_get_time() - t_start;
    printf("LittleFS Formatted in %lld us\n", t_littlefs);

}

TEST_CASE("Write 5 files, then delete 5 files", TAG){
    test_setup();
    printf("FAT:\n");
    write_test_1("/fat", 5);
    printf("SPIFFS:\n");
    write_test_1("/spiffs", 5);
    printf("LittleFS:\n");
    write_test_1("/littlefs", 5);
    test_teardown();
}
