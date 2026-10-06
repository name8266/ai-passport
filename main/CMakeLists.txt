idf_component_register(
    SRCS "slot_app.c"
         "slot_core.c"
         "multi_slot_core.c"
         "chance_core.c"
         "motion_core.c"
         "plinko_core.c"
         "light_core.c"
         "tiny3d_math.c"
         "rigid_body.c"
         "tiny3d_render.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_hw_support esp_timer esp_driver_usb_serial_jtag
)

target_sources(${COMPONENT_LIB} PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../assets/images/slot_symbols.c"
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/arcade_font_14.c"
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/arcade_font_20.c"
)
