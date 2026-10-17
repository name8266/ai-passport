idf_component_register(
    SRCS "main.c"
         "kinetic_core.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_timer esp_system esp_hw_support freertos lvgl
)
target_link_libraries(${COMPONENT_LIB} PUBLIC m)
