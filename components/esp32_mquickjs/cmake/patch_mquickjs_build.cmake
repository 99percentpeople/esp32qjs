if(NOT DEFINED INPUT_FILE OR INPUT_FILE STREQUAL "")
    message(FATAL_ERROR "INPUT_FILE is required")
endif()

if(NOT DEFINED OUTPUT_FILE OR OUTPUT_FILE STREQUAL "")
    message(FATAL_ERROR "OUTPUT_FILE is required")
endif()

if(NOT EXISTS "${INPUT_FILE}")
    message(FATAL_ERROR "Input file not found: ${INPUT_FILE}")
endif()

file(READ "${INPUT_FILE}" BUILD_TOOL_SOURCE)
string(REPLACE "#define ATOM_ALIGN 64" "#define ATOM_ALIGN 256" BUILD_TOOL_SOURCE "${BUILD_TOOL_SOURCE}")

if(BUILD_TOOL_SOURCE STREQUAL "")
    message(FATAL_ERROR "Failed to read or patch ${INPUT_FILE}")
endif()

cmake_path(GET OUTPUT_FILE PARENT_PATH OUTPUT_DIR)
if(OUTPUT_DIR)
    file(MAKE_DIRECTORY "${OUTPUT_DIR}")
endif()

file(WRITE "${OUTPUT_FILE}" "${BUILD_TOOL_SOURCE}")
