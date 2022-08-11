//#define LOG_LOCAL_LEVEL 4

#include "esp_littlefs.h"
#include "sdkconfig.h"

#include <stdio.h>
#include <fcntl.h>
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
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"
#include "esp_partition.h"
#include "errno.h"
#include "esp_idf_version.h"

#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(4, 3, 0)
#define esp_rom_printf ets_printf
#else
#include "esp_rom_sys.h"
#endif


static const char littlefs_test_partition_label[] = "flash_test";
static const char littlefs_test_hello_str[] = "Hello, World!\n";
#define littlefs_base_path "/littlefs"

static void test_littlefs_write_file_with_offset(const char *filename);
static void test_littlefs_read_file_with_offset(const char *filename);
static void test_littlefs_create_file_with_text(const char* name, const char* text);
static void test_littlefs_overwrite_append(const char* filename);
static void test_littlefs_read_file(const char* filename);
static void test_littlefs_readdir_many_files(const char* dir_prefix);
static void test_littlefs_open_max_files(const char* filename_prefix, size_t files_count);
static void test_littlefs_concurrent_rw(const char* filename_prefix);
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
    TEST_ASSERT_EQUAL(8192, used); // 2 blocks are used on a fresh filesystem
    test_teardown();
}

TEST_CASE("can format mounted partition", "[littlefs]")
{
    // Mount LittleFS, create file, format, check that the file does not exist.
    const esp_partition_t* part = get_test_data_partition();
    TEST_ASSERT_NOT_NULL(part);
    test_setup();
    const char* filename = littlefs_base_path "/hello.txt";
    test_littlefs_create_file_with_text(filename, littlefs_test_hello_str);
    printf("Deleting \"%s\" via formatting fs.\n", filename);
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
    const char* filename = littlefs_base_path "/hello.txt";
    test_littlefs_create_file_with_text(filename, littlefs_test_hello_str);
    test_teardown();

    esp_littlefs_format(part->label);
    // Don't use test_setup here, need to mount without formatting
    const esp_vfs_littlefs_conf_t conf = {
        .base_path = littlefs_base_path,
        .partition_label = littlefs_test_partition_label,
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
    test_littlefs_create_file_with_text(littlefs_base_path "/hello.txt", littlefs_test_hello_str);
    test_teardown();
}

TEST_CASE("can read file", "[littlefs]")
{
    test_setup();
    test_littlefs_create_file_with_text(littlefs_base_path "/hello.txt", littlefs_test_hello_str);
    test_littlefs_read_file(littlefs_base_path "/hello.txt");
    test_teardown();
}

TEST_CASE("can write to file with offset (pwrite)", "[littlefs]")
{
    test_setup();
    test_littlefs_write_file_with_offset(littlefs_base_path "/hello.txt");
    test_teardown();
}

TEST_CASE("can read from file with offset (pread)", "[littlefs]")
{
    test_setup();
    test_littlefs_read_file_with_offset(littlefs_base_path "/hello.txt");
    test_teardown();
}

TEST_CASE("r+ mode read and write file", "[littlefs]")
{
    /* Note: despite some online resources, "r+" should not create a file
     * if it does not exist */

    const char fn[] = littlefs_base_path "/hello.txt";
    char buf[100] = { 0 };

    test_setup();

    test_littlefs_create_file_with_text(fn, "foo");
    
    /* Read back the previously written foo, and add bar*/
    {
        FILE* f = fopen(fn, "r+");
        TEST_ASSERT_NOT_NULL(f);
        TEST_ASSERT_EQUAL(3, fread(buf, 1, sizeof(buf), f));
        TEST_ASSERT_EQUAL_STRING("foo", buf);
        TEST_ASSERT_TRUE(fputs("bar", f) != EOF);
        TEST_ASSERT_EQUAL(0, fseek(f, 0, SEEK_SET));
        TEST_ASSERT_EQUAL(6, fread(buf, 1, 6, f));
        TEST_ASSERT_EQUAL_STRING("foobar", buf);
        TEST_ASSERT_EQUAL(0, fclose(f));
    }

    /* Just normal read the whole contents */
    {
        FILE* f = fopen(fn, "r+");
        TEST_ASSERT_NOT_NULL(f);
        TEST_ASSERT_EQUAL(6, fread(buf, 1, sizeof(buf), f));
        TEST_ASSERT_EQUAL_STRING("foobar", buf);
        TEST_ASSERT_EQUAL(0, fclose(f));
    }

    test_teardown();
}

TEST_CASE("w+ mode read and write file", "[littlefs]")
{
    const char fn[] = littlefs_base_path "/hello.txt";
    char buf[100] = { 0 };

    test_setup();

    test_littlefs_create_file_with_text(fn, "foo");

    /* this should overwrite the file and be readable */
    {
        FILE* f = fopen(fn, "w+");
        TEST_ASSERT_NOT_NULL(f);
        TEST_ASSERT_TRUE(fputs("bar", f) != EOF);
        TEST_ASSERT_EQUAL(0, fseek(f, 0, SEEK_SET));
        TEST_ASSERT_EQUAL(3, fread(buf, 1, sizeof(buf), f));
        TEST_ASSERT_EQUAL_STRING("bar", buf);
        TEST_ASSERT_EQUAL(0, fclose(f));
    }

    test_teardown();
}


TEST_CASE("can open maximum number of files", "[littlefs]")
{
    size_t max_files = 61;  /* account for stdin, stdout, stderr, esp-idf defaults to maximum 64 file descriptors */
    test_setup();
    test_littlefs_open_max_files("/littlefs/f", max_files);
    test_teardown();
}

TEST_CASE("overwrite and append file", "[littlefs]")
{
    test_setup();
    test_littlefs_overwrite_append(littlefs_base_path "/hello.txt");
    test_teardown();
}

TEST_CASE("can lseek", "[littlefs]")
{
    test_setup();

    FILE* f = fopen(littlefs_base_path "/seek.txt", "wb+");
    TEST_ASSERT_NOT_NULL(f);
    TEST_ASSERT_EQUAL(11, fprintf(f, "0123456789\n"));
    TEST_ASSERT_EQUAL(0, fseek(f, -2, SEEK_CUR));
    TEST_ASSERT_EQUAL('9', fgetc(f));
    TEST_ASSERT_EQUAL(0, fseek(f, 3, SEEK_SET));
    TEST_ASSERT_EQUAL('3', fgetc(f));
    TEST_ASSERT_EQUAL(0, fseek(f, -3, SEEK_END));
    TEST_ASSERT_EQUAL('8', fgetc(f));
    TEST_ASSERT_EQUAL(0, fseek(f, 0, SEEK_END));
    TEST_ASSERT_EQUAL(11, ftell(f));
    // Appending to end
    TEST_ASSERT_EQUAL(4, fprintf(f, "abc\n"));
    TEST_ASSERT_EQUAL(0, fseek(f, 0, SEEK_END));
    TEST_ASSERT_EQUAL(15, ftell(f));
    // Appending past end of file, creating a "hole"
    TEST_ASSERT_EQUAL(0, fseek(f, 2, SEEK_END));
    TEST_ASSERT_EQUAL(4, fprintf(f, "foo\n"));
    TEST_ASSERT_EQUAL(0, fseek(f, 0, SEEK_SET));
    char buf[32];
    TEST_ASSERT_EQUAL(21, fread(buf, 1, sizeof(buf), f));
    const char ref_buf[] = "0123456789\nabc\n\0\0foo\n";
    TEST_ASSERT_EQUAL_INT8_ARRAY(ref_buf, buf, sizeof(ref_buf) - 1);

    // Error checking
    // Attempting to seek before the beginning of file should return an error
    TEST_ASSERT_EQUAL(-1, fseek(f, 100, 100));  // Bad mode
    TEST_ASSERT_EQUAL(EINVAL, errno);
    TEST_ASSERT_EQUAL(-1, fseek(f, -1, SEEK_SET)); // Seeking to before start of file
    TEST_ASSERT_EQUAL(EINVAL, errno);


    TEST_ASSERT_EQUAL(0, fclose(f));
    test_teardown();
}

TEST_CASE("truncate", "[littlefs]")
{
    test_setup();

    FILE* f;
    char buf[10] = { 0 };
    const char fn[] = littlefs_base_path "/truncate.txt";

    f = fopen(fn, "w");
    TEST_ASSERT_NOT_NULL(f);
    TEST_ASSERT_EQUAL(11, fprintf(f, "0123456789\n"));
    TEST_ASSERT_EQUAL(0, fclose(f));

    TEST_ASSERT_EQUAL(0, truncate(fn, 3));

    f = fopen(fn, "r");
    TEST_ASSERT_NOT_NULL(f);
    TEST_ASSERT_EQUAL(3, fread(buf, 1, 8, f));
    TEST_ASSERT_EQUAL(0, fclose(f));
    TEST_ASSERT_EQUAL_STRING_LEN("012", buf, 8);

    test_teardown();
}

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 4, 2)
TEST_CASE("ftruncate", "[littlefs]")
{
    test_setup();

    int fd;
    FILE* f;
    char buf[10] = { 0 };
    const char fn[] = littlefs_base_path "/truncate.txt";

    f = fopen(fn, "w");
    TEST_ASSERT_NOT_NULL(f);
    TEST_ASSERT_EQUAL(11, fprintf(f, "0123456789\n"));
    TEST_ASSERT_EQUAL(0, fclose(f));

    fd = open(fn, O_RDWR);
    TEST_ASSERT_EQUAL(0, ftruncate(fd, 3));
    TEST_ASSERT_EQUAL(0, close(fd));


    f = fopen(fn, "r");
    TEST_ASSERT_NOT_NULL(f);
    TEST_ASSERT_EQUAL(3, fread(buf, 1, 8, f));
    TEST_ASSERT_EQUAL(0, fclose(f));
    TEST_ASSERT_EQUAL_STRING_LEN("012", buf, 8);

    test_teardown();
}
#endif // ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 4, 2)


