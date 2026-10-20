idf_component_register(
    SRCS "main.c" "brake_hero.c" "brake_scores.c" "brake_app.c"
         "brake_render.c" "brake_vehicle.c" "brake_display_math.c" "brake_runtime.c" "brake_sound.c"
         "../assets/images/brake-hero/converted/brake_images.c"
         "../assets/fonts/brake-hero/brake_font.c"
         "../assets/music/brake-hero/brake_audio.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_timer esp_hw_support nvs_flash
)
