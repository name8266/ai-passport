idf_component_register(
    SRCS "stock_app.c" "stock_core.c" "stock_preview.c" "stock_network.c" "stock_gbk.c" "stock_web.c" "stock_sound.c" "stock_wifi.c"
    EMBED_TXTFILES "../assets/web/stock_manager.html"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_event esp_netif esp_timer esp_wifi nvs_flash esp_http_client esp_http_server json mbedtls
)
set(stock_wifi_header_dir "${CMAKE_CURRENT_BINARY_DIR}/stock-private")
set(stock_wifi_generator "${CMAKE_CURRENT_LIST_DIR}/../tools/stock_wifi_config.py")
add_custom_target(stock_wifi_header ALL
    COMMAND "${PYTHON}" "${stock_wifi_generator}"
        --output "${stock_wifi_header_dir}/stock_wifi_build.h"
        --repository "${CMAKE_SOURCE_DIR}" --from-environment
    BYPRODUCTS "${stock_wifi_header_dir}/stock_wifi_build.h"
    VERBATIM)
add_dependencies(${COMPONENT_LIB} stock_wifi_header)
target_include_directories(${COMPONENT_LIB} PRIVATE "${stock_wifi_header_dir}")
target_sources(${COMPONENT_LIB} PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/stock_font_16.c"
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/stock_font_12.c"
)
