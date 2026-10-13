#include "zuma_game.h"

#include <math.h>
#include <string.h>

#define BALL_STEP 13.0f
#define SHOT_SPEED 190.0f

typedef struct { float x, y; } point_t;

/* A hand-drawn double loop. The path and character are original to this game. */
static const point_t route[] = {
    {6, 53}, {45, 51}, {110, 51}, {174, 54}, {215, 78},
    {220, 120}, {212, 170}, {190, 205}, {135, 216}, {75, 211},
    {29, 185}, {20, 143}, {45, 104}, {94, 89}, {151, 96},
    {183, 122}, {187, 155}, {157, 178}, {111, 180}, {75, 159},
    {77, 130}, {105, 117}, {137, 126}, {148, 147}
};

static float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static uint32_t random_next(zuma_game_t *g) {
    uint32_t x = g->seed;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g->seed = x ? x : 0x9e3779b9u;
    return g->seed;
}

static uint8_t random_color(zuma_game_t *g) {
    return (uint8_t)(random_next(g) % 4u);
}

static uint8_t incoming_color(zuma_game_t *g) {
    uint8_t c = random_color(g);
    if (g->count >= 2 && c == g->balls[g->count - 1] &&
        c == g->balls[g->count - 2]) c = (uint8_t)((c + 1) % 4);
    return c;
}

static void add_path_point(zuma_game_t *g, float x, float y) {
    if (g->path_count >= ZUMA_PATH_POINTS) return;
    zuma_path_point_t *p = &g->path[g->path_count];
    float d = 0;
    if (g->path_count) {
        const zuma_path_point_t *prev = &g->path[g->path_count - 1];
        d = prev->distance + hypotf(x - prev->x, y - prev->y);
    }
    *p = (zuma_path_point_t){x, y, d};
    g->path_count++;
    g->path_length = d;
}

static void build_path(zuma_game_t *g) {
    g->path_count = 0;
    const int n = (int)(sizeof(route) / sizeof(route[0]));
    for (int i = 0; i < n - 1; i++) {
        const point_t p0 = route[i > 0 ? i - 1 : i];
        const point_t p1 = route[i];
        const point_t p2 = route[i + 1];
        const point_t p3 = route[i + 2 < n ? i + 2 : i + 1];
        for (int j = 0; j < 12; j++) {
            float t = j / 12.0f, t2 = t * t, t3 = t2 * t;
            float x = 0.5f * ((2 * p1.x) + (-p0.x + p2.x) * t +
                       (2*p0.x - 5*p1.x + 4*p2.x - p3.x) * t2 +
                       (-p0.x + 3*p1.x - 3*p2.x + p3.x) * t3);
            float y = 0.5f * ((2 * p1.y) + (-p0.y + p2.y) * t +
                       (2*p0.y - 5*p1.y + 4*p2.y - p3.y) * t2 +
                       (-p0.y + 3*p1.y - 3*p2.y + p3.y) * t3);
            add_path_point(g, x, y);
        }
    }
    add_path_point(g, route[n - 1].x, route[n - 1].y);
}

void zuma_path_position(const zuma_game_t *g, float d, float *x, float *y) {
    if (g->path_count == 0) { *x = 0; *y = 0; return; }
    d = clampf(d, 0, g->path_length);
    int lo = 0, hi = g->path_count - 1;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (g->path[mid].distance < d) lo = mid + 1;
        else hi = mid;
    }
    if (lo == 0) { *x = g->path[0].x; *y = g->path[0].y; return; }
    const zuma_path_point_t *a = &g->path[lo - 1], *b = &g->path[lo];
    float span = b->distance - a->distance;
    float t = span > 0 ? (d - a->distance) / span : 0;
    *x = a->x + (b->x - a->x) * t;
    *y = a->y + (b->y - a->y) * t;
}

void zuma_init(zuma_game_t *g, uint32_t seed) {
    memset(g, 0, sizeof(*g));
    g->seed = seed ? seed : 0x3423bcadu;
    g->mode = ZUMA_MENU;
    g->aim_radians = -1.570796327f;
    build_path(g);
}

