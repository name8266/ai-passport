#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
baker="$(mktemp /tmp/pusher-background.XXXXXX)"
trap 'rm -f "$baker"' EXIT
"${CC:-cc}" -O2 -std=c11 -DCP_RENDER_HOST -DCP_RENDER_BAKE -Imain tools/bake_pusher_background.c main/pusher_render.c main/pusher_physics.c main/pusher_rogue.c main/pusher_tactics.c main/pusher_records.c main/pusher_photo.c -lm -o "$baker"
"$baker" main/pusher_background.bin
