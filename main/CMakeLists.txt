idf_component_register(
    SRCS "arcade_main.c" "arcade_game.c" "arcade_art.c"
         "zuma_audio.c" "zuma_synth.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_hw_support esp_timer heap
    EMBED_FILES "art/eight-realms/breakout.rgb565"
                "art/eight-realms/tetris.rgb565"
                "art/eight-realms/doodle.rgb565"
                "art/eight-realms/snake.rgb565"
                "art/eight-realms/shooter.rgb565"
                "art/eight-realms/flappy.rgb565"
                "art/eight-realms/lander.rgb565"
                "art/eight-realms/racer.rgb565"
                "art/eight-realms/breakout-sprite.rgb565a8"
                "art/eight-realms/tetris-sprite.rgb565a8"
                "art/eight-realms/doodle-sprite.rgb565a8"
                "art/eight-realms/snake-sprite.rgb565a8"
                "art/eight-realms/shooter-sprite.rgb565a8"
                "art/eight-realms/flappy-sprite.rgb565a8"
                "art/eight-realms/lander-sprite.rgb565a8"
                "art/eight-realms/racer-sprite.rgb565a8"
)

target_sources(${COMPONENT_LIB} PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/arcade_cjk_14.c"
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/arcade_cjk_20.c"
)
