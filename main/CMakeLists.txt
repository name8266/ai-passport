idf_component_register(
    SRCS "main.c"
         "coin_pusher_app.c"
         "pusher_physics.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_hw_support nvs_flash
)
