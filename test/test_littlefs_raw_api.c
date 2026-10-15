#include "test_littlefs_common.h"

/* Opt in locally; the v5.x unit-test-app doesn't load this repo's sdkconfig.defaults. */
#define ESP_LITTLEFS_RAW_API
#include "lfs.h"

#ifndef LFS_CONFIG
#error "LFS_CONFIG was not exported to the consumer"
#endif

/* Same geometry and callbacks the VFS layer uses for the test partition. */
static int raw_part_err(esp_err_t err)
{
    return err == ESP_OK ? LFS_ERR_OK : LFS_ERR_IO;
}

static int raw_read(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, void *buffer, lfs_size_t size)
{
    return raw_part_err(esp_partition_read(c->context, block * c->block_size + off, buffer, size));
}

static int raw_prog(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, const void *buffer, lfs_size_t size)
{
    return raw_part_err(esp_partition_write(c->context, block * c->block_size + off, buffer, size));
}

static int raw_erase(const struct lfs_config *c, lfs_block_t block)
{
    return raw_part_err(esp_partition_erase_range(c->context, block * c->block_size, c->block_size));
}

static int raw_sync(const struct lfs_config *c)
{
    (void)c;
    return LFS_ERR_OK;
}

static void get_test_raw_cfg(struct lfs_config *cfg)
{
    const esp_partition_t *part = get_test_data_partition();
    TEST_ASSERT_NOT_NULL(part);

    memset(cfg, 0, sizeof(*cfg));
    cfg->context        = (void *)part;
    cfg->read           = raw_read;
    cfg->prog           = raw_prog;
    cfg->erase          = raw_erase;
    cfg->sync           = raw_sync;
    cfg->read_size      = CONFIG_LITTLEFS_READ_SIZE;
    cfg->prog_size      = CONFIG_LITTLEFS_WRITE_SIZE;
    cfg->cache_size     = CONFIG_LITTLEFS_CACHE_SIZE;
    cfg->lookahead_size = CONFIG_LITTLEFS_LOOKAHEAD_SIZE;
    cfg->block_cycles   = CONFIG_LITTLEFS_BLOCK_CYCLES;
    cfg->block_size     = 4096;
    cfg->block_count    = part->size / cfg->block_size;
#if CONFIG_LITTLEFS_MULTIVERSION
#if CONFIG_LITTLEFS_DISK_VERSION_MOST_RECENT
    cfg->disk_version = 0;
#elif CONFIG_LITTLEFS_DISK_VERSION_2_1
    cfg->disk_version = 0x00020001;
#elif CONFIG_LITTLEFS_DISK_VERSION_2_0
    cfg->disk_version = 0x00020000;
#endif
#endif
}

TEST_CASE("raw littlefs headers are exposed to consumers", "[littlefs_raw]")
{
    /* Declared only in include/lfs_config.h, not in upstream's lfs_util.h. */
    TEST_ASSERT_EQUAL_STRING("esp_littlefs", ESP_LITTLEFS_TAG);

    TEST_ASSERT_EQUAL_UINT32(4, lfs_alignup(3, 4));
    TEST_ASSERT_EQUAL_UINT32(0, lfs_aligndown(3, 4));
    TEST_ASSERT_LESS_THAN(0, LFS_ERR_CORRUPT);
}

/* Mount the raw API over the same partition the VFS layer uses and exchange
 * data both ways: the published headers must describe the on-disk format the
 * component writes. */
TEST_CASE("raw littlefs API is interchangeable with the VFS layer", "[littlefs_raw]")
{
    const esp_partition_t *part = get_test_data_partition();
    TEST_ASSERT_NOT_NULL(part);
    TEST_ESP_OK(esp_partition_erase_range(part, 0, part->size));

    struct lfs_config cfg;
    lfs_t lfs;
    get_test_raw_cfg(&cfg);
    TEST_ASSERT_EQUAL(LFS_ERR_OK, lfs_format(&lfs, &cfg));
    TEST_ASSERT_EQUAL(LFS_ERR_OK, lfs_mount(&lfs, &cfg));

    lfs_file_t file;
    TEST_ASSERT_EQUAL(LFS_ERR_OK, lfs_file_open(&lfs, &file, "hello.txt", LFS_O_WRONLY | LFS_O_CREAT));
    TEST_ASSERT_EQUAL(strlen(littlefs_test_hello_str),
                      lfs_file_write(&lfs, &file, littlefs_test_hello_str, strlen(littlefs_test_hello_str)));
    TEST_ASSERT_EQUAL(LFS_ERR_OK, lfs_file_close(&lfs, &file));

    struct lfs_info info;
    TEST_ASSERT_EQUAL(LFS_ERR_OK, lfs_stat(&lfs, "hello.txt", &info));
    TEST_ASSERT_EQUAL(LFS_TYPE_REG, info.type);
    TEST_ASSERT_EQUAL(strlen(littlefs_test_hello_str), info.size);
    TEST_ASSERT_EQUAL(LFS_ERR_OK, lfs_unmount(&lfs));

    /* Mount VFS over the existing image; test_setup() would format it away. */
    const esp_vfs_littlefs_conf_t conf = {
        .base_path = littlefs_base_path,
        .partition_label = littlefs_test_partition_label,
        .format_if_mount_failed = false,
    };
    TEST_ESP_OK(esp_vfs_littlefs_register(&conf));
    test_littlefs_read_file_with_content(littlefs_base_path "/hello.txt", littlefs_test_hello_str);

    test_littlefs_create_file_with_text(littlefs_base_path "/from_vfs.txt", littlefs_test_hello_str);
    test_teardown();

    TEST_ASSERT_EQUAL(LFS_ERR_OK, lfs_mount(&lfs, &cfg));
    char buf[32] = {0};
    TEST_ASSERT_EQUAL(LFS_ERR_OK, lfs_file_open(&lfs, &file, "from_vfs.txt", LFS_O_RDONLY));
    TEST_ASSERT_EQUAL(strlen(littlefs_test_hello_str), lfs_file_read(&lfs, &file, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL(LFS_ERR_OK, lfs_file_close(&lfs, &file));
    TEST_ASSERT_EQUAL_STRING(littlefs_test_hello_str, buf);
    TEST_ASSERT_EQUAL(LFS_ERR_OK, lfs_unmount(&lfs));
}
