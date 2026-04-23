set(srcs
    "src/runtime.c"
    "src/repl_line.c"
    "src/repl.c"
    "src/repl_input.c"
)

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "internal"
    PRIV_REQUIRES
    esp_driver_usb_serial_jtag
    esp_timer
)

target_link_libraries(${COMPONENT_LIB} PRIVATE
    idf::esp_driver_usb_serial_jtag
    idf::esp_timer
)
