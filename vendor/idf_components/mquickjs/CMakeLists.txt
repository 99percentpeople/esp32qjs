set(MQUICKJS_DIR "${CMAKE_CURRENT_LIST_DIR}/../../mquickjs")
set(MQUICKJS_GENERATED_DIR "${CMAKE_CURRENT_LIST_DIR}/generated")

idf_component_register(
    SRCS
        "${MQUICKJS_DIR}/cutils.c"
        "${MQUICKJS_DIR}/dtoa.c"
        "${MQUICKJS_DIR}/libm.c"
        "${MQUICKJS_DIR}/mquickjs.c"
    INCLUDE_DIRS
        "${MQUICKJS_GENERATED_DIR}"
        "${MQUICKJS_DIR}"
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE _GNU_SOURCE)
target_compile_options(
    ${COMPONENT_LIB}
    PRIVATE
        -fno-math-errno
        -fno-trapping-math
        -Wno-error=format
        -Wno-error=type-limits
)
