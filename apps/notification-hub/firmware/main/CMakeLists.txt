idf_component_register(
    SRCS "app_main.c" "hub_protocol.c"
    INCLUDE_DIRS "."
    REQUIRES bsp bt nvs_flash esp_timer
)
