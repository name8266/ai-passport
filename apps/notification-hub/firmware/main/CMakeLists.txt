idf_component_register(
    SRCS "app_main.c" "hub_protocol.c" "hub_backlog.c" "hub_archive.c"
         "hub_ai.c" "hub_response.c" "hub_ai_settings.c" "hub_ai_web.c" "hub_form.c" "hub_ui.c" "hub_alert.c" "hub_sound.c"
         "../../../../assets/fonts/notification_hub_16.c"
         "../../../../assets/fonts/notification_hub_24.c"
    INCLUDE_DIRS "."
    REQUIRES bsp bt nvs_flash esp_timer fatfs wear_levelling esp_partition
             esp_wifi esp_netif esp_event esp_http_client esp_http_server mbedtls json
)

# Keep the legacy Bluedroid transport as a compile-time rollback option.
if(CONFIG_BT_NIMBLE_ENABLED)
    target_sources(${COMPONENT_LIB} PRIVATE "hub_ble_nimble.c")
endif()
