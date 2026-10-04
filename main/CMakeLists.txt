idf_component_register(
    SRCS "stock_app.c" "stock_core.c" "stock_network.c" "stock_gbk.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_event esp_netif esp_timer esp_wifi nvs_flash esp_http_client esp_http_server json mbedtls
)
target_sources(${COMPONENT_LIB} PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/stock_font_16.c"
)
