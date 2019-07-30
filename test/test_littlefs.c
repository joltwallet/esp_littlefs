#include "esp_littlefs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <sys/unistd.h>
#include "unity.h"
#include "test_utils.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_vfs.h"
#include "esp_littlefs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_partition.h"


static const char littlefs_test_partition_label[] = "flash_test";
const char* littlefs_test_hello_str = "Hello, World!\n";

static void test_littlefs_create_file_with_text(const char* name, const char* text);
static void test_littlefs_overwrite_append(const char* filename);
void test_littlefs_read_file(const char* filename);
void test_littlefs_readdir_many_files(const char* dir_prefix);
static void test_setup();
static void test_teardown();

TEST_CASE("can initialize LittleFS in erased partition", "[littlefs]")
{
    /* Gets the partition labeled "flash_test" */
    const esp_partition_t* part = get_test_data_partition();
    TEST_ASSERT_NOT_NULL(part);
    TEST_ESP_OK(esp_partition_erase_range(part, 0, part->size));
    test_setup();
    size_t total = 0, used = 0;
    TEST_ESP_OK(esp_littlefs_info(littlefs_test_partition_label, &total, &used));
    printf("total: %d, used: %d\n", total, used);
    TEST_ASSERT_EQUAL(0, used);
    test_teardown();
}

TEST_CASE("can format mounted partition", "[littlefs]")
{
    // Mount LittleFS, create file, format, check that the file does not exist.
    const esp_partition_t* part = get_test_data_partition();
    TEST_ASSERT_NOT_NULL(part);
    test_setup();
    const char* filename = "/littlefs/hello.txt";
    test_littlefs_create_file_with_text(filename, littlefs_test_hello_str);
    esp_littlefs_format(part->label);
    FILE* f = fopen(filename, "r");
    TEST_ASSERT_NULL(f);
    test_teardown();
}

TEST_CASE("can format unmounted partition", "[littlefs]")
{
    // Mount LittleFS, create file, unmount. Format. Mount again, check that
    // the file does not exist.
    const esp_partition_t* part = get_test_data_partition();
    TEST_ASSERT_NOT_NULL(part);
    test_setup();
    const char* filename = "/littlefs/hello.txt";
    test_littlefs_create_file_with_text(filename, littlefs_test_hello_str);
    test_teardown();
    esp_littlefs_format(part->label);
    // Don't use test_setup here, need to mount without formatting
    esp_vfs_littlefs_conf_t conf = {
        .base_path = "/littlefs",
        .partition_label = littlefs_test_partition_label,
        .max_files = 5,
        .format_if_mount_failed = false
    };
    TEST_ESP_OK(esp_vfs_littlefs_register(&conf));
    FILE* f = fopen(filename, "r");
    TEST_ASSERT_NULL(f);
    test_teardown();
}

TEST_CASE("can create and write file", "[littlefs]")
{
    test_setup();
    test_littlefs_create_file_with_text("/littlefs/hello.txt", littlefs_test_hello_str);
    test_teardown();
}

TEST_CASE("can read file", "[littlefs]")
{
    test_setup();
    test_littlefs_create_file_with_text("/littlefs/hello.txt", littlefs_test_hello_str);
    test_littlefs_read_file("/littlefs/hello.txt");
    test_teardown();
}

TEST_CASE("can open maximum number of files", "[littlefs]")
{
    size_t max_files = FOPEN_MAX - 3; /* account for stdin, stdout, stderr */
    esp_vfs_littlefs_conf_t conf = {
        .base_path = "/little",
        .partition_label = littlefs_test_partition_label,
        .format_if_mount_failed = true,
        .max_files = max_files
    };
    TEST_ESP_OK(esp_vfs_littlefs_register(&conf));
    //test_littlefs_open_max_files("/littlefs/f", max_files);
    assert(0); // TODO
    TEST_ESP_OK(esp_vfs_littlefs_unregister(littlefs_test_partition_label));
}

TEST_CASE("overwrite and append file", "[littlefs]")
{
    test_setup();
    test_littlefs_overwrite_append("/littlefs/hello.txt");
    test_teardown();
}

TEST_CASE("can lseek", "[littlefs]")
{
    test_setup();
    //test_littlefs_lseek("/littlefs/seek.txt");
    assert(0); // TODO
    test_teardown();
}


TEST_CASE("stat returns correct values", "[littlefs]")
{
    test_setup();
    //test_littlefs_stat("/littlefs/stat.txt");
    assert(0); // TODO
    test_teardown();
}

TEST_CASE("unlink removes a file", "[littlefs]")
{
    test_setup();
    //test_littlefs_unlink("/littlefs/unlink.txt");
    assert(0); // TODO
    test_teardown();
}

TEST_CASE("rename moves a file", "[littlefs]")
{
    test_setup();
    //test_littlefs_rename("/littlefs/move");
    assert(0); // TODO
    test_teardown();
}

TEST_CASE("can opendir root directory of FS", "[littlefs]")
{
    test_setup();
    //test_littlefs_can_opendir("/littlefs");
    assert(0); // TODO
    test_teardown();
}

