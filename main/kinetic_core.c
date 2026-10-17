#include "kinetic_core.h"
#include <math.h>
#include <stddef.h>
#include <string.h>

#define KIN_G 0.30f
#define KIN_PI 3.14159265358979323846f

static uint32_t next_rand(kin_game_t *g)
{
    uint32_t x = g->rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return g->rng = x ? x : 0x9e3779b9u;
}

static float inv_mass(const kin_body_t *b) { return 1.0f / b->mass; }
static float inv_inertia(const kin_body_t *b) {
    return 2.0f / (b->mass * b->radius * b->radius);
}
static float clamp(float x, float a, float b) {
    return x < a ? a : (x > b ? b : x);
}

static kin_body_t *add_body(kin_game_t *g, kin_kind_t kind,
                            float x, float y, float r)
{
    for (int i = 0; i < KIN_MAX_BODIES; ++i) {
        kin_body_t *b = &g->bodies[i];
        if (!b->alive) {
            *b = (kin_body_t){
                .x = x, .y = y, .radius = r, .kind = (uint8_t)kind,
                .mass = kind == KIN_PROJECTILE ? 3.4f :
                        kind == KIN_STONE ? 2.8f : 1.2f,
                .alive = 1,
            };
            return b;
        }
    }
    return NULL;
}

static void stage_setup(kin_game_t *g)
{
    memset(g->bodies, 0, sizeof(g->bodies));
    g->ticks = 0;
    g->shots = 5;
    g->targets = 0;
    g->cooldown = 0;
    g->flash = 0;
    g->combo = 0;
    g->combo_ticks = 0;
    g->paused = 0;
    g->phase = KIN_PLAY;
    int rows = 2 + (int)((g->stage - 1u) % 3u);
    /* Packed, supported pyramid: five discs on the floor, fewer above.
     * Alternating stone targets change the cascade dynamics per stage. */
    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < 5 - row; ++col) {
            float x = 150.0f + (float)col * 16.2f + row * 8.1f;
            float y = 261.7f - 14.15f * row;
            int index = row * 7 + col;
            bool stone = ((index + g->stage + (next_rand(g) & 1u)) % 5u) == 0u;
            kin_kind_t kind = stone ? KIN_STONE : KIN_GLASS;
            kin_body_t *b = add_body(g, kind, x, y, 8.0f);
            if (b && kind == KIN_GLASS) ++g->targets;
        }
    }
}

void kin_new_game(kin_game_t *g, uint32_t seed)
{
    if (!g) return;
    memset(g, 0, sizeof(*g));
    g->rng = seed ? seed : 0xa47b9b2du;
    g->stage = 1;
    g->aim_deg = 10;
    g->power = 1;
    stage_setup(g);
}

void kin_next_stage(kin_game_t *g)
{
    if (!g || g->phase != KIN_WON) return;
    if (g->stage < 99) ++g->stage;
    g->score += 250;
    stage_setup(g);
}

void kin_aim(kin_game_t *g, int delta)
{
    if (!g) return;
    int a = (int)g->aim_deg + delta;
    g->aim_deg = (uint8_t)(a < 10 ? 10 : (a > 65 ? 65 : a));
}

void kin_power_next(kin_game_t *g)
{
    if (!g) return;
    g->power = (uint8_t)((g->power + 1u) % 3u);
}

bool kin_fire(kin_game_t *g)
{
    if (!g || g->phase != KIN_PLAY || g->paused ||
        g->shots == 0 || g->cooldown) return false;
    float a = (float)g->aim_deg * (KIN_PI / 180.0f);
    float v = 9.4f + (float)g->power * 1.7f;
    kin_body_t *ball = add_body(g, KIN_PROJECTILE, 25.0f, 243.0f, 7.0f);
    if (!ball) return false;
    ball->vx = v * cosf(a);
    ball->vy = -v * sinf(a);
    ball->omega = 0.08f;
    --g->shots;
    g->cooldown = 22;
    return true;
}