TEST_CASE("stat/fstat returns correct values", "[littlefs]")
{
    test_setup();
    const char filename[] = littlefs_base_path "/stat.txt";

    test_littlefs_create_file_with_text(filename, "foo\n");
    struct stat st;
    for(uint8_t i=0; i < 2; i++) {
        if(i == 0){
            // Test stat
            TEST_ASSERT_EQUAL(0, stat(filename, &st));
        }
        else {
#ifndef CONFIG_LITTLEFS_USE_ONLY_HASH
            // Test fstat
            FILE *f = fopen(filename, "r");
            TEST_ASSERT_NOT_NULL(f);
            TEST_ASSERT_EQUAL(0, fstat(fileno(f), &st));
            TEST_ASSERT_EQUAL(0, fclose(f));
#endif
        }
        TEST_ASSERT(st.st_mode & S_IFREG);
        TEST_ASSERT_FALSE(st.st_mode & S_IFDIR);
        TEST_ASSERT_EQUAL(4, st.st_size);
        // TODO: test mtime for fstat
    }

    test_teardown();
}

TEST_CASE("unlink removes a file", "[littlefs]")
{
    test_setup();

    const char filename[] = littlefs_base_path "/unlink.txt";

    test_littlefs_create_file_with_text(filename, "unlink\n");
    TEST_ASSERT_EQUAL(0, unlink(filename));
    TEST_ASSERT_NULL(fopen(filename, "r"));

    test_teardown();
}

