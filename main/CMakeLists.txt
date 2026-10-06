idf_component_register(
    SRCS "slot_app.c"
         "slot_core.c"
         "chance_core.c"
         "motion_core.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_hw_support esp_timer
)
