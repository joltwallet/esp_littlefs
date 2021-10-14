cmake_minimum_required(VERSION 3.10)

project(littlefs)

file(GLOB SOURCES src/littlefs/*.c)
list(APPEND SOURCES src/esp_littlefs.c src/littlefs_api.c)
idf_component_register(
    SRCS ${SOURCES}
    INCLUDE_DIRS src include
    REQUIRES spi_flash)
