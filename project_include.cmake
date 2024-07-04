
# littlefs_create_partition_image
#
# Create a littlefs image of the specified directory on the host during build and optionally
# have the created image flashed using `idf.py flash`

set(littlefs_py_venv "${CMAKE_CURRENT_BINARY_DIR}/littlefs_py_venv")
set(littlefs_py_requirements "${CMAKE_CURRENT_LIST_DIR}/image-building-requirements.txt")

set_directory_properties(PROPERTIES
    ADDITIONAL_CLEAN_FILES "${littlefs_py_venv}"
)

function(littlefs_create_partition_image partition base_dir)
	set(options FLASH_IN_PROJECT)
	set(multi DEPENDS)
	cmake_parse_arguments(arg "${options}" "" "${multi}" "${ARGN}")

	idf_build_get_property(idf_path IDF_PATH)

	get_filename_component(base_dir_full_path ${base_dir} ABSOLUTE)

	partition_table_get_partition_info(size "--partition-name ${partition}" "size")
	partition_table_get_partition_info(offset "--partition-name ${partition}" "offset")

	if("${size}" AND "${offset}")
		set(image_file ${CMAKE_BINARY_DIR}/${partition}.bin)

		if(CMAKE_HOST_WIN32)
			set(littlefs_py "${littlefs_py_venv}/Scripts/littlefs-python.exe")
			add_custom_command(
					OUTPUT ${littlefs_py_venv}
					COMMAND ${PYTHON} -m venv ${littlefs_py_venv} && ${littlefs_py_venv}/Scripts/pip.exe install -r ${littlefs_py_requirements}
					WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
					DEPENDS ${littlefs_py_requirements}
			)
		else()
			set(littlefs_py "${littlefs_py_venv}/bin/littlefs-python")
			add_custom_command(
					OUTPUT ${littlefs_py_venv}
					COMMAND ${PYTHON} -m venv ${littlefs_py_venv} && ${littlefs_py_venv}/bin/pip install -r ${littlefs_py_requirements}
					WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
					DEPENDS ${littlefs_py_requirements}
			)
		endif()

		file(WRITE ${CMAKE_BINARY_DIR}/parse_littlefs_version.cmake "
file(READ \${INPUT_FILE} littlefs_version_output)
string(REGEX MATCH \"[0-9]+\\\\.[0-9]+\" littlefs_version_number \"\${littlefs_version_output}\")
string(REPLACE \".\" \";\" version_list \${littlefs_version_number})
list(GET version_list 0 major_version)
list(GET version_list 1 minor_version)
if(\${major_version} EQUAL 0 AND \${minor_version} LESS 11)
	set(create_fs_command \"${littlefs_py} create ${base_dir_full_path} --image=${image_file} -v --fs-size=${size} --name-max=${CONFIG_LITTLEFS_OBJ_NAME_LEN} --block-size=4096\")
else()
	set(create_fs_command \"${littlefs_py} create ${base_dir_full_path} ${image_file} -v --fs-size=${size} --name-max=${CONFIG_LITTLEFS_OBJ_NAME_LEN} --block-size=4096\")
endif()
file(WRITE ${CMAKE_BINARY_DIR}/create_fs_command.txt \"\${create_fs_command}\")
")
		add_custom_command(
			OUTPUT ${CMAKE_BINARY_DIR}/create_fs_command.txt
			COMMAND ${littlefs_py} --version > ${CMAKE_BINARY_DIR}/littlefs_version.txt
			COMMAND ${CMAKE_COMMAND} -DINPUT_FILE=${CMAKE_BINARY_DIR}/littlefs_version.txt -P ${CMAKE_BINARY_DIR}/parse_littlefs_version.cmake
			DEPENDS ${littlefs_py_venv}
		)
		# Execute LittleFS image generation; this always executes as there is no way to specify for CMake to watch for
		# contents of the base dir changing.

		add_custom_target(littlefs_${partition}_bin ALL
			COMMAND ${create_fs_command}
			DEPENDS ${arg_DEPENDS} ${littlefs_py_venv} ${CMAKE_BINARY_DIR}/create_fs_command.txt
		)

		set_property(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" APPEND PROPERTY
			ADDITIONAL_MAKE_CLEAN_FILES
			${image_file})

		set(IDF_VER_NO_V "${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}")

		if(${IDF_VER_NO_V} VERSION_LESS 4.3)
			message(WARNING "Unsupported/unmaintained/deprecated ESP-IDF version ${IDF_VER}")
		endif()

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
		set(message "Failed to create littlefs image for partition '${partition}'. "
					"Check project configuration if using the correct partition table file."
		)
		fail_at_build_time(littlefs_${partition}_bin "${message}")
	endif()
endfunction()
