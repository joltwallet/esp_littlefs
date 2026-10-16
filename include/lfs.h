/*
 * Shim for consuming the raw LittleFS API from the esp_littlefs component.
 *
 * Gated behind an explicit opt-in: enable CONFIG_LITTLEFS_EXPOSE_RAW_API in
 * menuconfig (Component config -> LittleFS), or define ESP_LITTLEFS_RAW_API
 * in the consuming code's build flags.
 */
#ifndef ESP_LITTLEFS_LFS_SHIM_H
#define ESP_LITTLEFS_LFS_SHIM_H

#if !defined(ESP_LITTLEFS_RAW_API)
#include "sdkconfig.h"
#if !defined(CONFIG_LITTLEFS_EXPOSE_RAW_API)
#error "esp_littlefs: the raw LittleFS API is not enabled. Set CONFIG_LITTLEFS_EXPOSE_RAW_API=y (Component config -> LittleFS) or define ESP_LITTLEFS_RAW_API to opt in. The raw API bypasses the VFS layer's locking; do not mount the same partition through VFS and the raw API at the same time."
#endif
#endif

#ifndef LFS_CONFIG
#define LFS_CONFIG lfs_config.h
#endif

#include "../src/littlefs/lfs.h"

#endif
