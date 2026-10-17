#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
work="$(mktemp -d /tmp/pusher-poses.XXXXXX)"
trap 'rm -rf "$work"' EXIT
"${CC:-cc}" -O2 -std=c11 -DCP_RENDER_HOST -DCP_RENDER_BAKE_COIN -Imain \
    tools/bake_pusher_impostors.c main/pusher_render.c main/pusher_physics.c main/pusher_rogue.c main/pusher_tactics.c main/pusher_records.c main/pusher_photo.c -lm -o "$work/baker"
"$work/baker" "$work/poses.bin"
python3 tools/pack_pusher_impostors.py "$work/poses.bin"
