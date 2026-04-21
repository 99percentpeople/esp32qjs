if(NOT DEFINED OUTPUT_FILE OR OUTPUT_FILE STREQUAL "")
    message(FATAL_ERROR "OUTPUT_FILE is required")
endif()

if(NOT DEFINED TOOL OR TOOL STREQUAL "")
    message(FATAL_ERROR "TOOL is required")
endif()

if(NOT EXISTS "${TOOL}")
    message(FATAL_ERROR "Tool not found: ${TOOL}")
endif()

cmake_path(GET OUTPUT_FILE PARENT_PATH OUTPUT_DIR)
if(OUTPUT_DIR)
    file(MAKE_DIRECTORY "${OUTPUT_DIR}")
endif()

set(COMMAND_ARGS)
foreach(ARG_INDEX RANGE 0 7)
    if(DEFINED ARG${ARG_INDEX})
        list(APPEND COMMAND_ARGS "${ARG${ARG_INDEX}}")
    endif()
endforeach()

execute_process(
    COMMAND "${TOOL}" ${COMMAND_ARGS}
    OUTPUT_FILE "${OUTPUT_FILE}"
    RESULT_VARIABLE COMMAND_RESULT
)

if(NOT COMMAND_RESULT EQUAL 0)
    message(FATAL_ERROR "Command failed (${COMMAND_RESULT}): ${TOOL} ${COMMAND_ARGS}")
endif()
