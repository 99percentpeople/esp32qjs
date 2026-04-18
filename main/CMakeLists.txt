idf_component_register(
    SRCS
        "app_main.c"
        "repl.c"
        "repl_input.c"
    INCLUDE_DIRS "."
    REQUIRES mquickjs esp_driver_usb_serial_jtag
)
