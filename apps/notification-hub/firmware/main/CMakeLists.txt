idf_component_register(
    SRCS "app_main.c" "hub_protocol.c" "hub_archive.c"
         "hub_ai.c" "hub_ai_web.c"
    INCLUDE_DIRS "."
    REQUIRES bsp bt nvs_flash esp_timer fatfs wear_levelling esp_partition
             esp_wifi esp_netif esp_event esp_http_client esp_http_server mbedtls json
)
