# Configure-time transforms must rerun when any implementation or registry input
# changes, including shared Python helpers imported by multiple adapters.
file(GLOB_RECURSE _sdk_patch_inputs CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/scripts/sdk_patches/*.py"
    "${CMAKE_SOURCE_DIR}/scripts/sdk_patches/*.json")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${_sdk_patch_inputs} "${CMAKE_SOURCE_DIR}/scripts/sdk_patch.py"
    "${CMAKE_SOURCE_DIR}/scripts/tool_paths.py")
idf_build_get_property(_sdk_patch_python PYTHON)
set(_sdk_patch_include "${CMAKE_BINARY_DIR}/esp32qjs_sdk_fixes/patches.cmake")
execute_process(COMMAND "${_sdk_patch_python}" "${CMAKE_SOURCE_DIR}/scripts/sdk_patch.py"
    cmake --output "${_sdk_patch_include}"
    RESULT_VARIABLE _sdk_patch_result ERROR_VARIABLE _sdk_patch_error)
if(NOT _sdk_patch_result EQUAL 0)
    message(FATAL_ERROR "SDK patch registry failed: ${_sdk_patch_error}")
endif()
include("${_sdk_patch_include}")
