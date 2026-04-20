set(MQUICKJS_DIR "${CMAKE_CURRENT_LIST_DIR}/../../mquickjs")
set(MQUICKJS_GENERATED_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/generated")
set(MQUICKJS_GENERATED_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(MQUICKJS_HOST_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/host_tools")
set(MQUICKJS_HOST_TOOL "${MQUICKJS_HOST_BUILD_DIR}/mqjs_stdlib")
set(MQUICKJS_RUN_TOOL_TO_FILE_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/cmake/run_tool_to_file.cmake")

if(DEFINED ENV{HOSTCC} AND NOT "$ENV{HOSTCC}" STREQUAL "")
    set(MQUICKJS_HOST_CC "$ENV{HOSTCC}")
else()
    find_program(MQUICKJS_HOST_CC NAMES cc gcc clang REQUIRED NO_CACHE)
endif()

set(MQUICKJS_HOST_CFLAGS
    -Wall
    -g
    -MMD
    -D_GNU_SOURCE
    -fno-math-errno
    -fno-trapping-math
    -O2
)

set(MQUICKJS_HOST_LDFLAGS
    -g
)

set(MQUICKJS_ATOM_HEADER "${MQUICKJS_GENERATED_BINARY_DIR}/mquickjs_atom.h")
set(MQUICKJS_STDLIB_HEADER "${MQUICKJS_GENERATED_BINARY_DIR}/mqjs_stdlib.h")

file(MAKE_DIRECTORY "${MQUICKJS_GENERATED_BINARY_DIR}")

if(NOT CMAKE_SCRIPT_MODE_FILE)
    add_custom_command(
        OUTPUT "${MQUICKJS_HOST_TOOL}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${MQUICKJS_HOST_BUILD_DIR}"
        COMMAND ${MQUICKJS_HOST_CC}
                ${MQUICKJS_HOST_CFLAGS}
                -c
                -o "${MQUICKJS_HOST_BUILD_DIR}/mqjs_stdlib.host.o"
                "${MQUICKJS_DIR}/mqjs_stdlib.c"
        COMMAND ${MQUICKJS_HOST_CC}
                ${MQUICKJS_HOST_CFLAGS}
                -c
                -o "${MQUICKJS_HOST_BUILD_DIR}/mquickjs_build.host.o"
                "${MQUICKJS_DIR}/mquickjs_build.c"
        COMMAND ${MQUICKJS_HOST_CC}
                ${MQUICKJS_HOST_LDFLAGS}
                -o "${MQUICKJS_HOST_TOOL}"
                "${MQUICKJS_HOST_BUILD_DIR}/mqjs_stdlib.host.o"
                "${MQUICKJS_HOST_BUILD_DIR}/mquickjs_build.host.o"
        DEPENDS
            "${MQUICKJS_DIR}/mqjs_stdlib.c"
            "${MQUICKJS_DIR}/mquickjs_build.c"
            "${MQUICKJS_DIR}/mquickjs.h"
            "${MQUICKJS_DIR}/cutils.h"
        VERBATIM
    )

    add_custom_command(
        OUTPUT
            "${MQUICKJS_ATOM_HEADER}"
            "${MQUICKJS_STDLIB_HEADER}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${MQUICKJS_GENERATED_BINARY_DIR}"
        COMMAND ${CMAKE_COMMAND}
                -DOUTPUT_FILE=${MQUICKJS_ATOM_HEADER}
                -DTOOL=${MQUICKJS_HOST_TOOL}
                -DARG0=-a
                -DARG1=-m32
                -P "${MQUICKJS_RUN_TOOL_TO_FILE_SCRIPT}"
        COMMAND ${CMAKE_COMMAND}
                -DOUTPUT_FILE=${MQUICKJS_STDLIB_HEADER}
                -DTOOL=${MQUICKJS_HOST_TOOL}
                -DARG0=-m32
                -P "${MQUICKJS_RUN_TOOL_TO_FILE_SCRIPT}"
        DEPENDS
            "${MQUICKJS_HOST_TOOL}"
            "${MQUICKJS_RUN_TOOL_TO_FILE_SCRIPT}"
        VERBATIM
    )

    add_custom_target(mquickjs_generated_headers
        DEPENDS
            "${MQUICKJS_ATOM_HEADER}"
            "${MQUICKJS_STDLIB_HEADER}"
    )

    add_custom_command(
        OUTPUT
            "${MQUICKJS_GENERATED_SOURCE_DIR}/mquickjs_atom.h"
            "${MQUICKJS_GENERATED_SOURCE_DIR}/mqjs_stdlib.h"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${MQUICKJS_GENERATED_SOURCE_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MQUICKJS_ATOM_HEADER}"
                "${MQUICKJS_GENERATED_SOURCE_DIR}/mquickjs_atom.h"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MQUICKJS_STDLIB_HEADER}"
                "${MQUICKJS_GENERATED_SOURCE_DIR}/mqjs_stdlib.h"
        DEPENDS mquickjs_generated_headers
        VERBATIM
    )

    add_custom_target(update_mquickjs_headers
        DEPENDS
            "${MQUICKJS_GENERATED_SOURCE_DIR}/mquickjs_atom.h"
            "${MQUICKJS_GENERATED_SOURCE_DIR}/mqjs_stdlib.h"
    )
endif()

idf_component_register(
    SRCS
        "${MQUICKJS_DIR}/cutils.c"
        "${MQUICKJS_DIR}/dtoa.c"
        "${MQUICKJS_DIR}/libm.c"
        "${MQUICKJS_DIR}/mquickjs.c"
    INCLUDE_DIRS
        "${MQUICKJS_GENERATED_BINARY_DIR}"
        "${MQUICKJS_DIR}"
)

if(NOT CMAKE_SCRIPT_MODE_FILE)
    add_dependencies(${COMPONENT_LIB} update_mquickjs_headers)
endif()

target_compile_definitions(${COMPONENT_LIB} PRIVATE _GNU_SOURCE)
target_compile_options(
    ${COMPONENT_LIB}
    PRIVATE
        -fno-math-errno
        -fno-trapping-math
        -Wno-error=format
        -Wno-error=type-limits
)
