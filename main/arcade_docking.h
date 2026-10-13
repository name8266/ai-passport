#pragma once
#include "arcade_game.h"

void arcade_docking_start(arcade_game_t *game);
void arcade_docking_input(arcade_game_t *game, arcade_action_t action);
void arcade_docking_tick(arcade_game_t *game, float dt);
void arcade_docking_demo(arcade_game_t *game, float dt);
const char *arcade_docking_axis_name(int axis);
