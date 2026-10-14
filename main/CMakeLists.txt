idf_component_register(
    SRCS "main.c"
         "coin_pusher_app.c"
         "pusher_physics.c"
         "coin_assets.c"
         "coin_save.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_hw_support nvs_flash esp_timer
)
