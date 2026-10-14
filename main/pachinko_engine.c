#include "pachinko_engine.h"

#include <math.h>
#include <string.h>

#define PK_DT (0.020f)         /* two physics substeps per 40 ms UI frame */
#define PK_RADIUS (3.4f)
#define PK_PEG_RADIUS (3.6f)
#define PK_GRAVITY (295.0f)
#define PK_SPEED_CAP (245.0f)

/* Small deterministic PRNG: seed is supplied by platform entropy, not money RNG. */
static uint32_t random32(pk_state_t *s)
{
    uint32_t x = s->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return s->rng = x ? x : 0xA341316Cu;
}

static uint32_t clamp_score_add(uint32_t score, uint32_t add)
{
    return add > UINT32_MAX - score ? UINT32_MAX : score + add;
}

static void award(pk_state_t *s, uint32_t value)
{
    s->score = clamp_score_add(s->score, value);
    s->last_award = value > INT16_MAX ? INT16_MAX : (int16_t)value;
    if (s->score > s->high_score[s->mode]) {
        s->high_score[s->mode] = s->score;
    }
}

int pk_peg_count(int row)
{
    return row < 0 || row >= PK_PEG_ROWS ? 0 : ((row & 1) ? 6 : 7);
}

void pk_peg_pos(int row, int col, float *x, float *y)
{
    if (x) *x = 29.0f + 30.0f * col + ((row & 1) ? 15.0f : 0.0f);
    if (y) *y = 107.0f + 25.0f * row;
}

float pk_bumper_x(const pk_state_t *s)
{
    if (s->mode != PK_SURVIVAL) return 119.0f;
    unsigned phase = s->ticks % 120u;
    /* Traveling target uses a triangular wave; no trigonometry or allocations. */
    return 75.0f + (phase < 60 ? phase : 120u - phase) * 1.47f;
}

int pk_active_balls(const pk_state_t *s)
{
    int n = 0;
    for (int i = 0; i < PK_MAX_BALLS; ++i) n += s->balls[i].active;
    return n;
}

void pk_start(pk_state_t *s, pk_mode_t mode)
{
    uint32_t best[PK_MODE_COUNT];
    memcpy(best, s->high_score, sizeof(best));
    uint32_t seed = s->rng ? s->rng : 0xB5297A4Du;
    memset(s, 0, sizeof(*s));
    memcpy(s->high_score, best, sizeof(best));
    s->rng = seed;
    s->mode = mode < PK_MODE_COUNT ? mode : PK_CLASSIC;
    s->ammo = s->mode == PK_CLASSIC ? 30 : (s->mode == PK_SURVIVAL ? 12 : -1);
    s->combo = 1;
    s->aim_x = 120;
    s->last_slot = 255;
}

void pk_init(pk_state_t *s, uint32_t seed)
{
    memset(s, 0, sizeof(*s));
    s->rng = seed ? seed : 0x9E3779B9u;
    pk_start(s, PK_CLASSIC);
}

void pk_aim(pk_state_t *s, int direction)
{
    if (s->finished) return;
    int x = (int)s->aim_x + (direction > 0 ? 9 : -9);
    s->aim_x = (int16_t)(x < 38 ? 38 : (x > 202 ? 202 : x));
}

bool pk_fire(pk_state_t *s)
{
    if (s->finished || s->ammo == 0) return false;
    if (s->mode == PK_RUSH && s->ticks >= 1500) return false;
    if (s->shots && s->ticks - s->last_shot_tick < 5) return false;
    for (int i = 0; i < PK_MAX_BALLS; ++i) {
        pk_ball_t *ball = &s->balls[i];
        if (ball->active) continue;
        uint32_t r = random32(s);
        *ball = (pk_ball_t) {
            .x = 120.0f, .y = 84.0f,
            .vx = ((float)s->aim_x - 120.0f) * 0.72f +
                  ((int)(r % 11) - 5) * 0.7f,
            .vy = 26.0f, .active = true,
        };
        if (s->ammo > 0) --s->ammo;
        ++s->shots;
        s->last_shot_tick = s->ticks;
        s->last_award = 0;
        s->last_jackpot = false;
        return true;
    }
    return false;
}

/* A deliberate one-shot impulse consumes bumper charge and is cooldown-limited. */
bool pk_nudge(pk_state_t *s)
{
    if (s->energy < 25 || !pk_active_balls(s)) return false;
    /* +1 distinguishes tick zero from the 'never nudged' sentinel. */
    if (s->last_nudge_tick &&
        (s->ticks + 1u - s->last_nudge_tick) < 40u) return false;
    s->energy -= 25;
    s->last_nudge_tick = s->ticks + 1u;
    for (int i = 0; i < PK_MAX_BALLS; ++i) {
        if (s->balls[i].active) {
            s->balls[i].vx += s->balls[i].x < 120.0f ? 48.0f : -48.0f;
            s->balls[i].vy -= 32.0f;
        }
    }
    return true;
}

