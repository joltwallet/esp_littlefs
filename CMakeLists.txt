cmake_minimum_required(VERSION 3.10)

if (NOT DEFINED PROJECT_NAME)
    include($ENV{IDF_PATH}/tools/cmake/project.cmake)
    project(esp_littlefs)
else ()
    list(APPEND SOURCES src/esp_littlefs_flash.c src/esp_littlefs_ram.c src/esp_littlefs_sdcard.c src/esp_littlefs_vfs.c src/esp_littlefs_abs.c src/littlefs/lfs.c src/littlefs/lfs_util.c)
    idf_component_register(
        SRCS ${SOURCES}
        INCLUDE_DIRS include
        PRIV_INCLUDE_DIRS src
        REQUIRES spi_flash sdmmc)
endif (NOT DEFINED PROJECT_NAME)
