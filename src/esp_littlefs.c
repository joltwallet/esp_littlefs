/**
 * @file esp_littlefs.c
 * @brief Maps LittleFS <-> ESP_VFS 
 * @author Brian Pugh
 */

//#define ESP_LOCAL_LOG_LEVEL 4

#include "esp_log.h"
#include "esp_spi_flash.h"
#include "esp_image_format.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <unistd.h>
#include <dirent.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/lock.h>
#include "esp32/rom/spi_flash.h"
#include "esp_system.h"

#include "esp_littlefs.h"
#include "littlefs_api.h"

static const char TAG[] = "esp_littlefs";

#define ABSOLUTE_MAX_NUM_FILES 20
#define CONFIG_LITTLEFS_BLOCK_SIZE 4096 /* ESP32 can only operate at 4kb */

/**
 * @brief littlefs DIR structure
 */
typedef struct {
    DIR dir;            /*!< VFS DIR struct */
    lfs_dir_t d;        /*!< littlefs DIR struct */
    struct dirent e;    /*!< Last open dirent */
    long offset;        /*!< Offset of the current dirent */
    char *path;         /*!< Requested directory name */
} vfs_littlefs_dir_t;

static int     vfs_littlefs_open(void* ctx, const char * path, int flags, int mode);
static ssize_t vfs_littlefs_write(void* ctx, int fd, const void * data, size_t size);
static ssize_t vfs_littlefs_read(void* ctx, int fd, void * dst, size_t size);
static int     vfs_littlefs_close(void* ctx, int fd);
static off_t   vfs_littlefs_lseek(void* ctx, int fd, off_t offset, int mode);
static int     vfs_littlefs_fstat(void* ctx, int fd, struct stat * st);
static int     vfs_littlefs_stat(void* ctx, const char * path, struct stat * st);
static int     vfs_littlefs_unlink(void* ctx, const char *path);
static int     vfs_littlefs_rename(void* ctx, const char *src, const char *dst);
static DIR*    vfs_littlefs_opendir(void* ctx, const char* name);
static int     vfs_littlefs_closedir(void* ctx, DIR* pdir);
static struct  dirent* vfs_littlefs_readdir(void* ctx, DIR* pdir);
static int     vfs_littlefs_readdir_r(void* ctx, DIR* pdir,
                                struct dirent* entry, struct dirent** out_dirent);
static long    vfs_littlefs_telldir(void* ctx, DIR* pdir);
static void    vfs_littlefs_seekdir(void* ctx, DIR* pdir, long offset);
static int     vfs_littlefs_mkdir(void* ctx, const char* name, mode_t mode);
static int     vfs_littlefs_rmdir(void* ctx, const char* name);
static int     vfs_littlefs_utime(void *ctx, const char *path, const struct utimbuf *times);

static esp_err_t esp_littlefs_init(const esp_vfs_littlefs_conf_t* conf);
static esp_err_t esp_littlefs_erase_partition(const char *partition_label);
static esp_err_t esp_littlefs_by_label(const char* label, int * index);
static esp_err_t esp_littlefs_get_empty(int *index);
static void esp_littlefs_free(esp_littlefs_t ** efs);
static int esp_littlefs_free_fd(esp_littlefs_t *efs, int fd);
static int esp_littlefs_get_fd(esp_littlefs_t *efs);
static int esp_littlefs_get_fd_by_name(esp_littlefs_t *efs, const char *path);
static void esp_littlefs_dir_free(vfs_littlefs_dir_t *dir);
static int esp_littlefs_flags_conv(int m);
static void vfs_littlefs_update_mtime(esp_littlefs_t *efs, const char *path);
static int vfs_littlefs_update_mtime_value(esp_littlefs_t *efs, const char *path, time_t t);
static time_t vfs_littlefs_get_mtime(esp_littlefs_t *efs, const char *path);

static int sem_take(esp_littlefs_t *efs);
static int sem_give(esp_littlefs_t *efs);

static SemaphoreHandle_t _efs_lock = NULL;
static esp_littlefs_t * _efs[CONFIG_LITTLEFS_MAX_PARTITIONS] = { 0 };

/********************
 * Public Functions *
 ********************/

bool esp_littlefs_mounted(const char* partition_label) {
    int index;
    esp_err_t err;
    esp_littlefs_t *efs = NULL;

    err = esp_littlefs_by_label(partition_label, &index);
    if(err != ESP_OK) return false;
    efs = _efs[index];
    return efs->mounted;
}

esp_err_t esp_littlefs_info(const char* partition_label, size_t *total_bytes, size_t *used_bytes){
    int index;
    esp_err_t err;
    esp_littlefs_t *efs = NULL;

    err = esp_littlefs_by_label(partition_label, &index);
    if(err != ESP_OK) return false;
    efs = _efs[index];

    if(total_bytes) *total_bytes = efs->cfg.block_size * efs->cfg.block_count; 
    if(used_bytes) *used_bytes = efs->cfg.block_size * lfs_fs_size(efs->fs);

    return ESP_OK;
}

