idf_component_register(
    SRCS "life_lab.c"
         "binary_life.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_hw_support esp_timer nvs_flash
)
