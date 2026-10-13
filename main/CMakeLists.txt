idf_component_register(
    SRCS "life_lab_v2.c"
         "avida_vm.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_hw_support esp_timer spi_flash
)
