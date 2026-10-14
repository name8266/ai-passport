# Standalone game app: keep original demo sources in the repository for BSP reference.
idf_component_register(
    SRCS "pachinko_app.c" "pachinko_engine.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_hw_support esp_timer nvs_flash lvgl
)
