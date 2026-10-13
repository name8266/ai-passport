#include "arcade_game.h"
#include "arcade_docking.h"
#include <math.h>
#include <string.h>

static const char *NAMES[] = {
    "琉璃破阵", "星石方阵", "云端跃迁", "苍翠灵蛇",
    "银河守卫", "霞谷飞翼", "月面归航", "沙海疾驰", "太空对接"
};
static const char *HINTS[] = {
    "上左 下右 确定发球", "上左 下右 确定转 双击落",
    "上左 下右 确定冲刺", "左右转 确定加速/降速",
    "上左下右 连发 确定加速", "确定拍翼 上左 下右",
    "上左转 下右转 确定推进", "上左道 下右道 确定加速/降速",
    "上/下推进 确定换轴"
};
static const char *GOALS[] = {
    "击碎12块砖", "消除6行", "攀升600分", "吃8颗宝石",
    "击败15个敌人", "穿越10道关口", "安全降落", "存活25秒", "安全对接"
};

bool arcade_long_exit_uses_up(arcade_kind_t kind) {
    return kind == ARCADE_LANDER;
}
const char *arcade_name(arcade_kind_t kind) {
    return kind < ARCADE_GAME_COUNT ? NAMES[kind] : "未知游戏";
}
const char *arcade_hint(arcade_kind_t kind) {
    return kind < ARCADE_GAME_COUNT ? HINTS[kind] : "";
}
const char *arcade_goal(arcade_kind_t kind) {
    return kind < ARCADE_GAME_COUNT ? GOALS[kind] : "";
}
static uint32_t rnd(arcade_game_t *g) {
    uint32_t x = g->rng ? g->rng : 1;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return g->rng = x;
}
static float clampf(float x, float low, float high) {
    return x < low ? low : x > high ? high : x;
}
static bool near(float a, float b, float radius) { return fabsf(a - b) < radius; }

