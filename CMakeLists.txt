cmake_minimum_required(VERSION 3.16.0)

if (NOT DEFINED PROJECT_NAME)
    include($ENV{IDF_PATH}/tools/cmake/project.cmake)
    project(esp_littlefs)
else ()
    file(GLOB_RECURSE SOURCES src/*.cpp)
    idf_component_register(SRCS ${SOURCES} INCLUDE_DIRS src)
endif (NOT DEFINED PROJECT_NAME)
