# Standalone Pachinko application. BSP hardware implementation is reused unchanged.
idf_component_register(
    SRCS "pachinko_app.c" "pachinko_engine.c" "pachinko_rewards.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_hw_support esp_timer nvs_flash
)