static void break_glass(kin_game_t *g, kin_body_t *victim)
{
    if (!victim->alive || victim->kind != KIN_GLASS) return;
    float cx = victim->x, cy = victim->y;
    victim->alive = 0;
    if (g->targets) --g->targets;
    if (g->combo_ticks) ++g->combo;
    else g->combo = 1;
    g->combo_ticks = 50;
    g->score += 100u + 25u * (g->combo > 12u ? 12u : g->combo);
    g->flash = 12;
    g->flash_x = cx;
    g->flash_y = cy;
    ++g->hits;
    /* Breaking a shell transfers residual impulse to nearby dynamic bodies. */
    for (int i = 0; i < KIN_MAX_BODIES; ++i) {
        kin_body_t *b = &g->bodies[i];
        if (!b->alive) continue;
        float dx = b->x - cx, dy = b->y - cy;
        float ds = dx * dx + dy * dy;
        if (ds > 33.0f * 33.0f || ds < 0.01f) continue;
        float d = sqrtf(ds);
        float kick = 1.65f * (1.0f - d / 33.0f);
        b->vx += kick * dx / d;
        b->vy += kick * dy / d - 0.12f;
    }
}

static void collide_pair(kin_game_t *g, kin_body_t *a, kin_body_t *b)
{
    if (!a->alive || !b->alive) return;
    float dx = b->x - a->x, dy = b->y - a->y;
    float min_dist = a->radius + b->radius;
    float dist_sq = dx * dx + dy * dy;
    if (dist_sq >= min_dist * min_dist) return;
    float dist = sqrtf(dist_sq);
    float nx = dist > 0.001f ? dx / dist : 1.0f;
    float ny = dist > 0.001f ? dy / dist : 0.0f;
    float ia = inv_mass(a), ib = inv_mass(b);
    float total = ia + ib;
    float penetration = min_dist - dist;
    float correction = fmaxf(penetration - 0.02f, 0.0f) * 0.65f / total;
    a->x -= nx * correction * ia;
    a->y -= ny * correction * ia;
    b->x += nx * correction * ib;
    b->y += ny * correction * ib;

    float rvx = b->vx - a->vx, rvy = b->vy - a->vy;
    float normal_v = rvx * nx + rvy * ny;
    if (normal_v >= -0.015f) return;
    float impact = -normal_v;
    float restitution = (a->kind == KIN_STONE || b->kind == KIN_STONE)
                            ? 0.37f : 0.48f;
    float impulse = -(1.0f + restitution) * normal_v / total;
    a->vx -= impulse * ia * nx;
    a->vy -= impulse * ia * ny;
    b->vx += impulse * ib * nx;
    b->vy += impulse * ib * ny;

    /* Coulomb friction at the two contact points, including angular inertia. */
    float tx = -ny, ty = nx;
    float slip = (b->vx - a->vx) * tx + (b->vy - a->vy) * ty -
                 b->omega * b->radius - a->omega * a->radius;
    float denom = total + a->radius * a->radius * inv_inertia(a) +
                  b->radius * b->radius * inv_inertia(b);
    float jt = clamp(-slip / denom, -0.38f * impulse, 0.38f * impulse);
    a->vx -= jt * tx * ia;
    a->vy -= jt * ty * ia;
    b->vx += jt * tx * ib;
    b->vy += jt * ty * ib;
    a->omega -= jt * a->radius * inv_inertia(a);
    b->omega -= jt * b->radius * inv_inertia(b);

    /* Impact energy determines destruction, not a scripted hit animation. */
    if (impact > 2.9f) {
        if (a->kind == KIN_GLASS) break_glass(g, a);
        if (b->kind == KIN_GLASS) break_glass(g, b);
    }
}