TEST_CASE("rename moves a file", "[littlefs]")
{
    test_setup();
    const char filename_prefix[] = littlefs_base_path "/move";

    char name_dst[64];
    char name_src[64];
    snprintf(name_dst, sizeof(name_dst), "%s_dst.txt", filename_prefix);
    snprintf(name_src, sizeof(name_src), "%s_src.txt", filename_prefix);

    unlink(name_dst);
    unlink(name_src);

    FILE* f = fopen(name_src, "w+");
    TEST_ASSERT_NOT_NULL(f);
    const char* str = "0123456789";
    for (int i = 0; i < 400; ++i) {
        TEST_ASSERT_NOT_EQUAL(EOF, fputs(str, f));
    }
    TEST_ASSERT_EQUAL(0, fclose(f));
    TEST_ASSERT_EQUAL(0, rename(name_src, name_dst));
    TEST_ASSERT_NULL(fopen(name_src, "r"));
    FILE* fdst = fopen(name_dst, "r");
    TEST_ASSERT_NOT_NULL(fdst);
    TEST_ASSERT_EQUAL(0, fseek(fdst, 0, SEEK_END));
    TEST_ASSERT_EQUAL(4000, ftell(fdst));
    TEST_ASSERT_EQUAL(0, fclose(fdst));

    test_teardown();
}

TEST_CASE("can opendir root directory of FS", "[littlefs]")
{
    test_setup();

    const char path[] = littlefs_base_path;

    char name_dir_file[64];
    const char * file_name = "test_opd.txt";
    snprintf(name_dir_file, sizeof(name_dir_file), "%s/%s", path, file_name);
    unlink(name_dir_file);
    test_littlefs_create_file_with_text(name_dir_file, "test_opendir\n");
    DIR* dir = opendir(path);
    TEST_ASSERT_NOT_NULL(dir);
    bool found = false;
    while (true) {
        struct dirent* de = readdir(dir);
        if (!de) {
            break;
        }
        if (strcasecmp(de->d_name, file_name) == 0) {
            found = true;
            break;
        }
    }
    TEST_ASSERT_TRUE(found);
    TEST_ASSERT_EQUAL(0, closedir(dir));
    unlink(name_dir_file);

    test_teardown();
}

TEST_CASE("mkdir, rmdir", "[littlefs]")
{
    test_setup();
    const char filename_prefix[] = littlefs_base_path "/";

    char name_dir1[64];
    char name_dir2[64];
    char name_dir2_file[64];

    snprintf(name_dir1, sizeof(name_dir1), "%s1", filename_prefix);
    snprintf(name_dir2, sizeof(name_dir2), "%s2", filename_prefix);
    snprintf(name_dir2_file, sizeof(name_dir2_file), "%s2/1.txt", filename_prefix);

    TEST_ASSERT_EQUAL(0, mkdir(name_dir1, 0755));
    struct stat st;
    TEST_ASSERT_EQUAL(0, stat(name_dir1, &st));
    TEST_ASSERT_TRUE(st.st_mode & S_IFDIR);
    TEST_ASSERT_FALSE(st.st_mode & S_IFREG);
    TEST_ASSERT_EQUAL(0, rmdir(name_dir1));

    // Attempt to stat a removed directory
    TEST_ASSERT_EQUAL(-1, stat(name_dir1, &st));
    TEST_ASSERT_EQUAL(ENOENT, errno);

    TEST_ASSERT_EQUAL(0, mkdir(name_dir2, 0755));
    test_littlefs_create_file_with_text(name_dir2_file, "foo\n");
    TEST_ASSERT_EQUAL(0, stat(name_dir2, &st));
    TEST_ASSERT_TRUE(st.st_mode & S_IFDIR);
    TEST_ASSERT_FALSE(st.st_mode & S_IFREG);
    TEST_ASSERT_EQUAL(0, stat(name_dir2_file, &st));
    TEST_ASSERT_FALSE(st.st_mode & S_IFDIR);
    TEST_ASSERT_TRUE(st.st_mode & S_IFREG);

    // Can't remove directory, its not empty
    TEST_ASSERT_EQUAL(-1, rmdir(name_dir2));
    TEST_ASSERT_EQUAL(ENOTEMPTY, errno);

    TEST_ASSERT_EQUAL(0, unlink(name_dir2_file));
#if !CONFIG_LITTLEFS_SPIFFS_COMPAT
    /* this will have already been deleted */
    TEST_ASSERT_EQUAL(0, rmdir(name_dir2));
#endif

    test_teardown();
}

