idf_component_register(
    SRCS "esp32qjs.c"
    INCLUDE_DIRS "."
    REQUIRES mquickjs esp_driver_usb_serial_jtag
)