TEST_CASE("opendir, readdir, rewinddir, seekdir work as expected", "[littlefs]")
{
    test_setup();
    //test_littlefs_opendir_readdir_rewinddir("/littlefs/dir");
    assert(0); // TODO
    test_teardown();
}

TEST_CASE("readdir with large number of files", "[littlefs][timeout=30]")
{
    test_setup();
    //test_littlefs_readdir_many_files("/littlefs/dir2");
    assert(0); // TODO
    test_teardown();
}

TEST_CASE("multiple tasks can use same volume", "[littlefs]")
{
    test_setup();
    //test_littlefs_concurrent("/littlefs/f");
    assert(0); // TODO
    test_teardown();
}


static void test_littlefs_create_file_with_text(const char* name, const char* text)
{
    FILE* f = fopen(name, "wb");
    TEST_ASSERT_NOT_NULL(f);
    TEST_ASSERT_TRUE(fputs(text, f) != EOF);
    TEST_ASSERT_EQUAL(0, fclose(f));
}

static void test_littlefs_overwrite_append(const char* filename)
{
    /* Create new file with 'aaaa' */
    test_littlefs_create_file_with_text(filename, "aaaa");

    /* Append 'bbbb' to file */
    FILE *f_a = fopen(filename, "a");
    TEST_ASSERT_NOT_NULL(f_a);
    TEST_ASSERT_NOT_EQUAL(EOF, fputs("bbbb", f_a));
    TEST_ASSERT_EQUAL(0, fclose(f_a));

    /* Read back 8 bytes from file, verify it's 'aaaabbbb' */
    char buf[10] = { 0 };
    FILE *f_r = fopen(filename, "r");
    TEST_ASSERT_NOT_NULL(f_r);
    TEST_ASSERT_EQUAL(8, fread(buf, 1, 8, f_r));
    TEST_ASSERT_EQUAL_STRING_LEN("aaaabbbb", buf, 8);

    /* Be sure we're at end of file */
    TEST_ASSERT_EQUAL(0, fread(buf, 1, 8, f_r));

    TEST_ASSERT_EQUAL(0, fclose(f_r));

    /* Overwrite file with 'cccc' */
    test_littlefs_create_file_with_text(filename, "cccc");

    /* Verify file now only contains 'cccc' */
    f_r = fopen(filename, "r");
    TEST_ASSERT_NOT_NULL(f_r);
    bzero(buf, sizeof(buf));
    TEST_ASSERT_EQUAL(4, fread(buf, 1, 8, f_r)); // trying to read 8 bytes, only expecting 4
    TEST_ASSERT_EQUAL_STRING_LEN("cccc", buf, 4);
    TEST_ASSERT_EQUAL(0, fclose(f_r));
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

void test_littlefs_readdir_many_files(const char* dir_prefix)
{
    const int n_files = 40;
    const int n_folders = 4;
    unsigned char file_count[n_files * n_folders];
    memset(file_count, 0, sizeof(file_count)/sizeof(file_count[0]));
    char file_name[ESP_VFS_PATH_MAX + CONFIG_LITTLEFS_OBJ_NAME_LEN];

    /* clean stale files before the test */
    DIR* dir = opendir(dir_prefix);
    if (dir) {
        while (true) {
            struct dirent* de = readdir(dir);
            if (!de) {
                break;
            }
            int len = snprintf(file_name, sizeof(file_name), "%s/%s", dir_prefix, de->d_name);
            assert(len < sizeof(file_name));
            unlink(file_name);
        }
    }

    /* create files */
    for (int d = 0; d < n_folders; ++d) {
        printf("filling directory %d\n", d);
        for (int f = 0; f < n_files; ++f) {
            snprintf(file_name, sizeof(file_name), "%s/%d/%d.txt", dir_prefix, d, f);
            test_littlefs_create_file_with_text(file_name, file_name);
        }
    }

    /* list files */
    for (int d = 0; d < n_folders; ++d) {
        printf("listing files in directory %d\n", d);
        snprintf(file_name, sizeof(file_name), "%s/%d", dir_prefix, d);
        dir = opendir(file_name);
        TEST_ASSERT_NOT_NULL(dir);
        while (true) {
            struct dirent* de = readdir(dir);
            if (!de) {
                break;
            }
            int file_id;
            TEST_ASSERT_EQUAL(1, sscanf(de->d_name, "%d.txt", &file_id));
            file_count[file_id + d * n_files]++;
        }
        closedir(dir);
    }

    /* check that all created files have been seen */
    for (int d = 0; d < n_folders; ++d) {
        printf("checking that all files have been found in directory %d\n", d);
        for (int f = 0; f < n_files; ++f) {
            TEST_ASSERT_EQUAL(1, file_count[f + d * n_files]);
        }
    }
}

static void test_setup() {
    esp_vfs_littlefs_conf_t conf = {
        .base_path = "/littlefs",
        .partition_label = littlefs_test_partition_label,
        .max_files = 5,
        .format_if_mount_failed = true
    };
    TEST_ESP_OK(esp_vfs_littlefs_register(&conf));
}

static void test_teardown(){
    TEST_ESP_OK(esp_vfs_littlefs_unregister(littlefs_test_partition_label));
}

