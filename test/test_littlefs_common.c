#include "test_littlefs_common.h"


const char littlefs_test_partition_label[] = "flash_test";
const char littlefs_test_hello_str[] = "Hello, World!\n";


void test_littlefs_create_file_with_text(const char* name, const char* text)
{
    printf("Writing to \"%s\"\n", name);
    FILE* f = fopen(name, "wb");
    TEST_ASSERT_NOT_NULL(f);
    TEST_ASSERT_TRUE(fputs(text, f) != EOF);
    TEST_ASSERT_EQUAL(0, fclose(f));
}

void test_littlefs_read_file(const char* filename)
{
    FILE* f = fopen(filename, "r");
    TEST_ASSERT_NOT_NULL(f);
    char buf[32] = { 0 };
    int cb = fread(buf, 1, sizeof(buf), f);
    TEST_ASSERT_EQUAL(strlen(littlefs_test_hello_str), cb);
    TEST_ASSERT_EQUAL(0, strcmp(littlefs_test_hello_str, buf));
    TEST_ASSERT_EQUAL(0, fclose(f));
}


void test_setup() {
    esp_littlefs_format(littlefs_test_partition_label);
    const esp_vfs_littlefs_conf_t conf = {
        .base_path = littlefs_base_path,
        .partition_label = littlefs_test_partition_label,
        .format_if_mount_failed = true
    };
    TEST_ESP_OK(esp_vfs_littlefs_register(&conf));
    TEST_ASSERT_TRUE( heap_caps_check_integrity_all(true) );
    printf("Test setup complete.\n");
}

void test_teardown(){
    TEST_ESP_OK(esp_vfs_littlefs_unregister(littlefs_test_partition_label));
    TEST_ASSERT_TRUE( heap_caps_check_integrity_all(true) );
    printf("Test teardown complete.\n");
}