/* One canonical 4x4 rotation rule is shared by the host test and renderer. */
static const int8_t SHAPES[7][4][2] = {
    {{0,1},{1,1},{2,1},{3,1}}, {{1,0},{2,0},{1,1},{2,1}},
    {{1,0},{0,1},{1,1},{2,1}}, {{0,0},{1,0},{1,1},{2,1}},
    {{1,0},{2,0},{0,1},{1,1}}, {{0,0},{0,1},{1,1},{2,1}},
    {{2,0},{0,1},{1,1},{2,1}}
};
bool arcade_tetris_cell(int piece, int rotation, int x, int y) {
    if (piece < 0 || piece >= 7 || x < 0 || x >= 4 || y < 0 || y >= 4) return false;
    for (int i = 0; i < 4; i++) {
        int px = SHAPES[piece][i][0], py = SHAPES[piece][i][1];
        for (int n = 0; n < (rotation & 3); n++) {
            int next = 3 - py; py = px; px = next;
        }
        if (px == x && py == y) return true;
    }
    return false;
}
static bool tet_fits(const arcade_game_t *g, int x, int y, int rot) {
    for (int py = 0; py < 4; py++) for (int px = 0; px < 4; px++) {
        if (!arcade_tetris_cell(g->piece, rot, px, py)) continue;
        int gx = x + px, gy = y + py;
        if (gx < 0 || gx >= 10 || gy >= 18 || (gy >= 0 && g->grid[gy][gx])) return false;
    }
    return true;
}
static void tet_spawn(arcade_game_t *g) {
    g->piece = (int)(rnd(g) % 7);
    g->rotation = 0; g->piece_x = 3; g->piece_y = -2;
    if (!tet_fits(g, g->piece_x, g->piece_y, 0)) g->mode = ARCADE_LOST;
}
static void tet_lock(arcade_game_t *g) {
    for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) {
        if (!arcade_tetris_cell(g->piece, g->rotation, x, y)) continue;
        int gy = g->piece_y + y;
        if (gy < 0) { g->mode = ARCADE_LOST; return; }
        g->grid[gy][g->piece_x + x] = (uint8_t)(g->piece + 1);
    }
    for (int y = 17; y >= 0; y--) {
        bool full = true;
        for (int x = 0; x < 10; x++) if (!g->grid[y][x]) full = false;
        if (!full) continue;
        for (int row = y; row > 0; row--) memcpy(g->grid[row], g->grid[row - 1], 10);
        memset(g->grid[0], 0, 10);
        g->lines++; g->score += 100; y++;
    }
    if (g->lines >= 6) g->mode = ARCADE_WON;
    else tet_spawn(g);
}
static void snake_food(arcade_game_t *g) {
    for (int attempt = 0; attempt < 100; attempt++) {
        int x = (int)(rnd(g) % 12), y = (int)(rnd(g) % 14);
        bool occupied = false;
        for (int i = 0; i < g->snake_length; i++)
            if (g->snake_x[i] == x && g->snake_y[i] == y) occupied = true;
        if (!occupied) { g->food_x = x; g->food_y = y; return; }
    }
    g->mode = ARCADE_WON;
}
void arcade_init(arcade_game_t *g, uint32_t seed) {
    memset(g, 0, sizeof(*g));
    g->rng = seed ? seed : 1;
    g->mode = ARCADE_HOME;
}
void arcade_start(arcade_game_t *g, arcade_kind_t kind) {
    if (kind >= ARCADE_GAME_COUNT) return;
    uint32_t seed = g->rng ? g->rng : 1;
    memset(g, 0, sizeof(*g));
    g->rng = seed; g->kind = kind; g->mode = ARCADE_PLAYING; g->lives = 3;
    g->x = 120; g->y = 160;
    switch (kind) {
    case ARCADE_BREAKOUT:
        g->x = 120; g->y = 251; g->angle = 120; g->phase = 0;
        for (int r = 0; r < 5; r++) for (int c = 0; c < 6; c++) g->bricks[r][c] = 1;
        break;
    case ARCADE_TETRIS: tet_spawn(g); break;
    case ARCADE_DOODLE:
        g->x = 120; g->y = 198; g->vy = -210; g->lane = 2;
        for (int i = 0; i < 7; i++) {
            float previous = i ? g->actors[i - 1].x : 120;
            float x = i == 0 ? 120 : clampf(previous + (int)(rnd(g) % 101) - 50, 30, 210);
            g->actors[i] = (arcade_actor_t){.x = x,
                .y = 258 - i * 42, .active = true};
        }
        break;
    case ARCADE_SNAKE:
        g->direction = 0; g->snake_length = 3;
        for (int i = 0; i < 3; i++) { g->snake_x[i] = 6; g->snake_y[i] = 8 + i; }
        snake_food(g); break;
    case ARCADE_SHOOTER:
        g->x = 120; g->y = 265; g->lives = 4;
        for (int i = 0; i < 6; i++) g->actors[i] = (arcade_actor_t){
            .x = 28 + rnd(g) % 184, .y = -i * 52, .active = true};
        break;
    case ARCADE_FLAPPY:
        g->x = 70; g->y = 140;
        for (int i = 0; i < 3; i++) g->actors[i] = (arcade_actor_t){
            .x = 250 + i * 112, .y = 105 + rnd(g) % 100, .active = true};
        break;
    case ARCADE_LANDER:
        g->x = (rnd(g) & 1) ? 100 : 140;
        g->y = 65; g->vx = g->x < 120 ? 7 : -7;
        g->fuel = 100; g->lives = 1; break;
    case ARCADE_RACER:
        g->lane = 1; g->x = 120; g->y = 257; g->lives = 3; break;
    case ARCADE_DOCKING:
        arcade_docking_start(g); break;
    default: break;
    }
}
void arcade_input(arcade_game_t *g, arcade_action_t action) {
    if (action == ARCADE_HOME_REQUEST) { g->mode = ARCADE_HOME; g->held = 0; g->ok_held = false; return; }
    if (g->mode != ARCADE_PLAYING) {
        if (action == ARCADE_OK_CLICK) arcade_start(g, g->kind);
        return;
    }
    if (action == ARCADE_LEFT_UP && g->held == -1) g->held = 0;
    if (action == ARCADE_RIGHT_UP && g->held == 1) g->held = 0;
    if (g->kind == ARCADE_DOCKING) { arcade_docking_input(g, action); return; }
    if (action == ARCADE_OK_UP && g->kind == ARCADE_LANDER) g->ok_held = false;
    int dir = action == ARCADE_LEFT_DOWN ? -1 : action == ARCADE_RIGHT_DOWN ? 1 : 0;
    if (dir) {
        g->held = dir;
        g->move_clock = 0;
        /* Lander turns in discrete steps, leaving a held UP key for exit. */
        if (g->kind == ARCADE_LANDER)
            g->angle = clampf(g->angle + dir * 0.18f, -1.0f, 1.0f);
    }
    if (g->kind == ARCADE_TETRIS) {
        if (dir && tet_fits(g, g->piece_x + dir, g->piece_y, g->rotation)) g->piece_x += dir;
        if (action == ARCADE_OK_CLICK && tet_fits(g, g->piece_x, g->piece_y, g->rotation + 1))
            g->rotation = (g->rotation + 1) & 3;
        if (action == ARCADE_OK_DOUBLE) {
            while (tet_fits(g, g->piece_x, g->piece_y + 1, g->rotation)) g->piece_y++;
            tet_lock(g);
        }
    } else if (g->kind == ARCADE_SNAKE) {
        if (dir) g->direction = (g->direction + (dir < 0 ? 3 : 1)) & 3;
        if (action == ARCADE_OK_DOWN) g->ok_held = !g->ok_held;
    } else if (g->kind == ARCADE_RACER) {
        if (dir) { g->lane += dir; if (g->lane < 0) g->lane = 0; if (g->lane > 2) g->lane = 2; }
        if (action == ARCADE_OK_DOWN) g->ok_held = !g->ok_held;
    } else if (g->kind == ARCADE_BREAKOUT) {
        if (action == ARCADE_OK_DOWN && g->phase == 0) {
            g->phase = 1; g->vx = 89; g->vy = -148;
        }
    } else if (g->kind == ARCADE_FLAPPY) {
        if (action == ARCADE_OK_DOWN) g->vy = -165;
    } else if (g->kind == ARCADE_LANDER) {
        if (action == ARCADE_OK_DOWN) g->ok_held = true;
    } else if (g->kind == ARCADE_SHOOTER) {
        if (action == ARCADE_OK_DOWN) g->ok_held = !g->ok_held;
    } else if (g->kind == ARCADE_DOODLE && action == ARCADE_OK_DOWN && g->phase == 0) {
        g->vy = -245; g->phase = 1;
    }
}
static void tick_breakout(arcade_game_t *g, float dt) {
    g->angle = clampf(g->angle + g->held * 180 * dt, 30, 210); /* paddle center */
    if (g->phase == 0) { g->x = g->angle; g->y = 250; return; }
    float old_x = g->x, old_y = g->y;
    g->x += g->vx * dt; g->y += g->vy * dt;
    if (g->x < 12 || g->x > 228) { g->x = clampf(g->x, 12, 228); g->vx = -g->vx; }
    if (g->y < 39) { g->y = 39; g->vy = fabsf(g->vy); }
    if (g->vy > 0 && old_y < 247 && g->y >= 247 && near(g->x, g->angle, 30)) {
        g->y = 247; g->vy = -fabsf(g->vy); g->vx = (g->x - g->angle) * 5.0f;
        if (fabsf(g->vx) < 45) g->vx = (rnd(g) & 1) ? 55 : -55;
    }
    for (int r = 0; r < 5; r++) for (int c = 0; c < 6; c++) {
        if (!g->bricks[r][c]) continue;
        float bx = 28 + c * 36, by = 65 + r * 18;
        if (near(g->x, bx, 19) && near(g->y, by, 10)) {
            g->bricks[r][c] = 0; g->score += 10; g->vy = -g->vy;
            if (fabsf(old_x - bx) > 18) g->vx = -g->vx;
            if (g->score >= 120) g->mode = ARCADE_WON;
            return;
        }
    }
    if (g->y > 283) { g->lives--; g->phase = 0; if (g->lives <= 0) g->mode = ARCADE_LOST; }
}
static void tick_tetris(arcade_game_t *g, float dt) {
    if (g->held) {
        g->move_clock += dt;
        if (g->move_clock >= 0.14f) {
            g->move_clock -= 0.14f;
            if (tet_fits(g, g->piece_x + g->held, g->piece_y, g->rotation))
                g->piece_x += g->held;
        }
    }
    g->accumulator += dt;
    float fall = clampf(0.62f - g->lines * 0.025f, 0.22f, 0.62f);
    if (g->accumulator < fall) return;
    g->accumulator -= fall;
    if (tet_fits(g, g->piece_x, g->piece_y + 1, g->rotation)) g->piece_y++;
    else tet_lock(g);
}
static void tick_doodle(arcade_game_t *g, float dt) {
    float old_bottom = g->y + 8;
    g->x += g->held * 125 * dt;
    if (g->x < -8) g->x = 248;
    if (g->x > 248) g->x = -8;
    g->vy += 330 * dt; g->y += g->vy * dt;
    if (g->vy > 0) for (int i = 0; i < 7; i++) {
        arcade_actor_t *p = &g->actors[i];
        if (old_bottom <= p->y && g->y + 8 >= p->y && near(g->x, p->x, 32)) {
            g->y = p->y - 8; g->vy = -215; g->phase = 0;
            float best_y = -1000;
            for (int j = 0; j < 7; j++) if (j != i && g->actors[j].y < p->y &&
                g->actors[j].y > p->y - 72 && g->actors[j].y > best_y) {
                best_y = g->actors[j].y; g->lane = j;
            }
            break;
        }
    }
    if (g->y < 115) {
        float shift = 115 - g->y; g->y = 115;
        g->score += (uint32_t)shift;
        for (int i = 0; i < 7; i++) {
            arcade_actor_t *p = &g->actors[i]; p->y += shift;
            if (p->y > 310) {
                float top_y = 999, top_x = 120;
                for (int j = 0; j < 7; j++) if (j != i && g->actors[j].y < top_y) {
                    top_y = g->actors[j].y; top_x = g->actors[j].x;
                }
                p->y -= 294;
                p->x = clampf(top_x + (int)(rnd(g) % 101) - 50, 30, 210);
            }
        }
    }
    if (g->score >= 600) g->mode = ARCADE_WON;
    if (g->y > 310) g->mode = ARCADE_LOST;
}
static void tick_snake(arcade_game_t *g, float dt) {
    g->accumulator += dt * (g->ok_held ? 1.8f : 1.0f);
    if (g->accumulator < 0.15f) return;
    g->accumulator -= 0.15f;
    static const int dx[] = {0,1,0,-1}, dy[] = {-1,0,1,0};
    int x = g->snake_x[0] + dx[g->direction], y = g->snake_y[0] + dy[g->direction];
    if (x < 0 || x >= 12 || y < 0 || y >= 14) { g->mode = ARCADE_LOST; return; }
    bool eat = x == g->food_x && y == g->food_y;
    for (int i = 0; i < g->snake_length - (eat ? 0 : 1); i++)
        if (g->snake_x[i] == x && g->snake_y[i] == y) { g->mode = ARCADE_LOST; return; }
    if (eat && g->snake_length < 128) { g->snake_length++; g->score += 10; }
    for (int i = g->snake_length - 1; i > 0; i--) {
        g->snake_x[i] = g->snake_x[i - 1]; g->snake_y[i] = g->snake_y[i - 1];
    }
    g->snake_x[0] = x; g->snake_y[0] = y;
    if (eat) snake_food(g);
    if (g->score >= 80) g->mode = ARCADE_WON;
}
static void shoot(arcade_game_t *g) {
    for (int i = 0; i < 4; i++) if (!g->shots[i].active) {
        g->shots[i] = (arcade_actor_t){.x = g->x, .y = 251, .active = true}; return;
    }
}
static void tick_shooter(arcade_game_t *g, float dt) {
    g->x = clampf(g->x + g->held * 130 * dt, 18, 222);
    g->accumulator += dt;
    /* Auto-fire avoids unreliable simultaneous button presses on the ADC ladder. */
    float fire_interval = g->ok_held ? 0.12f : 0.27f;
    if (g->accumulator >= fire_interval) {
        g->accumulator -= fire_interval;
        shoot(g);
    }
    for (int i = 0; i < 4; i++) if (g->shots[i].active) {
        g->shots[i].y -= 280 * dt;
        if (g->shots[i].y < 25) g->shots[i].active = false;
    }
    for (int i = 0; i < 6; i++) {
        arcade_actor_t *e = &g->actors[i];
        e->y += (38 + g->elapsed * 0.4f) * dt;
        if (e->y > 294) {
            g->lives--; e->y = -(float)(rnd(g) % 120); e->x = 25 + rnd(g) % 190;
            if (g->lives <= 0) { g->mode = ARCADE_LOST; return; }
        }
        for (int j = 0; j < 4; j++) if (g->shots[j].active &&
            near(e->x, g->shots[j].x, 13) && near(e->y, g->shots[j].y, 12)) {
            g->shots[j].active = false; e->y = -(float)(rnd(g) % 120);
            e->x = 25 + rnd(g) % 190; g->score += 10;
        }
        if (near(e->x, g->x, 14) && near(e->y, g->y, 14)) {
            g->lives--; e->y = -80;
            if (g->lives <= 0) { g->mode = ARCADE_LOST; return; }
        }
    }
    if (g->score >= 150) g->mode = ARCADE_WON;
}
static void tick_flappy(arcade_game_t *g, float dt) {
    g->x = clampf(g->x + g->held * 55 * dt, 35, 110);
    g->vy += 385 * dt; g->y += g->vy * dt;
    if (g->y < 30 || g->y > 283) { g->mode = ARCADE_LOST; return; }
    for (int i = 0; i < 3; i++) {
        arcade_actor_t *p = &g->actors[i]; p->x -= 67 * dt;
        if (!p->kind && p->x + 28 < g->x) { p->kind = 1; g->score++; }
        if (p->x < -35) {
            float max_x = 240;
            for (int j = 0; j < 3; j++) if (g->actors[j].x > max_x) max_x = g->actors[j].x;
            p->x = max_x + 112; p->y = 105 + rnd(g) % 100; p->kind = 0;
        }
        if (g->x + 7 > p->x && g->x - 7 < p->x + 28 && fabsf(g->y - p->y) > 37)
            { g->mode = ARCADE_LOST; return; }
    }
    if (g->score >= 10) g->mode = ARCADE_WON;
}
static void tick_lander(arcade_game_t *g, float dt) {
    if (g->ok_held && g->fuel > 0) {
        g->vx += sinf(g->angle) * 60 * dt;
        g->vy -= cosf(g->angle) * 60 * dt;
        g->fuel = fmaxf(0, g->fuel - 17 * dt);
    }
    g->vy += 31 * dt; g->x += g->vx * dt; g->y += g->vy * dt;
    if (g->x < 8 || g->x > 232 || g->y < 20) { g->mode = ARCADE_LOST; return; }
    if (g->y >= 258) {
        if (g->x > 77 && g->x < 163 && fabsf(g->vx) < 22 && g->vy < 38 && fabsf(g->angle) < 0.3f)
            { g->mode = ARCADE_WON; g->score = (uint32_t)(100 + g->fuel); }
        else g->mode = ARCADE_LOST;
    }
}
static void tick_racer(arcade_game_t *g, float dt) {
    g->x = 70 + g->lane * 50;
    g->accumulator += dt;
    if (g->accumulator >= (g->ok_held ? 0.72f : 0.95f)) {
        g->accumulator = 0;
        for (int i = 0; i < 6; i++) if (!g->actors[i].active) {
            g->actors[i] = (arcade_actor_t){.x = 70 + (rnd(g) % 3) * 50,
                .y = 27, .active = true}; break;
        }
    }
    for (int i = 0; i < 6; i++) if (g->actors[i].active) {
        arcade_actor_t *a = &g->actors[i]; a->y += (108 + (g->ok_held ? 42 : 0)) * dt;
        if (a->y > 298) { a->active = false; g->score += 5; }
        else if (near(a->x, g->x, 19) && near(a->y, g->y, 20)) {
            a->active = false; g->lives--;
            if (g->lives <= 0) { g->mode = ARCADE_LOST; return; }
        }
    }
    /* Preserve fractional frame time instead of truncating every 33ms step. */
    int time_points = (int)(g->elapsed * 12.0f);
    if (time_points > g->phase) {
        g->score += (uint32_t)(time_points - g->phase);
        g->phase = time_points;
    }
    if (g->elapsed >= 25) g->mode = ARCADE_WON;
}
void arcade_tick(arcade_game_t *g, float dt, bool demo) {
    if (g->mode != ARCADE_PLAYING) return;
    dt = clampf(dt, 0, 0.05f);
    g->elapsed += dt;
    if (demo) {
        switch (g->kind) {
        case ARCADE_BREAKOUT:
            if (!g->phase) arcade_input(g, ARCADE_OK_DOWN);
            g->angle = clampf(g->angle + clampf(g->x - g->angle, -180 * dt, 180 * dt), 30, 210);
            break;
        case ARCADE_TETRIS:
            if ((int)(g->elapsed * 3) != (int)((g->elapsed - dt) * 3))
                arcade_input(g, (rnd(g) & 1) ? ARCADE_LEFT_DOWN : ARCADE_RIGHT_DOWN);
            break;
        case ARCADE_DOODLE:
            g->held = g->x < g->actors[g->lane].x - 5 ? 1 :
                      g->x > g->actors[g->lane].x + 5 ? -1 : 0;
            break;
        case ARCADE_SNAKE:
            if ((g->snake_x[0] < 2 && g->direction == 3) ||
                (g->snake_y[0] < 2 && g->direction == 0) ||
                (g->snake_x[0] > 9 && g->direction == 1) ||
                (g->snake_y[0] > 11 && g->direction == 2))
                arcade_input(g, ARCADE_RIGHT_DOWN);
            break;
        case ARCADE_SHOOTER:
            g->held = g->x < g->actors[0].x ? 1 : -1; g->ok_held = true; break;
        case ARCADE_FLAPPY:
            {
                float target_x = 999, target_y = 150;
                for (int i = 0; i < 3; i++) {
                    arcade_actor_t *p = &g->actors[i];
                    if (p->x + 28 > g->x - 7 && p->x < target_x)
                        { target_x = p->x; target_y = p->y; }
                }
                if (g->y > target_y + 5 && g->vy > -30)
                    arcade_input(g, ARCADE_OK_DOWN);
            }
            break;
        case ARCADE_LANDER:
            g->ok_held = g->vy > 24; break;
        case ARCADE_DOCKING: arcade_docking_demo(g, dt); break;
        case ARCADE_RACER:
            {
                int risk[3] = {0};
                for (int i = 0; i < 6; i++) if (g->actors[i].active &&
                    g->actors[i].y > 150 && g->actors[i].y < 280) {
                    int lane = (int)((g->actors[i].x - 70) / 50);
                    if (lane >= 0 && lane < 3) risk[lane]++;
                }
                if (risk[g->lane]) {
                    for (int lane = 0; lane < 3; lane++) if (!risk[lane]) {
                        g->lane = lane; break;
                    }
                }
            }
            break;
        default: break;
        }
    }
    switch (g->kind) {
    case ARCADE_BREAKOUT: tick_breakout(g, dt); break;
    case ARCADE_TETRIS: tick_tetris(g, dt); break;
    case ARCADE_DOODLE: tick_doodle(g, dt); break;
    case ARCADE_SNAKE: tick_snake(g, dt); break;
    case ARCADE_SHOOTER: tick_shooter(g, dt); break;
    case ARCADE_FLAPPY: tick_flappy(g, dt); break;
    case ARCADE_LANDER: tick_lander(g, dt); break;
    case ARCADE_RACER: tick_racer(g, dt); break;
    case ARCADE_DOCKING: arcade_docking_tick(g, dt); break;
    default: break;
    }
}
