# esp_blockdev only exists in ESP-IDF 6.0+.
if("${IDF_VERSION_MAJOR}" VERSION_LESS "6")
    set(exclude_srcs "test_littlefs_blockdev.c")
endif()

idf_component_register(SRC_DIRS "."
                    EXCLUDE_SRCS ${exclude_srcs}
                    INCLUDE_DIRS "."
                    REQUIRES spi_flash unity test_utils littlefs spiffs fatfs esp_timer vfs)

target_add_binary_data(${COMPONENT_TARGET} "./testfs.bin" BINARY)
