cmake_minimum_required(VERSION 3.10)

if (NOT DEFINED PROJECT_NAME)
    include($ENV{IDF_PATH}/tools/cmake/project.cmake)
    project(esp_littlefs)
else ()
    file(GLOB SOURCES src/littlefs/*.c)
    list(APPEND SOURCES src/esp_littlefs.c src/littlefs_api.c)
    idf_component_register(
        SRCS ${SOURCES}
        INCLUDE_DIRS src include
        REQUIRES spi_flash)
endif (NOT DEFINED PROJECT_NAME)
