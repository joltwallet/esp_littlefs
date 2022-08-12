#
# Component Makefile
#

COMPONENT_SRCDIRS := src src/littlefs

COMPONENT_ADD_INCLUDEDIRS := include

COMPONENT_PRIV_INCLUDEDIRS := src

COMPONENT_SUBMODULES := src/littlefs

CFLAGS += \
	-DLFS_CONFIG=lfs_config.h