TEST_CASE("opendir, readdir, rewinddir, seekdir work as expected", "[littlefs]")
{
    test_setup();
    const char dir_prefix[] = littlefs_base_path "/dir";

    char name_dir_inner_file[64];
    char name_dir_inner[64];
    char name_dir_file3[64];
    char name_dir_file2[64];
    char name_dir_file1[64];

    snprintf(name_dir_inner_file, sizeof(name_dir_inner_file), "%s/inner/3.txt", dir_prefix);
    snprintf(name_dir_inner,      sizeof(name_dir_inner),      "%s/inner",       dir_prefix);
    snprintf(name_dir_file3,      sizeof(name_dir_file2),      "%s/boo.bin",     dir_prefix);
    snprintf(name_dir_file2,      sizeof(name_dir_file2),      "%s/2.txt",       dir_prefix);
    snprintf(name_dir_file1,      sizeof(name_dir_file1),      "%s/1.txt",       dir_prefix);

    /* Remove files/dirs that may exist */
    unlink(name_dir_inner_file);
    rmdir(name_dir_inner);
    unlink(name_dir_file1);
    unlink(name_dir_file2);
    unlink(name_dir_file3);
    rmdir(dir_prefix);

    /* Create the files */
    TEST_ASSERT_EQUAL(0, mkdir(dir_prefix, 0755));
    TEST_ASSERT_EQUAL(0, mkdir(name_dir_inner, 0755));
    test_littlefs_create_file_with_text(name_dir_file1, "1\n");
    test_littlefs_create_file_with_text(name_dir_file2, "2\n");
    test_littlefs_create_file_with_text(name_dir_file3, "\01\02\03");
    test_littlefs_create_file_with_text(name_dir_inner_file, "3\n");

    DIR* dir = opendir(dir_prefix);
    TEST_ASSERT_NOT_NULL(dir);
    int count = 0;
    const char* names[4];
    while( true ) {
        struct dirent* de = readdir(dir);
        if (!de) {
            break;
        }
        if (strcasecmp(de->d_name, "1.txt") == 0) {
            TEST_ASSERT_TRUE(de->d_type == DT_REG);
            names[count] = "1.txt";
            ++count;
        } else if (strcasecmp(de->d_name, "2.txt") == 0) {
            TEST_ASSERT_TRUE(de->d_type == DT_REG);
            names[count] = "2.txt";
            ++count;
        } else if (strcasecmp(de->d_name, "inner") == 0) {
            TEST_ASSERT_TRUE(de->d_type == DT_DIR);
            names[count] = "inner";
            ++count;
        } else if (strcasecmp(de->d_name, "boo.bin") == 0) {
            TEST_ASSERT_TRUE(de->d_type == DT_REG);
            names[count] = "boo.bin";
            ++count;
        } else {
            char buf[512] = { 0 };
            snprintf(buf, sizeof(buf), "unexpected directory entry \"%s\"", de->d_name);
            TEST_FAIL_MESSAGE(buf);
        }
    }
    TEST_ASSERT_EQUAL(count, 4);

    rewinddir(dir);
    struct dirent* de = readdir(dir);
    TEST_ASSERT_NOT_NULL(de);
    TEST_ASSERT_EQUAL(0, strcasecmp(de->d_name, names[0]));
    seekdir(dir, 3);
    de = readdir(dir);
    TEST_ASSERT_NOT_NULL(de);
    TEST_ASSERT_EQUAL(0, strcasecmp(de->d_name, names[3]));
    seekdir(dir, 1);
    de = readdir(dir);
    TEST_ASSERT_NOT_NULL(de);
    TEST_ASSERT_EQUAL(0, strcasecmp(de->d_name, names[1]));
    seekdir(dir, 2);
    de = readdir(dir);
    TEST_ASSERT_NOT_NULL(de);
    TEST_ASSERT_EQUAL(0, strcasecmp(de->d_name, names[2]));

    TEST_ASSERT_EQUAL(0, closedir(dir));

    test_teardown();
}

TEST_CASE("readdir with large number of files", "[littlefs][timeout=30]")
{
    test_setup();
    test_littlefs_readdir_many_files(littlefs_base_path "/dir2");
    test_teardown();
}

TEST_CASE("multiple tasks can use same volume", "[littlefs]")
{
    test_setup();
    test_littlefs_concurrent_rw(littlefs_base_path "/f");
    test_teardown();
}

TEST_CASE("esp_littlefs_info", "[littlefs]")
{
    test_setup();

    char filename[] = littlefs_base_path "/test_esp_littlefs_info.bin";
    unlink(filename);  /* Delete the file incase it exists */
    
    /* Get starting system size */
    size_t total_og = 0, used_og = 0;
    TEST_ESP_OK(esp_littlefs_info(littlefs_test_partition_label, &total_og, &used_og));

    /* Write 100,000 bytes */
    FILE* f = fopen(filename, "wb");
    TEST_ASSERT_NOT_NULL(f);
    char val = 'c';
    size_t n_bytes = 100000;
    for(int i=0; i < n_bytes; i++) {
        TEST_ASSERT_EQUAL(1, fwrite(&val, 1, 1, f));
    }
    TEST_ASSERT_EQUAL(0, fclose(f));

    /* Re-check system size */
    size_t total_new = 0, used_new = 0;
    TEST_ESP_OK(esp_littlefs_info(littlefs_test_partition_label, &total_new, &used_new));

    printf("old: %d; new: %d; diff: %d\n", used_og, used_new, used_new-used_og);

    /* total amount of storage shouldn't change */
    TEST_ASSERT_EQUAL_INT(total_og, total_new);

    /* The actual amount of used storage should be within 2 blocks of expected.*/
    size_t diff = used_new - used_og;
    TEST_ASSERT_GREATER_THAN_INT(n_bytes - (2 * 4096), diff);
    TEST_ASSERT_LESS_THAN_INT(n_bytes + (2 * 4096), diff);

    unlink(filename);
    test_teardown();
}

