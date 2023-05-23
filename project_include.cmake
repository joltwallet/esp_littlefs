
# littlefs_create_partition_image
#
# Create a littlefs image of the specified directory on the host during build and optionally
# have the created image flashed using `idf.py flash`
set(MKLITTLEFS_DIR "${CMAKE_CURRENT_LIST_DIR}/mklittlefs")
set(MKLITTLEFS "${MKLITTLEFS_DIR}/mklittlefs")

function(littlefs_create_partition_image partition base_dir)
	set(options FLASH_IN_PROJECT)
	set(multi DEPENDS)
	cmake_parse_arguments(arg "${options}" "" "${multi}" "${ARGN}")

	idf_build_get_property(idf_path IDF_PATH)

	get_filename_component(base_dir_full_path ${base_dir} ABSOLUTE)

	partition_table_get_partition_info(size "--partition-name ${partition}" "size")
	partition_table_get_partition_info(offset "--partition-name ${partition}" "offset")

	add_custom_command(
		OUTPUT ${MKLITTLEFS}
		COMMAND make clean && make dist LFS_NAME_MAX=${CONFIG_LITTLEFS_OBJ_NAME_LEN}
		WORKING_DIRECTORY ${MKLITTLEFS_DIR}
		DEPENDS ${SDKCONFIG}
	)

	if("${size}" AND "${offset}")
		set(image_file ${CMAKE_BINARY_DIR}/${partition}.bin)

		# Execute LittleFS image generation; this always executes as there is no way to specify for CMake to watch for
		# contents of the base dir changing.
		add_custom_target(littlefs_${partition}_bin ALL
			COMMAND ${MKLITTLEFS} -d 0 -c ${base_dir_full_path} -s ${size} -p ${CONFIG_LITTLEFS_PAGE_SIZE} -b 4096 ${image_file}
			DEPENDS ${arg_DEPENDS} ${MKLITTLEFS}
			)

		set_property(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" APPEND PROPERTY
			ADDITIONAL_MAKE_CLEAN_FILES
			${image_file})

		set(IDF_VER_NO_V "${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}")

		if(${IDF_VER_NO_V} VERSION_LESS 4.1)
			message(WARNING "Unsupported/unmaintained/deprecated ESP-IDF version ${IDF_VER}")
		endif()

		if(${IDF_VER_NO_V} VERSION_GREATER_EQUAL 4.2)
			idf_component_get_property(main_args esptool_py FLASH_ARGS)
			idf_component_get_property(sub_args esptool_py FLASH_SUB_ARGS)
			esptool_py_flash_target(${partition}-flash "${main_args}" "${sub_args}")
			esptool_py_flash_target_image(${partition}-flash "${partition}" "${offset}" "${image_file}")

			add_dependencies(${partition}-flash littlefs_${partition}_bin)

			if(arg_FLASH_IN_PROJECT)
				esptool_py_flash_target_image(flash "${partition}" "${offset}" "${image_file}")
				add_dependencies(flash littlefs_${partition}_bin)
			endif()
		else()
			if(arg_FLASH_IN_PROJECT)
				esptool_py_flash_project_args("${partition}" "${offset}" "${image_file}" FLASH_IN_PROJECT)
			else()
				esptool_py_flash_project_args("${partition}" "${offset}" "${image_file}")
			endif()
		endif()

	else()
		set(message "Failed to create littlefs image for partition '${partition}'. "
					"Check project configuration if using the correct partition table file."
		)
		fail_at_build_time(littlefs_${partition}_bin "${message}")
	endif()
endfunction()
