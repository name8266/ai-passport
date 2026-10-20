#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/brake-hero
cc -std=c11 -O2 -Wall -Wextra -Werror -Imain tests/test_brake_hero.c main/brake_hero.c main/brake_scores.c -lm -o build/brake-hero/test
build/brake-hero/test
build/brake-hero/test --save build/brake-hero/host-score.bin
build/brake-hero/test --restore build/brake-hero/host-score.bin
cc -std=c11 -O2 -Wall -Wextra -Werror -Imain tests/test_brake_app.c main/brake_app.c main/brake_hero.c main/brake_scores.c -lm -o build/brake-hero/test-app
build/brake-hero/test-app
cc -std=c11 -O2 -Wall -Wextra -Werror -Wno-unused-variable -Itests/brake_audio_stubs -Imain tests/test_brake_sound.c -o build/brake-hero/test-sound
build/brake-hero/test-sound
cc -std=c11 -O2 -Wall -Wextra -Werror -Imain tests/test_brake_vehicle.c main/brake_vehicle.c main/brake_display_math.c main/brake_render.c main/brake_hero.c main/brake_scores.c assets/images/brake-hero/converted/brake_images.c assets/fonts/brake-hero/brake_font.c -lm -o build/brake-hero/test-vehicle
build/brake-hero/test-vehicle
cc -std=c11 -O2 -Wall -Wextra -Werror -Imain tests/preview_brake_hero.c main/brake_hero.c main/brake_scores.c main/brake_render.c main/brake_vehicle.c assets/images/brake-hero/converted/brake_images.c assets/fonts/brake-hero/brake_font.c -lm -o build/brake-hero/preview
build/brake-hero/preview
cc -std=c11 -O2 -Wall -Wextra -Werror -Imain tests/sim_brake_hero.c main/brake_hero.c -lm -o build/brake-hero/sim
build/brake-hero/sim > build/brake-hero/trajectories.csv
python3 tools/summarize_brake_sim.py
