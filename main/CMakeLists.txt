idf_component_register(
    SRCS "main.c" "battery_model.c" "battery_protocol.c" "battery_store.c" "battery_web.c" "battery_ui.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_event esp_netif esp_timer esp_wifi esp_http_server nvs_flash json
)
set(WEB_GZIP "${CMAKE_CURRENT_BINARY_DIR}/battery.html.gz")
add_custom_command(OUTPUT "${WEB_GZIP}"
    COMMAND ${PYTHON} "${CMAKE_CURRENT_LIST_DIR}/../tools/compress_web.py"
            "${CMAKE_CURRENT_LIST_DIR}/web/index.html" "${WEB_GZIP}"
    DEPENDS "${CMAKE_CURRENT_LIST_DIR}/web/index.html" "${CMAKE_CURRENT_LIST_DIR}/../tools/compress_web.py"
    VERBATIM)
add_custom_target(battery_web_asset DEPENDS "${WEB_GZIP}")
add_dependencies(${COMPONENT_LIB} battery_web_asset)
target_add_binary_data(${COMPONENT_LIB} "${WEB_GZIP}" BINARY)
