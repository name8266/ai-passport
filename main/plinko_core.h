#pragma once
#include <stdbool.h>
#include <stdint.h>
#define PLINKO_CORE_ROWS 8
#define PLINKO_CORE_BINS 9
#define PLINKO_MAX_BALLS 10
typedef struct {
    int32_t x, y, vx, vy;
    uint32_t rng;
    uint16_t ticks, collisions;
    uint8_t speed, bin;
    bool done;
} plinko_ball_t;
int plinko_peg_count(int row);
void plinko_peg_position(int row, int col, int *x, int *y);
void plinko_ball_init(plinko_ball_t *ball, uint32_t seed, uint8_t speed);
uint16_t plinko_ball_step(plinko_ball_t *ball);

uint8_t plinko_balls_init(plinko_ball_t *balls, uint8_t count, uint32_t seed, uint8_t speed);
