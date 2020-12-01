#include "esp_littlefs_ram.h"

#include "esp_littlefs_abs.h"

#include "memory.h"

// static const char *const TAG = ESP_LITTLEFS_RAM_TAG;

// region little fs hooks

static int littlefs_api_read(const struct lfs_config *c, lfs_block_t block,
                      lfs_off_t off, void *buffer, lfs_size_t size) {
    void * mem = c->context;
    size_t part_off = (block * c->block_size) + off;
    memcpy(buffer, mem + part_off, size);
    return 0;
}

static int littlefs_api_prog(const struct lfs_config *c, lfs_block_t block,
                      lfs_off_t off, const void *buffer, lfs_size_t size) {
    void * mem = c->context;
    size_t part_off = (block * c->block_size) + off;
    memcpy(mem + part_off, buffer, size);
    return 0;
}

static int littlefs_api_erase(const struct lfs_config *c, lfs_block_t block) {
    void * mem = c->context;
    size_t part_off = block * c->block_size;
    // might not be needed
    memset(mem + part_off, 1, c->block_size);
    return 0;
}

static int littlefs_api_sync(const struct lfs_config *c) {
    /* Unnecessary for esp-idf */
    return 0;
}

// endregion

// region public api

esp_err_t esp_littlefs_ram_create(lfs_t ** lfs, size_t size) {

    void * mem = calloc(1, size);

    if (mem == NULL)
        return ESP_ERR_NO_MEM;

    struct lfs_config config = { 0 };
    {/* LittleFS Configuration */
        config.context = mem;

        // block device operations
        config.read  = littlefs_api_read;
        config.prog  = littlefs_api_prog;
        config.erase = littlefs_api_erase;
        config.sync  = littlefs_api_sync;

        // block device configuration
        config.read_size = 128;
        config.prog_size = 128;
        config.block_size = 4096;;
        config.block_count = size / config.block_size;
        config.cache_size = 512;
        config.lookahead_size = 128;
        config.block_cycles = -1;
    }
    return esp_littlefs_abs_create(lfs, &config, true, free);
}
esp_err_t esp_littlefs_ram_delete(lfs_t ** lfs) {
    return esp_littlefs_abs_delete(lfs);
}

// endregion