esp_err_t esp_vfs_littlefs_register(const esp_vfs_littlefs_conf_t * conf)
{
    assert(conf->base_path);
    const esp_vfs_t vfs = {
        .flags       = ESP_VFS_FLAG_CONTEXT_PTR,
        .write_p     = &vfs_littlefs_write,
        .lseek_p     = &vfs_littlefs_lseek,
        .read_p      = &vfs_littlefs_read,
        .open_p      = &vfs_littlefs_open,
        .close_p     = &vfs_littlefs_close,
        .fstat_p     = &vfs_littlefs_fstat,
        .stat_p      = &vfs_littlefs_stat,
        .link_p      = NULL, /* Not Supported */
        .unlink_p    = &vfs_littlefs_unlink,
        .rename_p    = &vfs_littlefs_rename,
        .opendir_p   = &vfs_littlefs_opendir,
        .closedir_p  = &vfs_littlefs_closedir,
        .readdir_p   = &vfs_littlefs_readdir,
        .readdir_r_p = &vfs_littlefs_readdir_r,
        .seekdir_p   = &vfs_littlefs_seekdir,
        .telldir_p   = &vfs_littlefs_telldir,
        .mkdir_p     = &vfs_littlefs_mkdir,
        .rmdir_p     = &vfs_littlefs_rmdir,
#ifdef CONFIG_LITTLEFS_USE_MTIME
        .utime_p     = &vfs_littlefs_utime,
#else
        .utime_p     = NULL,
#endif // CONFIG_LITTLEFS_USE_MTIME
    };

    esp_err_t err = esp_littlefs_init(conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize LittleFS");
        return err;
    }

    int index;
    if (esp_littlefs_by_label(conf->partition_label, &index) != ESP_OK) {
        ESP_LOGE(TAG, "Unable to find partition \"%s\"", conf->partition_label);
        return ESP_ERR_NOT_FOUND;
    }

    strlcat(_efs[index]->base_path, conf->base_path, ESP_VFS_PATH_MAX + 1);
    err = esp_vfs_register(conf->base_path, &vfs, _efs[index]);
    if (err != ESP_OK) {
        esp_littlefs_free(&_efs[index]);
        ESP_LOGE(TAG, "Failed to register Littlefs to \"%s\"", conf->base_path);
        return err;
    }

    ESP_LOGD(TAG, "Successfully registered LittleFS to \"%s\"", conf->base_path);
    return ESP_OK;
}

esp_err_t esp_vfs_littlefs_unregister(const char* partition_label)
{
    assert(partition_label);
    int index;
    if (esp_littlefs_by_label(partition_label, &index) != ESP_OK) {
        ESP_LOGE(TAG, "Partition was never registered.");
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGD(TAG, "Unregistering \"%s\"", partition_label);
    esp_err_t err = esp_vfs_unregister(_efs[index]->base_path);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to unregister \"%s\"", partition_label);
        return err;
    }
    esp_littlefs_free(&_efs[index]);
    return ESP_OK;
}

esp_err_t esp_littlefs_format(const char* partition_label) {
    assert( partition_label );

    bool was_mounted = false;
    bool efs_free = false;
    int index = -1;
    esp_err_t err;
    esp_littlefs_t *efs = NULL;

    ESP_LOGI(TAG, "Formatting \"%s\"", partition_label);

    /* Get a context */
    err = esp_littlefs_by_label(partition_label, &index);

    if( err != ESP_OK ){
        /* Create a tmp context */
        ESP_LOGD(TAG, "Temporarily creating EFS context.");
        efs_free = true;
        const esp_vfs_littlefs_conf_t conf = {
                /* base_name not necessary for initializing */
                .dont_mount = true, 
                .partition_label = partition_label,
                .max_files = 1
        };
        err = esp_littlefs_init(&conf); /* Internally MIGHT esp_littlefs_format */
        if( err != ESP_OK ) {
            ESP_LOGE(TAG, "Failed to initialize to format.");
            goto exit;
        }

        err = esp_littlefs_by_label(partition_label, &index);
        if ( err != ESP_OK) {
            ESP_LOGE(TAG, "Error obtaining context.");
            goto exit;
        }
    }

    efs = _efs[index];
    assert( efs );

    /* Unmount if mounted */
    if(efs->mounted){
        int res;
        ESP_LOGD(TAG, "Partition was mounted. Unmounting...");
        was_mounted = true;
        res = lfs_unmount(efs->fs);
        if(res != LFS_ERR_OK){
            ESP_LOGE(TAG, "Failed to unmount.");
            return ESP_FAIL;
        }
        efs->mounted = false;
    }

    /* Erase and Format */
    {
        int res;
        ESP_LOGD(TAG, "Formatting filesystem");
        esp_littlefs_erase_partition(partition_label);
        res = lfs_format(efs->fs, &efs->cfg);
        if( res != LFS_ERR_OK ) {
            ESP_LOGE(TAG, "Failed to format filesystem");
            return ESP_FAIL;
        }
    }

    /* Mount filesystem */
    if( was_mounted ) {
        int res;
        /* Remount the partition */
        ESP_LOGD(TAG, "Remounting formatted partition");
        res = lfs_mount(efs->fs, &efs->cfg);
        if( res != LFS_ERR_OK ) {
            ESP_LOGE(TAG, "Failed to re-mount filesystem");
            return ESP_FAIL;
        }
    }
    ESP_LOGD(TAG, "Format Success!");
    
    err = ESP_OK;

exit:
    if(efs_free && index>=0) esp_littlefs_free(&_efs[index]);
    return err;
}

/**
 * @brief converts an enumerated lfs error into a string.
 * @param lfs_error The littlefs error.
 */
