// Tests for LittleFS mounted via esp_blockdev handle
#include "test_littlefs_common.h"
#include "esp_partition.h"
#include "esp_blockdev.h"
#include "spi_flash_mmap.h"
#include "esp_flash.h"

static esp_partition_t get_test_data_static_partition(void);
static void            test_setup_bdl(const esp_partition_t* partition, bool read_only);
static void            test_teardown_bdl(void);

static esp_blockdev_handle_t s_bdl_handle = NULL;

static esp_partition_t get_test_data_static_partition(void)
{
    /* Construct a partition descriptor for the embedded testfs binary */
    extern const uint8_t partition_blob_start[] asm("_binary_testfs_bin_start");
    extern const uint8_t partition_blob_end[] asm("_binary_testfs_bin_end");

    return (esp_partition_t){
        .flash_chip = esp_flash_default_chip,
        .type       = ESP_PARTITION_TYPE_DATA,
        .subtype    = ESP_PARTITION_SUBTYPE_DATA_FAT,
        .address    = spi_flash_cache2phys(partition_blob_start),
        .size       = ((uintptr_t)partition_blob_end) - ((uintptr_t)partition_blob_start),
        .erase_size = SPI_FLASH_SEC_SIZE, /* match flash sector & embedded LittleFS image block size */
        .label      = "",
        .encrypted  = false,
        .readonly   = false,
    };
}

static void test_setup_bdl(const esp_partition_t* partition, bool read_only)
{
    TEST_ESP_OK(esp_partition_ptr_get_blockdev(partition, &s_bdl_handle));

    const esp_vfs_littlefs_conf_t conf = {
        .base_path  = littlefs_base_path,
        .partition_label = NULL,
        .partition  = NULL,
        .blockdev   = s_bdl_handle,
        .dont_mount = false,
        .read_only  = read_only,
    };

    TEST_ESP_OK(esp_vfs_littlefs_register(&conf));
    TEST_ASSERT_TRUE(heap_caps_check_integrity_all(true));
    TEST_ASSERT_TRUE(esp_littlefs_blockdev_mounted(s_bdl_handle));
    printf("BDL test setup complete.\n");
}

static void test_teardown_bdl(void)
{
    TEST_ESP_OK(esp_vfs_littlefs_unregister_blockdev(s_bdl_handle));
    TEST_ASSERT_TRUE(heap_caps_check_integrity_all(true));
    s_bdl_handle = NULL; // released by unregister
    printf("BDL test teardown complete.\n");
}

static const esp_partition_t* get_rw_partition(void)
{
    const esp_partition_t *part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA,
        ESP_PARTITION_SUBTYPE_DATA_LITTLEFS,
        littlefs_test_partition_label);
    if (!part) {
        part = esp_partition_find_first(
            ESP_PARTITION_TYPE_DATA,
            ESP_PARTITION_SUBTYPE_ANY,
            littlefs_test_partition_label);
    }
    TEST_ASSERT_NOT_NULL_MESSAGE(part, "littlefs test partition not found");
    return part;
}

static void test_setup_bdl_rw(void)
{
    const esp_partition_t *part = get_rw_partition();
    /* Format via partition API to ensure clean media before blockdev mount */
    TEST_ESP_OK(esp_littlefs_format_partition(part));

    TEST_ESP_OK(esp_partition_ptr_get_blockdev(part, &s_bdl_handle));

    const esp_vfs_littlefs_conf_t conf = {
        .base_path  = littlefs_base_path,
        .partition_label = NULL,
        .partition  = NULL,
        .blockdev   = s_bdl_handle,
        .dont_mount = false,
        .read_only  = false,
        .format_if_mount_failed = true,
    };

    TEST_ESP_OK(esp_vfs_littlefs_register(&conf));
    TEST_ASSERT_TRUE(heap_caps_check_integrity_all(true));
    TEST_ASSERT_TRUE(esp_littlefs_blockdev_mounted(s_bdl_handle));
    printf("BDL RW setup complete.\n");
}

