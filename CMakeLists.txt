cmake_minimum_required(VERSION 3.10)

if (NOT DEFINED PROJECT_NAME)
    include($ENV{IDF_PATH}/tools/cmake/project.cmake)
    project(esp_littlefs)
endif (NOT DEFINED PROJECT_NAME)

list(APPEND SOURCES
        src/esp_littlefs_flash.c
        src/esp_littlefs_ram.c
        src/esp_littlefs_sdcard.c
        src/esp_littlefs_vfs.c
        src/esp_littlefs_abs.c
        src/littlefs/lfs.c
        src/littlefs/lfs_util.c
        )
idf_component_register(
        SRCS ${SOURCES}
        INCLUDE_DIRS src include
        PRIV_INCLUDE_DIRS src
        REQUIRES spi_flash sdmmc
        PRIV_REQUIRES esptool_py)

set_source_files_properties(
        ${SOURCES}
        PROPERTIES COMPILE_FLAGS "-DLFS_CONFIG=lfs_config.h"
)