void zuma_start(zuma_game_t *g) {
    g->mode = ZUMA_PLAYING;
    g->count = 0;
    g->spawned = 10;
    g->quota = (uint8_t)(25 + g->difficulty * 8);
    g->lead = BALL_STEP * 10;
    g->spawn_clock = 0;
    g->rollback = 0;
    g->score = 0;
    g->combo = 0;
    g->shot.active = false;
    g->aim_radians = -1.570796327f;
    for (int i = 0; i < 10; i++) {
        g->balls[i] = incoming_color(g);
        g->count++;
        g->ball_distance[i] = g->lead - i * BALL_STEP;
    }
    g->current_color = random_color(g);
    g->next_color = random_color(g);
    g->sound = ZUMA_SOUND_NONE;
}

void zuma_turn(zuma_game_t *g, int direction) {
    if (g->mode == ZUMA_MENU) {
        g->difficulty = (uint8_t)((g->difficulty + (direction > 0 ? 1 : 2)) % 3);
        return;
    }
    if (g->mode != ZUMA_PLAYING) return;
    g->aim_radians = clampf(g->aim_radians + (direction > 0 ? 0.115f : -0.115f),
                            -2.85f, -0.29f);
}

void zuma_fire(zuma_game_t *g) {
    if (g->mode == ZUMA_MENU || g->mode == ZUMA_WON || g->mode == ZUMA_LOST) {
        zuma_start(g);
        return;
    }
    if (g->mode != ZUMA_PLAYING || g->shot.active) return;
    g->shot = (zuma_shot_t){
        .x = 120 + cosf(g->aim_radians) * 17,
        .y = 264 + sinf(g->aim_radians) * 17,
        .vx = cosf(g->aim_radians) * SHOT_SPEED,
        .vy = sinf(g->aim_radians) * SHOT_SPEED,
        .color = g->current_color, .active = true,
    };
    g->current_color = g->next_color;
    g->next_color = random_color(g);
    g->sound = ZUMA_SOUND_SHOOT;
}

void zuma_swap(zuma_game_t *g) {
    if (g->mode != ZUMA_PLAYING) return;
    uint8_t c = g->current_color;
    g->current_color = g->next_color;
    g->next_color = c;
    g->sound = ZUMA_SOUND_SWAP;
}

void zuma_toggle_pause(zuma_game_t *g) {
    if (g->mode == ZUMA_PLAYING) g->mode = ZUMA_PAUSED;
    else if (g->mode == ZUMA_PAUSED) g->mode = ZUMA_PLAYING;
}

void zuma_to_menu(zuma_game_t *g) {
    g->mode = ZUMA_MENU;
    g->shot.active = false;
    g->sound = ZUMA_SOUND_NONE;
}

static bool touching(const zuma_game_t *g, int front, int rear) {
    return g->ball_distance[front] - g->ball_distance[rear] <= BALL_STEP + 0.6f;
}

static int resolve_matches(zuma_game_t *g, int index) {
    int removed_total = 0;
    while (g->count > 0 && index >= 0 && index < g->count) {
        uint8_t color = g->balls[index];
        int first = index, end = index + 1;
        while (first > 0 && g->balls[first - 1] == color &&
               touching(g, first - 1, first)) first--;
        while (end < g->count && g->balls[end] == color &&
               touching(g, end - 1, end)) end++;
        if (end - first < 3) break;
        int removed = end - first;
        memmove(&g->balls[first], &g->balls[end], g->count - end);
        memmove(&g->ball_distance[first], &g->ball_distance[end],
                (g->count - end) * sizeof(g->ball_distance[0]));
        g->count -= (uint8_t)removed;
        removed_total += removed;
        g->combo++;
        g->score += (uint32_t)(removed * 10 * g->combo);
        g->rollback = fminf(32.0f, g->rollback + removed * 3.0f);
        index = first > 0 ? first - 1 : 0;
    }
    if (removed_total) g->sound = g->combo > 1 ? ZUMA_SOUND_COMBO : ZUMA_SOUND_MATCH;
    if (g->count == 0) g->rollback = 0;
    return removed_total;
}

