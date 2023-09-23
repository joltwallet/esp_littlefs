cmake_minimum_required(VERSION 3.10)

file(GLOB SOURCES src/littlefs/*.c)
list(APPEND SOURCES src/esp_littlefs.c src/littlefs_api.c src/lfs_config.c)

if(IDF_VERSION_MAJOR GREATER_EQUAL 5)
    list(APPEND pub_requires esp_partition)
else()
    list(APPEND pub_requires spi_flash)
endif()
list(APPEND priv_requires esptool_py spi_flash vfs)

idf_component_register(
    SRCS ${SOURCES}
    INCLUDE_DIRS include
    PRIV_INCLUDE_DIRS src
    REQUIRES ${pub_requires}
    PRIV_REQUIRES ${priv_requires}
)

set_source_files_properties(
    ${SOURCES}
    PROPERTIES COMPILE_FLAGS "-DLFS_CONFIG=lfs_config.h"
)

if(CONFIG_LITTLEFS_FCNTL_GET_PATH)
    target_compile_definitions(${COMPONENT_LIB} PUBLIC -DF_GETPATH=${CONFIG_LITTLEFS_FCNTL_F_GETPATH_VALUE})
endif()

if(CONFIG_LITTLEFS_MULTIVERSION)
    target_compile_definitions(${COMPONENT_LIB} PUBLIC -DLFS_MULTIVERSION)
endif()