static void collide_bounds(kin_body_t *b)
{
    if (!b->alive) return;
    if (b->x < KIN_LEFT + b->radius) {
        b->x = KIN_LEFT + b->radius;
        if (b->vx < 0) b->vx *= -0.42f;
    }
    if (b->x > KIN_RIGHT - b->radius) {
        b->x = KIN_RIGHT - b->radius;
        if (b->vx > 0) b->vx *= -0.42f;
    }
    if (b->y < KIN_TOP + b->radius) {
        b->y = KIN_TOP + b->radius;
        if (b->vy < 0) b->vy *= -0.36f;
    }
    if (b->y > KIN_FLOOR - b->radius) {
        b->y = KIN_FLOOR - b->radius;
        if (b->vy > 0.0f) b->vy *= -0.19f;
        if (fabsf(b->vy) < 0.12f) b->vy = 0;
        float slip = b->vx - b->omega * b->radius;
        float j = clamp(-slip * b->mass / 3.0f, -0.17f, 0.17f);
        b->vx += j / b->mass;
        b->omega -= j * b->radius * inv_inertia(b);
        b->vx *= 0.994f;
        b->omega *= 0.988f;
    }
}

void kin_step(kin_game_t *g)
{
    if (!g || g->phase != KIN_PLAY || g->paused) return;
    ++g->ticks;
    if (g->cooldown) --g->cooldown;
    if (g->flash) --g->flash;
    if (g->combo_ticks && --g->combo_ticks == 0) g->combo = 0;

    /* Two 120 Hz integration substeps reduce tunneling; three solver passes
     * stabilize the stacked discs without a heap or dynamic allocations. */
    for (int sub = 0; sub < 2; ++sub) {
        for (int i = 0; i < KIN_MAX_BODIES; ++i) {
            kin_body_t *b = &g->bodies[i];
            if (!b->alive) continue;
            b->vy += KIN_G * 0.5f;
            b->x += b->vx * 0.5f;
            b->y += b->vy * 0.5f;
            b->angle += b->omega * 0.5f;
            b->vx *= 0.999f;
            b->omega *= 0.998f;
            collide_bounds(b);
        }
        for (int pass = 0; pass < 3; ++pass) {
            for (int i = 0; i < KIN_MAX_BODIES; ++i) {
                if (!g->bodies[i].alive) continue;
                for (int j = i + 1; j < KIN_MAX_BODIES; ++j) {
                    collide_pair(g, &g->bodies[i], &g->bodies[j]);
                }
            }
            for (int i = 0; i < KIN_MAX_BODIES; ++i)
                collide_bounds(&g->bodies[i]);
        }
    }
    /* Retire projectiles at rest: keeping them would block all five shots
     * from fitting and make endless low-energy contacts. */
    for (int i = 0; i < KIN_MAX_BODIES; ++i) {
        kin_body_t *b = &g->bodies[i];
        if (b->kind == KIN_PROJECTILE && b->alive &&
            fabsf(b->vx) + fabsf(b->vy) < 0.11f &&
            b->y > KIN_FLOOR - b->radius - 0.5f && g->ticks > 90)
            b->alive = 0;
    }
    if (g->targets == 0) g->phase = KIN_WON;
    else if (g->shots == 0 && g->ticks > 240) {
        bool moving = false;
        for (int i = 0; i < KIN_MAX_BODIES; ++i) {
            const kin_body_t *b = &g->bodies[i];
            if (b->alive && (fabsf(b->vx) + fabsf(b->vy) > 0.5f)) {
                moving = true; break;
            }
        }
        if (!moving && g->cooldown == 0) g->phase = KIN_LOST;
        if (g->ticks > 3600) g->phase = KIN_LOST; /* Never soft-lock */
    }
}

uint8_t kin_active_count(const kin_game_t *g)
{
    if (!g) return 0;
    uint8_t count = 0;
    for (int i = 0; i < KIN_MAX_BODIES; ++i)
        count += !!g->bodies[i].alive;
    return count;
}
