idf_component_register(
    SRCS "main.c" "netsec/core.c" "netsec/radio.c" "netsec/ble.c"
         "netsec/discovery.c" "netsec/ui.c"
         "../assets/fonts/netsec_font_14.c"
    INCLUDE_DIRS "."
    REQUIRES bsp bt esp_event esp_hw_support esp_netif esp_timer esp_wifi
             nvs_flash esp_driver_usb_serial_jtag vfs lwip
)