const char * esp_littlefs_errno(enum lfs_error lfs_errno) {
    switch(lfs_errno){
        case LFS_ERR_OK: return "LFS_ERR_OK";
        case LFS_ERR_IO: return "LFS_ERR_IO";
        case LFS_ERR_CORRUPT: return "LFS_ERR_CORRUPT";
        case LFS_ERR_NOENT: return "LFS_ERR_NOENT";
        case LFS_ERR_EXIST: return "LFS_ERR_EXIST";
        case LFS_ERR_NOTDIR: return "LFS_ERR_NOTDIR";
        case LFS_ERR_ISDIR: return "LFS_ERR_ISDIR";
        case LFS_ERR_NOTEMPTY: return "LFS_ERR_NOTEMPTY";
        case LFS_ERR_BADF: return "LFS_ERR_BADF";
        case LFS_ERR_FBIG: return "LFS_ERR_FBIG";
        case LFS_ERR_INVAL: return "LFS_ERR_INVAL";
        case LFS_ERR_NOSPC: return "LFS_ERR_NOSPC";
        case LFS_ERR_NOMEM: return "LFS_ERR_NOMEM";
        case LFS_ERR_NOATTR: return "LFS_ERR_NOATTR";
        case LFS_ERR_NAMETOOLONG: return "LFS_ERR_NAMETOOLONG";
        default: return "LFS_ERR_UNDEFINED";
    }
}


/********************
 * Static Functions *
 ********************/

/*** Helpers ***/

/**
 * @brief Free and clear a littlefs definition structure.
 * @param efs Pointer to pointer to struct. Done this way so we can also zero
 *            out the pointer.
 */
static void esp_littlefs_free(esp_littlefs_t ** efs)
{
    esp_littlefs_t * e = *efs;
    if (e == NULL) return;
    *efs = NULL;

    if (e->fs) {
        if(e->mounted) lfs_unmount(e->fs);
        free(e->fs);
    }
    if(e->lock) vSemaphoreDelete(e->lock);
    if(e->files) free(e->files);
    free(e);
}

/**
 * @brief Free a vfs_littlefs_dir_t struct.
 */
static void esp_littlefs_dir_free(vfs_littlefs_dir_t *dir){
    if(dir == NULL) return;
    if(dir->path) free(dir->path);
    free(dir);
}

/**
 * Get a mounted littlefs filesystem by label.
 * @param[in] label
 * @param[out] index index into _efs
 * @return ESP_OK on success
 */
static esp_err_t esp_littlefs_by_label(const char* label, int * index){
    int i;
    esp_littlefs_t * p;

    if(!label || !index) return ESP_ERR_INVALID_ARG;

    ESP_LOGD(TAG, "Searching for existing filesystem for partition \"%s\"", label);

    for (i = 0; i < CONFIG_LITTLEFS_MAX_PARTITIONS; i++) {
        p = _efs[i];
        if (p) {
            if (strncmp(label, p->partition->label, 17) == 0) {
                *index = i;
                ESP_LOGD(TAG, "Found existing filesystem \"%s\" at index %d", label, *index);
                return ESP_OK;
            }
        }
    }

    ESP_LOGD(TAG, "Existing filesystem \%s\" not found", label);
    return ESP_ERR_NOT_FOUND;
}

/**
 * @brief Get the index of an unallocated LittleFS slot.
 * @param[out] index Indexd of free LittleFS slot 
 * @return ESP_OK on success
 */
static esp_err_t esp_littlefs_get_empty(int *index) {
    assert(index);
    for(uint8_t i=0; i < CONFIG_LITTLEFS_MAX_PARTITIONS; i++){
        if( _efs[i] == NULL ){
            *index = i;
            return ESP_OK;
        }
    }
    ESP_LOGE(TAG, "No more free partitions available.");
    return ESP_FAIL;
}

/**
 * @brief erase a partition; make sure LittleFS is unmounted first.
 * @param partition_label NULL-terminated string of partition to erase
 * @return ESP_OK on success
 */
