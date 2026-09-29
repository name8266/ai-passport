idf_component_register(
    SRCS "main.c"
         "tarot_model.c"
         "tarot_runtime.c"
         "tarot_catalog.c"
         "tarot_history.c"
         "tarot_store.c"
         "tarot_audio.c"
         "fonts/tarot_font_14.c"
         "fonts/tarot_font_20.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_hw_support esp_timer nvs_flash
    EMBED_FILES "assets/tarot_cards.rgb565"
)
