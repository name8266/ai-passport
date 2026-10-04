idf_component_register(
    SRCS "main.c" "worldcam_protocol.c" "worldcam_mjpeg.c" "worldcam_catalog.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_event esp_netif esp_timer esp_wifi nvs_flash esp_http_client esp_http_server json mbedtls
)

target_sources(${COMPONENT_LIB} PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/worldcam_font_16.c"
    "${CMAKE_CURRENT_LIST_DIR}/../assets/maps/world_map.c"
)