static void test_teardown_bdl_rw(void)
{
    TEST_ESP_OK(esp_vfs_littlefs_unregister_blockdev(s_bdl_handle));
    TEST_ASSERT_TRUE(heap_caps_check_integrity_all(true));
    s_bdl_handle = NULL;
    printf("BDL RW teardown complete.\n");
}

TEST_CASE("bdl can initialize LittleFS in erased partition", "[littlefs_bdl]")
{
    const esp_partition_t *part = get_rw_partition();
    TEST_ESP_OK(esp_littlefs_format_partition(part));
    test_setup_bdl_rw();

    size_t total = 0, used = 0;
    TEST_ESP_OK(esp_littlefs_blockdev_info(s_bdl_handle, &total, &used));
    TEST_ASSERT_GREATER_THAN(0, total);

    test_teardown_bdl_rw();
}

TEST_CASE("bdl can read file", "[littlefs_bdl]")
{
    const esp_partition_t partition = get_test_data_static_partition();
    test_setup_bdl(&partition, true);

    test_littlefs_read_file_with_content(littlefs_base_path "/test1.txt", "test1");
    test_littlefs_read_file_with_content(littlefs_base_path "/test2.txt", "test2");
    test_littlefs_read_file_with_content(littlefs_base_path "/pangram.txt", "The quick brown fox jumps over the lazy dog");

    test_teardown_bdl();
}

TEST_CASE("bdl cannot create file when mounted read-only", "[littlefs_bdl]")
{
    const esp_partition_t partition = get_test_data_static_partition();
    test_setup_bdl(&partition, true);

    FILE* f = fopen(littlefs_base_path "/new_file.txt", "wb");
    TEST_ASSERT_NULL(f);

    test_teardown_bdl();
}

TEST_CASE("bdl cannot delete existing file when mounted read-only", "[littlefs_bdl]")
{
    const esp_partition_t partition = get_test_data_static_partition();
    test_setup_bdl(&partition, true);

    TEST_ASSERT_EQUAL(-1, unlink(littlefs_base_path "/test1.txt"));
    test_littlefs_read_file_with_content(littlefs_base_path "/test1.txt", "test1");

    test_teardown_bdl();
}

TEST_CASE("bdl can create and read file", "[littlefs_bdl]")
{
    test_setup_bdl_rw();

    const char *fn = littlefs_base_path "/hello.txt";
    test_littlefs_create_file_with_text(fn, littlefs_test_hello_str);
    test_littlefs_read_file(fn);

    test_teardown_bdl_rw();
}

TEST_CASE("bdl cannot write existing file when mounted read-only", "[littlefs_bdl]")
{
    const esp_partition_t partition = get_test_data_static_partition();
    test_setup_bdl(&partition, true);

    FILE* f = fopen(littlefs_base_path "/test1.txt", "wb");
    TEST_ASSERT_NULL(f);
    test_littlefs_read_file_with_content(littlefs_base_path "/test1.txt", "test1");

    test_teardown_bdl();
}

TEST_CASE("bdl can format mounted partition", "[littlefs_bdl]")
{
    const esp_partition_t *part = get_rw_partition();
    /* start clean */
    TEST_ESP_OK(esp_littlefs_format_partition(part));
    test_setup_bdl_rw();

    /* write a file then format via BDL path */
    const char *fn = littlefs_base_path "/hello.txt";
    test_littlefs_create_file_with_text(fn, littlefs_test_hello_str);

    /* unmount then format underlying partition */
    test_teardown_bdl_rw();
    TEST_ESP_OK(esp_littlefs_format_partition(part));

    /* mount again and ensure file is gone */
    test_setup_bdl_rw();
    FILE *f = fopen(fn, "r");
    TEST_ASSERT_NULL(f);

    test_teardown_bdl_rw();
}