static esp_err_t esp_littlefs_erase_partition(const char *partition_label) {
    ESP_LOGD(TAG, "Erasing partition...");

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

/**
 * @brief Convert fcntl flags to littlefs flags
 * @param m fcntl flags
 * @return lfs flags
 */
static int esp_littlefs_flags_conv(int m) {
    int lfs_flags = 0;
    if (m == O_APPEND) lfs_flags |= LFS_O_APPEND;
    if (m == O_RDONLY) lfs_flags |= LFS_O_RDONLY;
    if (m & O_WRONLY)  lfs_flags |= LFS_O_WRONLY;
    if (m & O_RDWR)    lfs_flags |= LFS_O_RDWR;
    if (m & O_EXCL)    lfs_flags |= LFS_O_EXCL;
    if (m & O_CREAT)   lfs_flags |= LFS_O_CREAT;
    if (m & O_TRUNC)   lfs_flags |= LFS_O_TRUNC;
    return lfs_flags;
}

/**
 * @brief Initialize and mount littlefs 
 * @param[in] conf Filesystem Configuration
 * @return ESP_OK on success
 */
static esp_err_t esp_littlefs_init(const esp_vfs_littlefs_conf_t* conf)
{
    int index = -1;
    esp_err_t err = ESP_FAIL;
    const esp_partition_t* partition = NULL;
    esp_littlefs_t * efs = NULL;

    if( _efs_lock == NULL ){
        static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
        portENTER_CRITICAL(&mux);
        if( _efs_lock == NULL ){
            _efs_lock = xSemaphoreCreateMutex();
            assert(_efs_lock);
        }
        portEXIT_CRITICAL(&mux);
    }

    xSemaphoreTake(_efs_lock, portMAX_DELAY);

    if (esp_littlefs_get_empty(&index) != ESP_OK) {
        ESP_LOGE(TAG, "max mounted partitions reached");
        err = ESP_ERR_INVALID_STATE;
        goto exit;
    }

    /* Input and Environment Validation */
    if (esp_littlefs_by_label(conf->partition_label, &index) == ESP_OK) {
        ESP_LOGE(TAG, "Partition already used");
        err = ESP_ERR_INVALID_STATE;
        goto exit;
    }

	{
        uint32_t flash_page_size = g_rom_flashchip.page_size;
        uint32_t log_page_size = CONFIG_LITTLEFS_PAGE_SIZE;
        if (log_page_size % flash_page_size != 0) {
            ESP_LOGE(TAG, "LITTLEFS_PAGE_SIZE is not multiple of flash chip page size (%d)",
                    flash_page_size);
            err = ESP_ERR_INVALID_ARG;
            goto exit;
        }
    }

    if(conf->max_files > ABSOLUTE_MAX_NUM_FILES || conf->max_files <= 0) {
        ESP_LOGE(TAG, "Max files must be in range (0, %d]. Provided %d", ABSOLUTE_MAX_NUM_FILES, conf->max_files);
        err = ESP_ERR_INVALID_ARG;
        goto exit;
    }

    if ( NULL == conf->partition_label ) {
        ESP_LOGE(TAG, "Partition label must be provided.");
        err = ESP_ERR_INVALID_ARG;
        goto exit;
    }

    partition = esp_partition_find_first(
            ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY,
            conf->partition_label);

    if (!partition) {
        ESP_LOGE(TAG, "partition \"%s\" could not be found", conf->partition_label);
        err = ESP_ERR_NOT_FOUND;
        goto exit;
    }

    if (partition->encrypted) {
        // TODO: allow encryption; should probably be fine,
        // just not allowing until tested.
        ESP_LOGE(TAG, "littlefs can not run on encrypted partition");
        err = ESP_ERR_INVALID_STATE;
        goto exit;
    }

    /* Allocate Context */
    efs = calloc(1, sizeof(esp_littlefs_t));
    if (efs == NULL) {
        ESP_LOGE(TAG, "esp_littlefs could not be malloced");
        err = ESP_ERR_NO_MEM;
        goto exit;
    }
    efs->partition = partition;
    efs->max_files = conf->max_files;

    { /* LittleFS Configuration */
        efs->cfg.context = efs;

        // block device operations
        efs->cfg.read  = littlefs_api_read;
        efs->cfg.prog  = littlefs_api_prog;
        efs->cfg.erase = littlefs_api_erase;
        efs->cfg.sync  = littlefs_api_sync;

        // block device configuration
        efs->cfg.read_size = CONFIG_LITTLEFS_READ_SIZE;
        efs->cfg.prog_size = CONFIG_LITTLEFS_WRITE_SIZE;
        efs->cfg.block_size = CONFIG_LITTLEFS_BLOCK_SIZE;; 
        efs->cfg.block_count = efs->partition->size / efs->cfg.block_size;
        efs->cfg.cache_size = CONFIG_LITTLEFS_CACHE_SIZE;
        efs->cfg.lookahead_size = CONFIG_LITTLEFS_LOOKAHEAD_SIZE;
        efs->cfg.block_cycles = CONFIG_LITTLEFS_BLOCK_CYCLES;
    }

    efs->lock = xSemaphoreCreateRecursiveMutex();
    if (efs->lock == NULL) {
        ESP_LOGE(TAG, "mutex lock could not be created");
        err = ESP_ERR_NO_MEM;
        goto exit;
    }

    efs->fs = calloc(1, sizeof(lfs_t));
    if (efs->fs == NULL) {
        ESP_LOGE(TAG, "littlefs could not be malloced");
        err = ESP_ERR_NO_MEM;
        goto exit;
    }

    efs->files = calloc(conf->max_files, sizeof(vfs_littlefs_file_t));
    if( efs->files == NULL ){
        ESP_LOGE(TAG, "file descriptor buffers could not be malloced");
        err = ESP_ERR_NO_MEM;
        goto exit;
    }

    // Mount and Error Check
    _efs[index] = efs;
    if(!conf->dont_mount){
        int res;
        res = lfs_mount(efs->fs, &efs->cfg);

        if (conf->format_if_mount_failed && res != LFS_ERR_OK) {
            esp_err_t err;
            ESP_LOGW(TAG, "mount failed, %i (%s). formatting...", res, esp_littlefs_errno(res));
            err = esp_littlefs_format(efs->partition->label);
            if(err != ESP_OK) {
                ESP_LOGE(TAG, "format failed");
                err = ESP_FAIL;
                goto exit;
            }
            res = lfs_mount(efs->fs, &efs->cfg);
        }
        if (res != LFS_ERR_OK) {
            ESP_LOGE(TAG, "mount failed, %i (%s)", res, esp_littlefs_errno(res));
            err = ESP_FAIL;
            goto exit;
        }
        efs->mounted = true;
    }

    err = ESP_OK;

exit:
    if(err != ESP_OK){
        if( index >= 0 ) {
            esp_littlefs_free(&_efs[index]);
        }
        else{
            esp_littlefs_free(&efs);
        }
    }
    xSemaphoreGive(_efs_lock);
    return err;
}

/**
 * @brief
 * @parameter efs file system context
 */
static int sem_take(esp_littlefs_t *efs) {
    return xSemaphoreTakeRecursive(efs->lock, portMAX_DELAY);
}

/**
 * @brief
 * @parameter efs file system context
 */
static int sem_give(esp_littlefs_t *efs) {
    return xSemaphoreGiveRecursive(efs->lock);
}

/**
 * @brief Get a file descriptor
 * @param[in,out] efs file system context
 * @return integer file descriptor. Returns -1 if a FD cannot be obtained.
 */
static int esp_littlefs_get_fd(esp_littlefs_t *efs){
    sem_take(efs);
    ESP_LOGD(TAG, "Searching for a free FD [0,%d). fd_used mask: 0x%08X",
            efs->max_files, (uint32_t)efs->fd_used);
    for(uint8_t i=0; i < efs->max_files; i++){
        bool used;
        used = (efs->fd_used >> i) & 1;
        if( !used ){
            efs->fd_used |= 1 << i;
            sem_give(efs);
            ESP_LOGD(TAG, "Obtained free FD %d. fd_used mask: 0x%08x",
                    i, (uint32_t)efs->fd_used);
            return i;
        }
    }
    sem_give(efs);
    ESP_LOGE(TAG, "Unable to get a free FD");
    return -1;
}

/**
 * @brief Release a file descriptor
 * @param[in,out] efs file system context
 * @param[in] fd File Descriptor to release
 * @return 0 on success. -1 if a FD cannot be obtained.
 */
static int esp_littlefs_free_fd(esp_littlefs_t *efs, int fd){
    if(fd > ABSOLUTE_MAX_NUM_FILES || fd < 0) {
        ESP_LOGE(TAG, "Max files must be <%d.", ABSOLUTE_MAX_NUM_FILES);
        return -1;
    }

    sem_take(efs);
    if(!((efs->fd_used >> fd) && 0x01)) {
        sem_give(efs);
        ESP_LOGE(TAG, "FD was never allocated");
        return -1;
    }

    ESP_LOGD(TAG, "Clearing FD");
    memset(&efs->files[fd], 0, sizeof(vfs_littlefs_file_t));
    efs->fd_used &= ~(1 << fd);
    sem_give(efs);

    return 0;
}

/**
 * @brief finds an open file descriptor by file name.
 * @param[in,out] efs file system context
 * @param[in] path File path to check.
 * @returns integer file descriptor. Returns -1 if not found.
 */
static int esp_littlefs_get_fd_by_name(esp_littlefs_t *efs, const char *path){
    sem_take(efs);
    for(uint8_t i=0; i < efs->max_files; i++){
        bool used;
        used = (efs->fd_used >> i) & 1;
        if( used && 0 == strcmp(path, efs->files[i].path) ){
            ESP_LOGD(TAG, "Found \"%s\" at FD %d.", path, i);
            return i;
        }
    }
    sem_give(efs);
    ESP_LOGD(TAG, "Unable to get a find FD for \"%s\"", path);
    return -1;
}

/*** Filesystem Hooks***/

static int vfs_littlefs_open(void* ctx, const char * path, int flags, int mode) {
    /* Note: mode is currently unused */
    int fd=-1, lfs_flags, res;
    esp_littlefs_t *efs = (esp_littlefs_t *)ctx;
    vfs_littlefs_file_t *file = NULL;

    assert(path);

    ESP_LOGD(TAG, "Opening %s", path);

    /* Convert flags to lfs flags */
    lfs_flags = esp_littlefs_flags_conv(flags);

    /* Get a FD */
    sem_take(efs);
    fd = esp_littlefs_get_fd(efs);
    if(fd < 0) {
        sem_give(efs);
        ESP_LOGE(TAG, "Error obtaining FD");
        res = LFS_ERR_INVAL;
        goto exit;
    }
    file = &efs->files[fd];

    /* Open File */
    res = lfs_file_open(efs->fs, &file->file, path, lfs_flags);

    if( res < 0 ) {
        sem_give(efs);
        ESP_LOGE(TAG, "Failed to open file. Error %s (%d)",
                esp_littlefs_errno(res), res);
        res = LFS_ERR_INVAL;
        goto exit;
    }
    strlcpy(file->path, path, sizeof(file->path));

    if (!(lfs_flags & LFS_O_RDONLY)) {
        /* If this is being opened as not read-only */
        vfs_littlefs_update_mtime(efs, path);
    }

    sem_give(efs);

    return fd;

exit:
    if(fd>=0) esp_littlefs_free_fd(efs, fd);
    return res;
}

static ssize_t vfs_littlefs_write(void* ctx, int fd, const void * data, size_t size) {
    esp_littlefs_t * efs = (esp_littlefs_t *)ctx;
    ssize_t res;
    vfs_littlefs_file_t *file = NULL;

    if(fd > ABSOLUTE_MAX_NUM_FILES || fd < 0) {
        ESP_LOGE(TAG, "Max files must be <%d.", ABSOLUTE_MAX_NUM_FILES);
        return LFS_ERR_BADF;
    }

    sem_take(efs);
    file = &efs->files[fd];
    res = lfs_file_write(efs->fs, &file->file, data, size);
    sem_give(efs);

    if(res < 0){
        ESP_LOGE(TAG, "Failed to write file \"%s\". Error %s (%d)",
                file->path, esp_littlefs_errno(res), res);
        return res;
    }

    return res;
}

static ssize_t vfs_littlefs_read(void* ctx, int fd, void * dst, size_t size) {
    esp_littlefs_t * efs = (esp_littlefs_t *)ctx;
    ssize_t res;
    vfs_littlefs_file_t *file = NULL;

    if(fd > ABSOLUTE_MAX_NUM_FILES || fd < 0) {
        ESP_LOGE(TAG, "FD must be <%d.", ABSOLUTE_MAX_NUM_FILES);
        return LFS_ERR_BADF;
    }

    sem_take(efs);
    file = &efs->files[fd];
    res = lfs_file_read(efs->fs, &file->file, dst, size);
    sem_give(efs);

    if(res < 0){
        ESP_LOGE(TAG, "Failed to read file \"%s\". Error %s (%d)",
                file->path, esp_littlefs_errno(res), res);
        return res;
    }

    return res;
}

static int vfs_littlefs_close(void* ctx, int fd) {
    esp_littlefs_t * efs = (esp_littlefs_t *)ctx;
    int res;
    vfs_littlefs_file_t *file = NULL;

    if(fd > ABSOLUTE_MAX_NUM_FILES || fd < 0) {
        ESP_LOGE(TAG, "FD must be <%d.", ABSOLUTE_MAX_NUM_FILES);
        return LFS_ERR_BADF;
    }

    sem_take(efs);
    file = &efs->files[fd];
    res = lfs_file_close(efs->fs, &file->file);
    if(res < 0){
        sem_give(efs);
        ESP_LOGE(TAG, "Failed to close file \"%s\". Error %s (%d)",
                file->path, esp_littlefs_errno(res), res);
        return res;
    }
    esp_littlefs_free_fd(efs, fd);
    sem_give(efs);
    return res;
}

static off_t vfs_littlefs_lseek(void* ctx, int fd, off_t offset, int mode) {
    esp_littlefs_t * efs = (esp_littlefs_t *)ctx;
    lfs_soff_t res;
    vfs_littlefs_file_t *file = NULL;
    int whence;

    if(fd > ABSOLUTE_MAX_NUM_FILES || fd < 0) {
        ESP_LOGE(TAG, "Max files must be <%d.", ABSOLUTE_MAX_NUM_FILES);
        return LFS_ERR_BADF;
    }
    file = &efs->files[fd];

    switch (mode) {
        case SEEK_SET: whence = LFS_SEEK_SET; break;
        case SEEK_CUR: whence = LFS_SEEK_CUR; break;
        case SEEK_END: whence = LFS_SEEK_END; break;
        default: 
            ESP_LOGE(TAG, "Invalid mode");
            return -1;
    }

    sem_take(efs);
    res = lfs_file_seek(efs->fs, &file->file, offset, whence);
    sem_give(efs);

    if(res < 0){
        ESP_LOGE(TAG, "Failed to seek file \"%s\" to offset %08x. Error %s (%d)",
                file->path, (unsigned int)offset, esp_littlefs_errno(res), res);
        return res;
    }

    return res;
}

static int vfs_littlefs_fstat(void* ctx, int fd, struct stat * st) {
    esp_littlefs_t * efs = (esp_littlefs_t *)ctx;
    struct lfs_info info;
    int res;
    vfs_littlefs_file_t *file = NULL;

    if(fd > ABSOLUTE_MAX_NUM_FILES || fd < 0) {
        ESP_LOGE(TAG, "Max files must be <%d.", ABSOLUTE_MAX_NUM_FILES);
        return LFS_ERR_BADF;
    }
    file = &efs->files[fd];

    memset(st, 0, sizeof(struct stat));
    st->st_blksize = efs->cfg.block_size;

    sem_take(efs);
    res = lfs_stat(efs->fs, file->path, &info);
    sem_give(efs);
    if (res < 0) {
        ESP_LOGE(TAG, "Failed to stat file \"%s\". Error %s (%d)",
                file->path, esp_littlefs_errno(res), res);
        return res;
    }
    st->st_size = info.size;
    st->st_mtime = vfs_littlefs_get_mtime(efs, file->path);
    st->st_mode = ((info.type==LFS_TYPE_REG)?S_IFREG:S_IFDIR);
    return 0;
}

static int vfs_littlefs_stat(void* ctx, const char * path, struct stat * st) {
    assert(path);
    esp_littlefs_t * efs = (esp_littlefs_t *)ctx;
    struct lfs_info info;
    int res;

    memset(st, 0, sizeof(struct stat));
    st->st_blksize = efs->cfg.block_size;

    sem_take(efs);
    res = lfs_stat(efs->fs, path, &info);
    sem_give(efs);
    if (res < 0) {
        /* Not strictly an error, since stat can be used to check
         * if a file exists */
        ESP_LOGI(TAG, "Failed to stat path \"%s\". Error %s (%d)",
                path, esp_littlefs_errno(res), res);
        return res;
    }
    st->st_size = info.size;
    st->st_mtime = vfs_littlefs_get_mtime(efs, path);
    st->st_mode = ((info.type==LFS_TYPE_REG)?S_IFREG:S_IFDIR);
    return 0;
}

static int vfs_littlefs_unlink(void* ctx, const char *path) {
#define fail_str_1 "Failed to unlink path \"%s\"."
    assert(path);
    esp_littlefs_t * efs = (esp_littlefs_t *)ctx;
    struct lfs_info info;
    int res;

    sem_take(efs);
    res = lfs_stat(efs->fs, path, &info);
    if (res < 0) {
        sem_give(efs);
        ESP_LOGE(TAG, fail_str_1 " Error %s (%d)",
                path, esp_littlefs_errno(res), res);
        return res;
    }

    if(esp_littlefs_get_fd_by_name(efs, path) >= 0) {
        sem_give(efs);
        ESP_LOGE(TAG, fail_str_1 " Has open FD.", path);
        return -1;
    }

    if (info.type == LFS_TYPE_DIR) {
        sem_give(efs);
        ESP_LOGE(TAG, "Cannot unlink a directory.");
        return LFS_ERR_ISDIR;
    }

    res = lfs_remove(efs->fs, path);
    if (res < 0) {
        sem_give(efs);
        ESP_LOGE(TAG, fail_str_1 " Error %s (%d)",
                path, esp_littlefs_errno(res), res);
        return res;
    }

    sem_give(efs);

    return 0;
#undef fail_str_1
}

static int vfs_littlefs_rename(void* ctx, const char *src, const char *dst) {
    esp_littlefs_t * efs = (esp_littlefs_t *)ctx;
    int res;

    sem_take(efs);

    if(esp_littlefs_get_fd_by_name(efs, src) >= 0){
        sem_give(efs);
        ESP_LOGE(TAG, "Cannot rename; src \"%s\" is open.", src);
        return -1;
    }
    else if(esp_littlefs_get_fd_by_name(efs, src) >= 0){
        sem_give(efs);
        ESP_LOGE(TAG, "Cannot rename; dst \"%s\" is open.", dst);
        return -1;
    }


    res = lfs_rename(efs->fs, src, dst);
    sem_give(efs);
    if (res < 0) {
        ESP_LOGE(TAG, "Failed to rename \"%s\" -> \"%s\". Error %s (%d)",
                src, dst, esp_littlefs_errno(res), res);
        return res;
    }

    return 0;
}

static DIR* vfs_littlefs_opendir(void* ctx, const char* name) {
    esp_littlefs_t * efs = (esp_littlefs_t *)ctx;
    int res;
    vfs_littlefs_dir_t *dir = NULL;

    dir = calloc(1, sizeof(vfs_littlefs_dir_t));
    if( dir == NULL ) {
        ESP_LOGE(TAG, "dir struct could not be malloced");
        goto exit;
    }

    dir->path = strdup(name);
    if(dir->path == NULL){
        ESP_LOGE(TAG, "dir path name could not be malloced");
        goto exit;
    }

    sem_take(efs);
    res = lfs_dir_open(efs->fs, &dir->d, dir->path);
    sem_give(efs);
    if (res < 0) {
        ESP_LOGE(TAG, "Failed to opendir \"%s\". Error %s (%d)",
                dir->path, esp_littlefs_errno(res), res);
        goto exit;
    }

    return (DIR *)dir;

exit:
    esp_littlefs_dir_free(dir);
    return NULL;
}

static int vfs_littlefs_closedir(void* ctx, DIR* pdir) {
    assert(pdir);
    esp_littlefs_t * efs = (esp_littlefs_t *)ctx;
    vfs_littlefs_dir_t * dir = (vfs_littlefs_dir_t *) pdir;
    int res;

    sem_take(efs);
    res = lfs_dir_close(efs->fs, &dir->d);
    sem_give(efs);
    if (res < 0) {
        ESP_LOGE(TAG, "Failed to closedir \"%s\". Error %s (%d)",
                dir->path, esp_littlefs_errno(res), res);
        return res;
    }

    esp_littlefs_dir_free(dir);
    return 0;
}

static struct dirent* vfs_littlefs_readdir(void* ctx, DIR* pdir) {
    assert(pdir);
    vfs_littlefs_dir_t * dir = (vfs_littlefs_dir_t *) pdir;
    int res;
    struct dirent* out_dirent;

    res = vfs_littlefs_readdir_r(ctx, pdir, &dir->e, &out_dirent);
    if (res != 0) return NULL;
    return out_dirent;
}

static int vfs_littlefs_readdir_r(void* ctx, DIR* pdir,
        struct dirent* entry, struct dirent** out_dirent) {
    assert(pdir);
    esp_littlefs_t * efs = (esp_littlefs_t *)ctx;
    vfs_littlefs_dir_t * dir = (vfs_littlefs_dir_t *) pdir;
    int res;
    struct lfs_info info = { 0 };

    sem_take(efs);
    do{ /* Read until we get a real object name */
        res = lfs_dir_read(efs->fs, &dir->d, &info);
    }while( res>0 && (strcmp(info.name, ".") == 0 || strcmp(info.name, "..") == 0));
    sem_give(efs);
    if (res < 0) {
        ESP_LOGE(TAG, "Failed to readdir \"%s\". Error %s (%d)",
                dir->path, esp_littlefs_errno(res), res);
        return -1;
    }

    if(info.type == LFS_TYPE_REG) {
        ESP_LOGD(TAG, "readdir a file of size %d named \"%s\"",
                info.size, info.name);
    }
    else {
        ESP_LOGD(TAG, "readdir a dir named \"%s\"", info.name);
    }

    if(res == 0) {
        /* End of Objs */
        ESP_LOGD(TAG, "Reached the end of the directory.");
        *out_dirent = NULL;
    }
    else {
        entry->d_ino = 0;
        entry->d_type = info.type == LFS_TYPE_REG ? DT_REG : DT_DIR;
        strncpy(entry->d_name, info.name, sizeof(entry->d_name));
        *out_dirent = entry;
    }
    dir->offset++;

    return 0;
}

static long vfs_littlefs_telldir(void* ctx, DIR* pdir) {
    assert(pdir);
    vfs_littlefs_dir_t * dir = (vfs_littlefs_dir_t *) pdir;
    return dir->offset;
}

static void vfs_littlefs_seekdir(void* ctx, DIR* pdir, long offset) {
    assert(pdir);
    esp_littlefs_t * efs = (esp_littlefs_t *)ctx;
    vfs_littlefs_dir_t * dir = (vfs_littlefs_dir_t *) pdir;
    int res;

    if (offset < dir->offset) {
        /* close and re-open dir to rewind to beginning */
        sem_take(efs);
        res = lfs_dir_rewind(efs->fs, &dir->d);
        sem_give(efs);
        if (res < 0) {
            ESP_LOGE(TAG, "Failed to rewind dir \"%s\". Error %s (%d)",
                    dir->path, esp_littlefs_errno(res), res);
            return;
        }
        dir->offset = 0;
    }

    while(dir->offset < offset){
        struct dirent *out_dirent;
        res = vfs_littlefs_readdir_r(ctx, pdir, &dir->e, &out_dirent);
        if( res != 0 ){
            ESP_LOGE(TAG, "Error readdir_r");
            return;
        }
    }
}

static int vfs_littlefs_mkdir(void* ctx, const char* name, mode_t mode) {
    /* Note: mode is currently unused */
    esp_littlefs_t * efs = (esp_littlefs_t *)ctx;
    int res;
    ESP_LOGD(TAG, "mkdir \"%s\"", name);

    sem_take(efs);
    res = lfs_mkdir(efs->fs, name);
    sem_give(efs);
    if (res < 0) {
        ESP_LOGE(TAG, "Failed to mkdir \"%s\". Error %s (%d)",
                name, esp_littlefs_errno(res), res);
        return res;
    }
    return 0;
}

static int vfs_littlefs_rmdir(void* ctx, const char* name) {
    esp_littlefs_t * efs = (esp_littlefs_t *)ctx;
    struct lfs_info info;
    int res;

    /* Error Checking */
    sem_take(efs);
    res = lfs_stat(efs->fs, name, &info);
    if (res < 0) {
        sem_give(efs);
        ESP_LOGE(TAG, "\"%s\" doesn't exist.", name);
        return -1;
    }

    if (info.type != LFS_TYPE_DIR) {
        sem_give(efs);
        ESP_LOGE(TAG, "\"%s\" is not a directory.", name);
        return -1;
    }

    /* Unlink the dir */
    res = lfs_remove(efs->fs, name);
    sem_give(efs);
    if ( res < 0) {
        ESP_LOGE(TAG, "Failed to unlink path \"%s\". Error %s (%d)",
                name, esp_littlefs_errno(res), res);
        return -1;
    }

    return 0;
}

/**
 * Sets the mtime attr to t.
 */
static int vfs_littlefs_update_mtime_value(esp_littlefs_t *efs, const char *path, time_t t)
{
    int res;
    res = lfs_setattr(efs->fs, path, LITTLEFS_ATTR_MTIME,
            &t, sizeof(t));
    if( res < 0 ) {
        ESP_LOGE(TAG, "Failed to update mtime (%d)", res);
    }

    return res;
}

/**
 * Sets the mtime attr to an appropriate value
 */
static void vfs_littlefs_update_mtime(esp_littlefs_t *efs, const char *path)
{
#if CONFIG_LITTLEFS_USE_MTIME
    vfs_littlefs_utime(efs, path, NULL);
#endif //CONFIG_LITTLEFS_USE_MTIME
}


#if CONFIG_LITTLEFS_USE_MTIME
static int vfs_littlefs_utime(void *ctx, const char *path, const struct utimbuf *times)
{
    esp_littlefs_t * efs = (esp_littlefs_t *)ctx;
    time_t t;

    assert(path);

    if (times) {
        t = times->modtime;
    } else {
#if CONFIG_LITTLEFS_MTIME_USE_SECONDS
        // use current time
        t = time(NULL);
#elif CONFIG_LITTLEFS_MTIME_USE_NONCE
        assert( sizeof(time_t) == 4 );
        t = vfs_littlefs_get_mtime(efs, path);
        if( 0 == t ) t = esp_random();
        else t += 1;

        if( 0 == t ) t = 1;
#else
#error "Invalid MTIME configuration"
#endif
    }

    int ret = vfs_littlefs_update_mtime_value(efs, path, t);
    return ret;
}

static time_t vfs_littlefs_get_mtime(esp_littlefs_t *efs, const char *path)
{
    time_t t = 0;
    int size;
    size = lfs_getattr(efs->fs, path, LITTLEFS_ATTR_MTIME,
            &t, sizeof(t));
    if( size < 0 ) {
        ESP_LOGI(TAG, "Failed to get mtime attribute %s (%d)",
                esp_littlefs_errno(size), size);
    }
    return t;
}
#endif //CONFIG_LITTLEFS_USE_MTIME
