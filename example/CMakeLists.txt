# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

# Add the root of this git repo to the component search path.
set(EXTRA_COMPONENT_DIRS "../")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(demo_esp_littlefs)

# Note: you must have a partition named the first argument (here it's "littlefs")
# in your partition table csv file.
littlefs_create_partition_image(littlefs flash_data)
