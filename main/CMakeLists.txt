idf_component_register(
    SRCS "slot_app.c"
         "slot_core.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_hw_support esp_timer nvs_flash
)
