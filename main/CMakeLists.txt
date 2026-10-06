idf_component_register(
    SRCS "slot_app.c"
         "slot_core.c"
         "chance_core.c"
         "motion_core.c"
         "light_core.c"
         "tiny3d_math.c"
         "rigid_body.c"
         "tiny3d_render.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_hw_support esp_timer esp_driver_usb_serial_jtag
)
