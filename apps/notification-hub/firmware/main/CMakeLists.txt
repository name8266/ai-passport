idf_component_register(
    SRCS "app_main.c" "hub_protocol.c" "hub_archive.c"
    INCLUDE_DIRS "."
    REQUIRES bsp bt nvs_flash esp_timer fatfs wear_levelling esp_partition
)
