PROJECT_NAME := littlefs

EXTRA_COMPONENT_DIRS := \
	$(abspath .) \
	$(abspath unit_tester) \
	$(IDF_PATH)/tools/unit-test-app/components/

CFLAGS += \
		-Werror

include $(IDF_PATH)/make/project.mk

.PHONY: tests

tests:
	$(MAKE) \
		TEST_COMPONENTS='src' \
		flash monitor;