#if CONFIG_LITTLEFS_USE_MTIME

#if CONFIG_LITTLEFS_MTIME_USE_SECONDS
TEST_CASE("mtime support", "[littlefs]")
{

    /* Open a file, check that mtime is set correctly */
    const char* filename = littlefs_base_path "/time";
    test_setup();
    time_t t_before_create = time(NULL);
    test_littlefs_create_file_with_text(filename, "test");
    time_t t_after_create = time(NULL);

    struct stat st;
    TEST_ASSERT_EQUAL(0, stat(filename, &st));
    printf("mtime=%d\n", (int) st.st_mtime);
    TEST_ASSERT(st.st_mtime >= t_before_create
             && st.st_mtime <= t_after_create);

    /* Wait a bit, open again, check that mtime is updated */
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    time_t t_before_open = time(NULL);
    FILE *f = fopen(filename, "a");
    time_t t_after_open = time(NULL);
    TEST_ASSERT_EQUAL(0, stat(filename, &st));
    printf("mtime=%d\n", (int) st.st_mtime);
    TEST_ASSERT(st.st_mtime >= t_before_open
             && st.st_mtime <= t_after_open);
    TEST_ASSERT_EQUAL(0, fclose(f));

    /* Wait a bit, open for reading, check that mtime is not updated */
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    time_t t_before_open_ro = time(NULL);
    f = fopen(filename, "r");
    TEST_ASSERT_EQUAL(0, stat(filename, &st));
    printf("mtime=%d\n", (int) st.st_mtime);
    TEST_ASSERT(t_before_open_ro > t_after_open
             && st.st_mtime >= t_before_open
             && st.st_mtime <= t_after_open);
    TEST_ASSERT_EQUAL(0, fclose(f));

    TEST_ASSERT_EQUAL(0, unlink(filename));

    test_teardown();
}
#endif

#if CONFIG_LITTLEFS_MTIME_USE_NONCE
TEST_CASE("mnonce support", "[littlefs]")
{
    /* Open a file, check that mtime is set correctly */
    struct stat st;
    const char* filename = littlefs_base_path "/time";
    test_setup();
    test_littlefs_create_file_with_text(filename, "test");

    int nonce1;
    TEST_ASSERT_EQUAL(0, stat(filename, &st));
    nonce1 = (int) st.st_mtime;
    printf("mtime=%d\n", nonce1);
    TEST_ASSERT(nonce1 >= 0);

    /* open again, check that mtime is updated */
    int nonce2;
    FILE *f = fopen(filename, "a");
    TEST_ASSERT_EQUAL(0, stat(filename, &st));
    nonce2 = (int) st.st_mtime;
    printf("mtime=%d\n", nonce2);
    if( nonce1 == UINT32_MAX ) {
        TEST_ASSERT_EQUAL_INT(1, nonce2);
    }
    else {
        TEST_ASSERT_EQUAL_INT(1, nonce2-nonce1);
    }
    TEST_ASSERT_EQUAL(0, fclose(f));

    /* open for reading, check that mtime is not updated */
    int nonce3;
    f = fopen(filename, "r");
    TEST_ASSERT_EQUAL(0, stat(filename, &st));
    nonce3 = (int) st.st_mtime;
    printf("mtime=%d\n", (int) st.st_mtime);
    TEST_ASSERT_EQUAL_INT(nonce2, nonce3);
    TEST_ASSERT_EQUAL(0, fclose(f));

    TEST_ASSERT_EQUAL(0, unlink(filename));

    test_teardown();
}
#endif

#endif

static void test_littlefs_write_file_with_offset(const char *filename)
{
    const char *source = "Replace this character: [k]";
    off_t offset = strstr(source, "k") - source;
    size_t len = strlen(source);
    const char new_char = 'y';

    // Create file with string at source string
    test_littlefs_create_file_with_text(filename, source);

    // Replace k with y at the file
    int fd = open(filename, O_RDWR);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, fd);
    int written = pwrite(fd, &new_char, 1, offset);
    TEST_ASSERT_EQUAL(1, written);
    TEST_ASSERT_EQUAL(0, close(fd));
    
    char buf[len];

    // Compare if both are equal
    FILE *f = fopen(filename, "r");
    TEST_ASSERT_NOT_NULL(f);
    int rd = fread(buf, len, 1, f);
    TEST_ASSERT_EQUAL(1, rd);
    TEST_ASSERT_EQUAL(buf[offset], new_char);
    TEST_ASSERT_EQUAL(0, fclose(f));
}

