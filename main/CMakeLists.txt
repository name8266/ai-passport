if(NOT CMAKE_BUILD_EARLY_EXPANSION)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/pusher_background.S.in"
    "${CMAKE_CURRENT_BINARY_DIR}/pusher_background.S" @ONLY)
set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/pusher_background.S"
    PROPERTIES OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/pusher_background.bin")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/pusher_coin_impostors.S.in"
    "${CMAKE_CURRENT_BINARY_DIR}/pusher_coin_impostors.S" @ONLY)
set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/pusher_coin_impostors.S"
    PROPERTIES OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/pusher_coin_impostors.bin")
endif()
idf_component_register(
    SRCS "main.c"
         "coin_pusher_app.c"
         "pusher_physics.c"
         "pusher_rogue.c"
         "pusher_tactics.c"
         "pusher_records.c"
         "pusher_store.c"
         "coin_assets.c"
         "coin_save.c"
         "pusher_render.c"
         "pusher_photo.c"
         "pusher_fx.c"
         "${CMAKE_CURRENT_BINARY_DIR}/pusher_background.S"
         "${CMAKE_CURRENT_BINARY_DIR}/pusher_coin_impostors.S"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_hw_support nvs_flash esp_timer esp_lcd esp_driver_usb_serial_jtag
)

# These bounded integer kernels execute from IRAM on the C3. Optimizing the small vectors avoids
# repeated loop overhead without changing physics precision.
set_source_files_properties(pusher_physics.c pusher_render.c
    PROPERTIES COMPILE_OPTIONS "-O3;-fno-unroll-loops;-fno-peel-loops;-fno-unswitch-loops")