static void hit_peg(pk_state_t *s, pk_ball_t *b, float cx, float cy)
{
    float dx = b->x - cx;
    float dy = b->y - cy;
    float d2 = dx * dx + dy * dy;
    float min_d = PK_RADIUS + PK_PEG_RADIUS;
    if (d2 >= min_d * min_d) return;
    float d = sqrtf(d2);
    if (d < 0.01f) { dx = 1.0f; dy = 0.0f; d = 1.0f; }
    float nx = dx / d, ny = dy / d;
    b->x += nx * (min_d - d + 0.08f);
    b->y += ny * (min_d - d + 0.08f);
    float vn = b->vx * nx + b->vy * ny;
    if (vn >= 0.0f) return; /* separation correction alone is not a new hit */
    b->vx -= 1.80f * vn * nx;
    b->vy -= 1.80f * vn * ny;
    b->vx += ((int)(random32(s) % 9) - 4) * 0.7f;
    if (b->hits < UINT8_MAX) ++b->hits;
    if ((b->hits % 4) == 0 && s->energy <= 97) s->energy += 3;
    if ((b->hits % 7) == 0 && s->combo < 8) ++s->combo;
}

static void hit_bumper(pk_state_t *s, pk_ball_t *b)
{
    float dx = b->x - pk_bumper_x(s);
    float dy = b->y - 170.0f;
    float d2 = dx * dx + dy * dy;
    if (d2 >= 14.0f * 14.0f) return;
    float d = sqrtf(d2);
    if (d < 0.01f) { dx = 1.0f; dy = 0; d = 1; }
    float nx = dx / d, ny = dy / d;
    b->x += nx * (14.1f - d);
    b->y += ny * (14.1f - d);
    float vn = b->vx * nx + b->vy * ny;
    if (vn < 0.0f) {
        b->vx -= 1.9f * vn * nx;
        b->vy -= 1.9f * vn * ny;
    }
    if (b->bumper_hit) return;
    b->bumper_hit = true;
    s->energy = (uint8_t)(s->energy > 80 ? 100 : s->energy + 20);
    award(s, 25u * s->combo);
    if (s->energy == 100) {
        s->fever_ticks = 250; /* ten seconds at 25 logic FPS */
        s->energy = 0;
    }
}

static void settle_ball(pk_state_t *s, pk_ball_t *b)
{
    static const uint16_t payouts[5] = { 10, 40, 160, 40, 10 };
    int slot = (int)((b->x - PK_BOARD_LEFT) / 42.8f);
    if (slot < 0) slot = 0;
    if (slot > 4) slot = 4;
    s->last_slot = (uint8_t)slot;
    uint32_t points = payouts[slot] * s->combo;
    if (s->mode == PK_SURVIVAL && (slot == 0 || slot == 4)) points = 0;
    if (s->fever_ticks) points *= 3;
    if (slot == 2) {
        bool same = true;
        for (int i = 0; i < 3; ++i) s->reels[i] = random32(s) % 3;
        for (int i = 1; i < 3; ++i) same &= s->reels[i] == s->reels[0];
        s->last_jackpot = same;
        if (same) {
            points += 1000;
            s->fever_ticks = 250;
            s->energy = 0;
        }
    } else {
        s->last_jackpot = false;
    }
    award(s, points);
    b->active = false;

    /* Jackpot grants two automatic extra balls; they consume no ammunition.
     * Reusing inactive fixed slots avoids allocating sprites at runtime. */
    if (s->last_jackpot) {
        int spawned = 0;
        for (int i = 0; i < PK_MAX_BALLS && spawned < 2; ++i) {
            if (s->balls[i].active) continue;
            uint32_t r = random32(s);
            s->balls[i] = (pk_ball_t) {
                .x = 115.0f + 10.0f * spawned,
                .y = 88.0f,
                .vx = ((int)(r % 91) - 45) * 0.8f,
                .vy = 28.0f,
                .active = true
            };
            ++spawned;
        }
    }
    /* New launch begins a fresh combo chain, but not a timed fever window. */
    if (!pk_active_balls(s)) s->combo = 1;
}

void pk_step(pk_state_t *s)
{
    if (s->finished) return;
    ++s->ticks;
    if (s->fever_ticks) --s->fever_ticks;
    for (int i = 0; i < PK_MAX_BALLS; ++i) {
        pk_ball_t *b = &s->balls[i];
        if (!b->active) continue;
        for (int sub = 0; sub < 2 && b->active; ++sub) {
            b->vy += PK_GRAVITY * PK_DT * (s->mode == PK_RUSH ? 1.3f : 1.0f);
            if (b->vy > PK_SPEED_CAP) b->vy = PK_SPEED_CAP;
            b->x += b->vx * PK_DT;
            b->y += b->vy * PK_DT;
            if (b->x < PK_BOARD_LEFT + PK_RADIUS) {
                b->x = PK_BOARD_LEFT + PK_RADIUS;
                b->vx = fabsf(b->vx) * 0.82f;
            } else if (b->x > PK_BOARD_RIGHT - PK_RADIUS) {
                b->x = PK_BOARD_RIGHT - PK_RADIUS;
                b->vx = -fabsf(b->vx) * 0.82f;
            }
            for (int row = 0; row < PK_PEG_ROWS; ++row) {
                for (int col = 0; col < pk_peg_count(row); ++col) {
                    float cx, cy;
                    pk_peg_pos(row, col, &cx, &cy);
                    hit_peg(s, b, cx, cy);
                }
            }
            hit_bumper(s, b);
            if (b->y >= PK_BOARD_BOTTOM - PK_RADIUS) {
                settle_ball(s, b);
                break;
            }
        }
    }
    if (!pk_active_balls(s) &&
        ((s->ammo == 0) ||
         (s->mode == PK_RUSH && s->ticks >= 1500))) {
        s->finished = true;
    }
}