static void test_littlefs_read_file_with_offset(const char *filename)
{
    const char *source = "This text will be partially read";
    off_t offset = strstr(source, "p") - source;
    size_t len = strlen(source);
    char buf[len - offset + 1];
    buf[len-offset] = '\0'; // EOS

    // Create file with string at source string
    test_littlefs_create_file_with_text(filename, source);

    // Read file content beginning at `partially` word
    int fd = open(filename, O_RDONLY);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, fd);
    int rd = pread(fd, buf, len - offset, offset);
    TEST_ASSERT_EQUAL(len - offset, rd);
    // Compare if string read from file and source string related slice are equal
    int res = strcmp(buf, &source[offset]);
    TEST_ASSERT_EQUAL(0, res);
    TEST_ASSERT_EQUAL(0, close(fd));
}

static void test_littlefs_create_file_with_text(const char* name, const char* text)
{
    printf("Writing to \"%s\"\n", name);
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

static void test_littlefs_read_file(const char* filename)
{
    FILE* f = fopen(filename, "r");
    TEST_ASSERT_NOT_NULL(f);
    char buf[32] = { 0 };
    int cb = fread(buf, 1, sizeof(buf), f);
    TEST_ASSERT_EQUAL(strlen(littlefs_test_hello_str), cb);
    TEST_ASSERT_EQUAL(0, strcmp(littlefs_test_hello_str, buf));
    TEST_ASSERT_EQUAL(0, fclose(f));
}

static void test_littlefs_readdir_many_files(const char* dir_prefix)
{
    const int n_files = 40;
    const int n_folders = 4;
    unsigned char file_count[n_files * n_folders];
    memset(file_count, 0, sizeof(file_count)/sizeof(file_count[0]));
    char file_name[ESP_VFS_PATH_MAX + CONFIG_LITTLEFS_OBJ_NAME_LEN];

    /* clean stale files before the test */
    mkdir(dir_prefix, 0755);
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
        snprintf(file_name, sizeof(file_name), "%s/%d", dir_prefix, d);
        mkdir( file_name, 0755 );
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

static void test_littlefs_open_max_files(const char* filename_prefix, size_t files_count)
{
    FILE** files = calloc(files_count, sizeof(FILE*));
    assert(files);
    for (size_t i = 0; i < files_count; ++i) {
        char name[32];
        snprintf(name, sizeof(name), "%s_%d.txt", filename_prefix, i);
        printf("Opening \"%s\"\n", name);
        TEST_ASSERT_TRUE( heap_caps_check_integrity_all(true) );
        files[i] = fopen(name, "w");
        TEST_ASSERT_NOT_NULL(files[i]);
        TEST_ASSERT_TRUE( heap_caps_check_integrity_all(true) );
    }
    /* close everything and clean up */
    for (size_t i = 0; i < files_count; ++i) {
        TEST_ASSERT_EQUAL(0, fclose(files[i]));
        TEST_ASSERT_TRUE( heap_caps_check_integrity_all(true) );
    }
    free(files);
}

typedef enum {
    CONCURRENT_TASK_ACTION_READ,
    CONCURRENT_TASK_ACTION_WRITE,
    CONCURRENT_TASK_ACTION_STAT,
} concurrent_task_action_t;

typedef struct {
    const char* filename;
    concurrent_task_action_t action;
    size_t word_count;
    int seed;
    SemaphoreHandle_t done;
    int result;
} read_write_test_arg_t;

#define READ_WRITE_TEST_ARG_INIT(name, seed_) \
        { \
            .filename = name, \
            .seed = seed_, \
            .word_count = 4096, \
            .action = CONCURRENT_TASK_ACTION_WRITE, \
            .done = xSemaphoreCreateBinary() \
        }

static void read_write_task(void* param)
{
    FILE *f = NULL;
    read_write_test_arg_t* args = (read_write_test_arg_t*) param;
    if (args->action == CONCURRENT_TASK_ACTION_WRITE) {
        f = fopen(args->filename, "wb");
        if (f == NULL) {args->result = ESP_ERR_NOT_FOUND; goto done;}
    } else if (args->action == CONCURRENT_TASK_ACTION_READ) {
        f = fopen(args->filename, "rb");
        if (f == NULL) {args->result = ESP_ERR_NOT_FOUND; goto done;}
    } else if (args->action == CONCURRENT_TASK_ACTION_STAT) {
    }

    srand(args->seed);
    for (size_t i = 0; i < args->word_count; ++i) {
        uint32_t val = rand();
        if (args->action == CONCURRENT_TASK_ACTION_WRITE) {
            int cnt = fwrite(&val, sizeof(val), 1, f);
            if (cnt != 1) {
                esp_rom_printf("E(w): i=%d, cnt=%d val=%d\n\n", i, cnt, val);
                args->result = ESP_FAIL;
                goto close;
            }
        } else if (args->action == CONCURRENT_TASK_ACTION_READ) {
            uint32_t rval;
            int cnt = fread(&rval, sizeof(rval), 1, f);
            if (cnt != 1) {
                esp_rom_printf("E(r): i=%d, cnt=%d rval=%d\n\n", i, cnt, rval);
                args->result = ESP_FAIL;
                goto close;
            }
        } else if (args->action == CONCURRENT_TASK_ACTION_STAT) {
            int res;
            struct stat buf;
            res = stat(args->filename, &buf);
            if(res < 0) {
                args->result = ESP_FAIL;
                goto done;
            }
        }
    }
    args->result = ESP_OK;

close:
    if(f) {
        TEST_ASSERT_EQUAL(0, fclose(f));
    }

done:
    xSemaphoreGive(args->done);
    vTaskDelay(1);
    vTaskDelete(NULL);
}


static void test_littlefs_concurrent_rw(const char* filename_prefix)
{
#define TASK_SIZE 4096
    char names[4][64];
    for (size_t i = 0; i < 4; ++i) {
        snprintf(names[i], sizeof(names[i]), "%s%d", filename_prefix, i + 1);
        unlink(names[i]);  // Make sure these files don't exist
    }

    /************************************************
     * TESTING CONCURRENT WRITES TO DIFFERENT FILES *
     ************************************************/
    read_write_test_arg_t args1 = READ_WRITE_TEST_ARG_INIT(names[0], 1);
    read_write_test_arg_t args2 = READ_WRITE_TEST_ARG_INIT(names[1], 2);
    printf("writing f1 and f2\n");
    const int cpuid_0 = 0;
    const int cpuid_1 = portNUM_PROCESSORS - 1;
    xTaskCreatePinnedToCore(&read_write_task, "rw1", TASK_SIZE, &args1, 3, NULL, cpuid_0);
    xTaskCreatePinnedToCore(&read_write_task, "rw2", TASK_SIZE, &args2, 3, NULL, cpuid_1);

    xSemaphoreTake(args1.done, portMAX_DELAY);
    printf("f1 done\n");
    TEST_ASSERT_EQUAL(ESP_OK, args1.result);
    xSemaphoreTake(args2.done, portMAX_DELAY);
    printf("f2 done\n");
    TEST_ASSERT_EQUAL(ESP_OK, args2.result);

    args1.action = CONCURRENT_TASK_ACTION_READ;
    args2.action = CONCURRENT_TASK_ACTION_READ;
    read_write_test_arg_t args3 = READ_WRITE_TEST_ARG_INIT(names[2], 3);
    read_write_test_arg_t args4 = READ_WRITE_TEST_ARG_INIT(names[3], 4);

    read_write_test_arg_t args5 = READ_WRITE_TEST_ARG_INIT(names[0], 3);
    args5.action = CONCURRENT_TASK_ACTION_STAT;
    args5.word_count = 300;
    read_write_test_arg_t args6 = READ_WRITE_TEST_ARG_INIT(names[0], 3);
    args6.action = CONCURRENT_TASK_ACTION_STAT;
    args6.word_count = 300;

    printf("reading f1 and f2, writing f3 and f4, stating f1 concurrently from 2 cores\n");

    xTaskCreatePinnedToCore(&read_write_task, "rw3", TASK_SIZE, &args3, 3, NULL, cpuid_1);
    xTaskCreatePinnedToCore(&read_write_task, "rw4", TASK_SIZE, &args4, 3, NULL, cpuid_0);
    xTaskCreatePinnedToCore(&read_write_task, "rw1", TASK_SIZE, &args1, 3, NULL, cpuid_0);
    xTaskCreatePinnedToCore(&read_write_task, "rw2", TASK_SIZE, &args2, 3, NULL, cpuid_1);

    xTaskCreatePinnedToCore(&read_write_task, "stat1", TASK_SIZE, &args5, 3, NULL, cpuid_0);
    xTaskCreatePinnedToCore(&read_write_task, "stat2", TASK_SIZE, &args6, 3, NULL, cpuid_1);


    xSemaphoreTake(args1.done, portMAX_DELAY);
    printf("f1 done\n");
    TEST_ASSERT_EQUAL(ESP_OK, args1.result);
    xSemaphoreTake(args2.done, portMAX_DELAY);
    printf("f2 done\n");
    TEST_ASSERT_EQUAL(ESP_OK, args2.result);
    xSemaphoreTake(args3.done, portMAX_DELAY);
    printf("f3 done\n");
    TEST_ASSERT_EQUAL(ESP_OK, args3.result);
    xSemaphoreTake(args4.done, portMAX_DELAY);
    printf("f4 done\n");

    TEST_ASSERT_EQUAL(ESP_OK, args5.result);
    xSemaphoreTake(args5.done, portMAX_DELAY);
    printf("stat1 done\n");
    TEST_ASSERT_EQUAL(ESP_OK, args6.result);
    xSemaphoreTake(args6.done, portMAX_DELAY);
    printf("stat2 done\n");


    vSemaphoreDelete(args1.done);
    vSemaphoreDelete(args2.done);
    vSemaphoreDelete(args3.done);
    vSemaphoreDelete(args4.done);
#undef TASK_SIZE
}

#if CONFIG_LITTLEFS_SPIFFS_COMPAT
TEST_CASE("SPIFFS COMPAT: file creation and deletion", "[littlefs]")
{
    test_setup();

    const char* filename = littlefs_base_path "/spiffs_compat/foo/bar/spiffs_compat.bin";

    FILE* f = fopen(filename, "w");
    TEST_ASSERT_NOT_NULL(f);
    TEST_ASSERT_TRUE(fputs("bar", f) != EOF);
    TEST_ASSERT_EQUAL(0, fclose(f));

    TEST_ASSERT_EQUAL(0, unlink(filename));

    /* check to see if all the directories were deleted */
    struct stat sb;
    if (stat(littlefs_base_path "/spiffs_compat", &sb) == 0 && S_ISDIR(sb.st_mode)) {
        TEST_FAIL_MESSAGE("Empty directories were not deleted");
    }

    test_teardown();
}

TEST_CASE("SPIFFS COMPAT: file creation and rename", "[littlefs]")
{
    test_setup();

    int res;
    char message[256];
    const char* src = littlefs_base_path "/spiffs_compat/src/foo/bar/spiffs_compat.bin";
    const char* dst = littlefs_base_path "/spiffs_compat/dst/foo/bar/spiffs_compat.bin";

    FILE* f = fopen(src, "w");
    TEST_ASSERT_NOT_NULL(f);
    TEST_ASSERT_TRUE(fputs("bar", f) != EOF);
    TEST_ASSERT_EQUAL(0, fclose(f));

    res = rename(src, dst);
    snprintf(message, sizeof(message), "errno: %d", errno);
    TEST_ASSERT_EQUAL_MESSAGE(0, res, message);

    /* check to see if all the directories were deleted */
    struct stat sb;
    if (stat(littlefs_base_path "/spiffs_compat/src", &sb) == 0 && S_ISDIR(sb.st_mode)) {
        TEST_FAIL_MESSAGE("Empty directories were not deleted");
    }

    test_teardown();
}

#endif  // CONFIG_LITTLEFS_SPIFFS_COMPAT

TEST_CASE("Rewriting file frees space immediately (#7426)", "[littlefs]")
{
    /* modified from:
     *     https://github.com/esp8266/Arduino/commit/c663c55926f205723c3d56dd7030bacbe7960f8e
     */

    test_setup();

    size_t total = 0, used = 0;
    TEST_ESP_OK(esp_littlefs_info(littlefs_test_partition_label, &total, &used));

    // 2 block overhead
    int kb_to_write = (total - used - (2*4096)) / 1024;

    // Create and overwrite a file >50% of spaceA (48/64K)
    uint8_t buf[1024];
    memset(buf, 0xaa, 1024);
    for (uint8_t x = 0; x < 2; x++) {
        FILE *f = fopen(littlefs_base_path "/file1.bin", "w");
        TEST_ASSERT_NOT_NULL(f);

        for (size_t i = 0; i < kb_to_write; i++) {
            TEST_ASSERT_EQUAL_INT(1024, fwrite(buf, 1, 1024, f));
        }
        TEST_ASSERT_EQUAL(0, fclose(f));
    }
    test_teardown();
}

TEST_CASE("esp_littlefs_info returns used_bytes > total_bytes", "[littlefs]")
{
    // https://github.com/joltwallet/esp_littlefs/issues/66
    test_setup();
    const char foo[] = "foofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoofoo";

    char names[7][64];
    for (size_t i = 0; i < 7; ++i) {
        snprintf(names[i], sizeof(names[i]), littlefs_base_path "/%d", i + 1);
        unlink(names[i]);  // Make sure these files don't exist

        FILE* f = fopen(names[i], "wb");
        TEST_ASSERT_NOT_NULL(f);
        char val = 'c';
        size_t n_bytes = 65432;
        for(int i=0; i < n_bytes; i++) {
            TEST_ASSERT_EQUAL(1, fwrite(&val, 1, 1, f));
        }
        TEST_ASSERT_EQUAL(0, fclose(f));
    }

    bool disk_full = false;
    int i = 0;
    while(!disk_full){
        char *filename = names[i % 7];
        FILE* f = fopen(filename, "a+b");
        TEST_ASSERT_NOT_NULL(f);
        size_t n_bytes = 200 + i % 17;
        int amount_written = fwrite(foo, n_bytes, 1, f);
        if(amount_written != 1) {
            disk_full = true;
        }
        if(0 != fclose(f)){
            disk_full = true;
        }

        size_t total = 0, used = 0;
        TEST_ESP_OK(esp_littlefs_info(littlefs_test_partition_label, &total, &used));
        TEST_ASSERT_GREATER_OR_EQUAL_INT(used, total);
        //printf("used: %d total: %d\n", used, total);
        i++;
    }
    test_teardown();
}

TEST_CASE("fcntl get flags", "[littlefs]")
{
    int fd;
    int ret;

    test_setup();

    fd = open("/littlefs/test.txt", O_CREAT | O_WRONLY);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, fd);
    ret = fcntl(fd, F_GETFL);
    TEST_ASSERT_EQUAL(O_WRONLY, ret);
    close(fd);

    fd = open("/littlefs/test.txt", O_RDONLY);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, fd);
    ret = fcntl(fd, F_GETFL);
    TEST_ASSERT_EQUAL(O_RDONLY, ret);
    close(fd);

    fd = open("/littlefs/test.txt", O_RDWR);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, fd);
    ret = fcntl(fd, F_GETFL);
    TEST_ASSERT_EQUAL(O_RDWR, ret);
    close(fd);

    test_teardown();
}

static void test_setup() {
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

static void test_teardown(){
    TEST_ESP_OK(esp_vfs_littlefs_unregister(littlefs_test_partition_label));
    TEST_ASSERT_TRUE( heap_caps_check_integrity_all(true) );
    printf("Test teardown complete.\n");
}
