//#define LOG_LOCAL_LEVEL 4

#include "esp_littlefs.h"

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
#include "esp_littlefs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"
#include "esp_partition.h"
#include "errno.h"


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
    TEST_ASSERT_EQUAL(4, fprintf(f, "abc\n"));
    TEST_ASSERT_EQUAL(0, fseek(f, 0, SEEK_END));
    TEST_ASSERT_EQUAL(15, ftell(f));
    TEST_ASSERT_EQUAL(0, fseek(f, 0, SEEK_SET));
    char buf[20];
    TEST_ASSERT_EQUAL(15, fread(buf, 1, sizeof(buf), f));
    const char ref_buf[] = "0123456789\nabc\n";
    TEST_ASSERT_EQUAL_INT8_ARRAY(ref_buf, buf, sizeof(ref_buf) - 1);

    TEST_ASSERT_EQUAL(0, fclose(f));

    test_teardown();
}


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
            fclose(f);
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
    TEST_ASSERT_EQUAL(-2, stat(name_dir1, &st));

    TEST_ASSERT_EQUAL(0, mkdir(name_dir2, 0755));
    test_littlefs_create_file_with_text(name_dir2_file, "foo\n");
    TEST_ASSERT_EQUAL(0, stat(name_dir2, &st));
    TEST_ASSERT_TRUE(st.st_mode & S_IFDIR);
    TEST_ASSERT_FALSE(st.st_mode & S_IFREG);
    TEST_ASSERT_EQUAL(0, stat(name_dir2_file, &st));
    TEST_ASSERT_FALSE(st.st_mode & S_IFDIR);
    TEST_ASSERT_TRUE(st.st_mode & S_IFREG);
    TEST_ASSERT_EQUAL(-1, rmdir(name_dir2));
    TEST_ASSERT_EQUAL(0, unlink(name_dir2_file));
    TEST_ASSERT_EQUAL(0, rmdir(name_dir2));

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
    fclose(f);

    /* Wait a bit, open for reading, check that mtime is not updated */
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    time_t t_before_open_ro = time(NULL);
    f = fopen(filename, "r");
    TEST_ASSERT_EQUAL(0, stat(filename, &st));
    printf("mtime=%d\n", (int) st.st_mtime);
    TEST_ASSERT(t_before_open_ro > t_after_open
             && st.st_mtime >= t_before_open
             && st.st_mtime <= t_after_open);
    fclose(f);

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
    fclose(f);

    /* open for reading, check that mtime is not updated */
    int nonce3;
    f = fopen(filename, "r");
    TEST_ASSERT_EQUAL(0, stat(filename, &st));
    nonce3 = (int) st.st_mtime;
    printf("mtime=%d\n", (int) st.st_mtime);
    TEST_ASSERT_EQUAL_INT(nonce2, nonce3);
    fclose(f);

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
        fclose(files[i]);
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
                ets_printf("E(w): i=%d, cnt=%d val=%d\n\n", i, cnt, val);
                args->result = ESP_FAIL;
                goto close;
            }
        } else if (args->action == CONCURRENT_TASK_ACTION_READ) {
            uint32_t rval;
            int cnt = fread(&rval, sizeof(rval), 1, f);
            if (cnt != 1) {
                ets_printf("E(r): i=%d, cnt=%d rval=%d\n\n", i, cnt, rval);
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
    if(f) fclose(f);

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


static void test_setup() {
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

