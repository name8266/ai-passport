idf_component_register(
    SRCS "app_main.c" "nav_packet.c"
    INCLUDE_DIRS "."
    REQUIRES bsp bt esp_timer nvs_flash
)