static void insert_hit(zuma_game_t *g, int target) {
    if (g->count >= ZUMA_MAX_BALLS) { g->shot.active = false; return; }
    float tx, ty, ax, ay;
    float d = g->ball_distance[target];
    zuma_path_position(g, d, &tx, &ty);
    zuma_path_position(g, d + 4, &ax, &ay);
    bool before = (g->shot.x - tx) * (ax - tx) +
                  (g->shot.y - ty) * (ay - ty) > 0;
    int index = target + (before ? 0 : 1);
    float insert_distance = before ? d : d - BALL_STEP;
    for (int j = g->count; j > index; j--) {
        g->balls[j] = g->balls[j - 1];
        g->ball_distance[j] = g->ball_distance[j - 1] - BALL_STEP;
    }
    g->balls[index] = g->shot.color;
    g->ball_distance[index] = insert_distance;
    g->count++;
    g->shot.active = false;
    g->sound = ZUMA_SOUND_HIT;
    if (!resolve_matches(g, index)) g->combo = 0;
    g->lead = g->count ? g->ball_distance[0] : 0;
}

void zuma_tick(zuma_game_t *g, float dt) {
    if (g->mode != ZUMA_PLAYING) return;
    dt = clampf(dt, 0, 0.05f);
    float speed = 18.0f + g->difficulty * 9.0f;
    float retreat = fminf(g->rollback, 54.0f * dt);
    g->rollback -= retreat;
    if (g->count) {
        g->ball_distance[0] += speed * dt - retreat;
        for (int i = 1; i < g->count; i++) {
            float target = g->ball_distance[i - 1] - BALL_STEP;
            float gap = target - g->ball_distance[i];
            float catchup = gap > 0.6f ? 70.0f : 0.0f;
            g->ball_distance[i] = fminf(target, g->ball_distance[i] +
                                        (speed + catchup) * dt - retreat);
        }
        g->lead = g->ball_distance[0];
    }
    g->spawn_clock += dt;
    if (g->spawned < g->quota && g->count < ZUMA_MAX_BALLS &&
        g->spawn_clock >= BALL_STEP / speed &&
        (g->count == 0 || g->ball_distance[g->count - 1] >= BALL_STEP)) {
        g->ball_distance[g->count] = g->count ?
            g->ball_distance[g->count - 1] - BALL_STEP : 0;
        g->balls[g->count] = incoming_color(g);
        g->count++;
        g->spawned++;
        g->spawn_clock = 0;
        g->lead = g->ball_distance[0];
    }
    if (g->count && g->ball_distance[0] >= g->path_length - 8) {
        g->mode = ZUMA_LOST;
        g->sound = ZUMA_SOUND_LOSE;
        return;
    }
    if (g->shot.active) {
        /* Two substeps prevent fast shots from tunnelling through a marble. */
        for (int step = 0; step < 2 && g->shot.active; step++) {
            g->shot.x += g->shot.vx * dt * 0.5f;
            g->shot.y += g->shot.vy * dt * 0.5f;
            for (int i = 0; i < g->count; i++) {
                float d = g->ball_distance[i];
                if (d < 0 || d >= g->path_length) continue;
                float x, y;
                zuma_path_position(g, d, &x, &y);
                float dx = x - g->shot.x, dy = y - g->shot.y;
                if (dx * dx + dy * dy <= 11.5f * 11.5f) {
                    insert_hit(g, i);
                    break;
                }
            }
            if (g->shot.x < -10 || g->shot.x > 250 ||
                g->shot.y < -10 || g->shot.y > 330) {
                g->shot.active = false;
                g->combo = 0;
            }
        }
    }
    for (int i = 1; i < g->count; i++) {
        if (touching(g, i - 1, i) && g->balls[i - 1] == g->balls[i]) {
            int old_count = g->count;
            resolve_matches(g, i);
            if (g->count != old_count) {
                i = 0;
                if (g->count) g->lead = g->ball_distance[0];
            }
        }
    }
    if (g->spawned == g->quota && g->count == 0) {
        g->mode = ZUMA_WON;
        g->sound = ZUMA_SOUND_WIN;
    }
}

zuma_sound_t zuma_take_sound(zuma_game_t *g) {
    zuma_sound_t result = g->sound;
    g->sound = ZUMA_SOUND_NONE;
    return result;
}
