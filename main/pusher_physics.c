#include "pusher_physics.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#define CP_PI 3.14159265358979323846f
#define CP_EDGE_GUTTER 24.0f
#define CP_GRAVITY 560.0f
#define CP_PHYS_DT (1.0f / 60.0f)

static float minf(float a, float b) { return a < b ? a : b; }
static float maxf(float a, float b) { return a > b ? a : b; }
static float clampf(float a, float lo, float hi) { return minf(hi, maxf(lo, a)); }

static uint32_t rnd(cp_game_t *g)
{
    uint32_t x = g->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g->rng = x ? x : 0x6d2b79f5u;
    return g->rng;
}

static float unit(cp_game_t *g)
{
    return (float)(rnd(g) & 0xffffu) / 65535.0f;
}

static cp_coin_t *spawn(cp_game_t *g, float x, float y, float z,
                        cp_coin_kind_t kind)
{
    for (unsigned i = 0; i < CP_MAX_COINS; ++i) {
        cp_coin_t *c = &g->coins[i];
        if (c->active) continue;
        memset(c, 0, sizeof(*c));
        c->active = true;
        c->kind = kind;
        c->radius = kind == CP_COIN_GIANT ? 11.8f : 8.2f;
        c->height = kind == CP_COIN_GIANT ? 4.8f : 2.9f;
        c->mass = kind == CP_COIN_GIANT ? 2.5f : 1.0f;
        c->x = clampf(x, c->radius, CP_BOARD_WIDTH - c->radius);
        c->y = y;
        c->z = z;
        return c;
    }
    return NULL;
}

unsigned cp_coin_count(const cp_game_t *g)
{
    unsigned n = 0;
    for (unsigned i = 0; i < CP_MAX_COINS; ++i) n += g->coins[i].active ? 1u : 0u;
    return n;
}

static void begin_event(cp_game_t *g, cp_event_t event)
{
    g->event = event;
    g->event_clock = 0;
    g->event_left = event == CP_EVENT_JACKPOT ? 2.0f : 5.5f;
    if (event == CP_EVENT_COIN_RAIN) {
        for (int n = 0; n < 6; ++n) {
            cp_coin_kind_t kind = n == 5 ? CP_COIN_RUBY : CP_COIN_GOLD;
            cp_coin_t *c = spawn(g, 15.0f + 178.0f * unit(g),
                                 48.0f + 30.0f * unit(g), 8.0f + n * 2.0f, kind);
            if (c) c->vy = 18.0f;
        }
    } else if (event == CP_EVENT_PORTAL) {
        for (unsigned i = 0; i < CP_MAX_COINS; ++i) {
            cp_coin_t *c = &g->coins[i];
            if (c->active && !c->falling && c->y < 135.0f) {
                c->x = 28.0f + 152.0f * unit(g);
                c->y = 145.0f;
                c->z += 8.0f;
                c->vy = 30.0f;
                break;
            }
        }
    } else if (event == CP_EVENT_JACKPOT) {
        g->bank += g->jackpot_pool;
        g->score += g->jackpot_pool;
        g->jackpot_hits++;
        g->jackpot_pool = 30;
        if (g->score > g->best) g->best = g->score;
    }
}

static void choose_event(cp_game_t *g)
{
    /* No rigged payout promise: seeded PRNG, probabilities are game mechanics. */
    uint32_t roll = rnd(g) % 100u;
    cp_event_t event = roll < 19 ? CP_EVENT_COIN_RAIN
                     : roll < 38 ? CP_EVENT_MAGNET
                     : roll < 51 ? CP_EVENT_EARTHQUAKE
                     : roll < 65 ? CP_EVENT_GOLD_RUSH
                     : roll < 78 ? CP_EVENT_MOON
                     : roll < 89 ? CP_EVENT_PORTAL
                     : roll < 98 ? CP_EVENT_BLACK_HOLE
                     : CP_EVENT_JACKPOT;
    begin_event(g, event);
}

void cp_init(cp_game_t *g, uint32_t seed, uint32_t bank,
             uint32_t best, uint32_t jackpot_pool)
{
    memset(g, 0, sizeof(*g));
    g->rng = seed ? seed : 0xa341316cu;
    g->bank = bank;
    g->best = best;
    g->jackpot_pool = jackpot_pool;
    g->drop_x = 104.0f;
    g->pusher_y = 28.0f;
    /* Packed staggered rows provide a physical pile from the first frame. */
    for (unsigned row = 0; row < 6; ++row) {
        for (unsigned col = 0; col < 9; ++col) {
            float x = 17.0f + col * 21.7f + (row % 2) * 2.0f;
            float y = 72.0f + row * 17.4f;
            cp_coin_kind_t kind = ((row * 9 + col) % 17 == 0) ? CP_COIN_GOLD
                                 : CP_COIN_COPPER;
            spawn(g, x, y, 0.0f, kind);
        }
    }
}

void cp_aim(cp_game_t *g, float offset)
{
    if (!g->paused) g->drop_x = clampf(g->drop_x + offset, 12.0f, 196.0f);
}

bool cp_drop(cp_game_t *g)
{
    if (g->paused || g->bank == 0 || cp_coin_count(g) >= CP_MAX_COINS) return false;
    uint32_t r = rnd(g) % 100u;
    cp_coin_kind_t kind = r < 77 ? CP_COIN_COPPER
                        : r < 95 ? CP_COIN_GOLD
                        : r < 99 ? CP_COIN_RUBY : CP_COIN_GIANT;
    cp_coin_t *c = spawn(g, g->drop_x, g->pusher_y + 13.0f, 5.0f, kind);
    if (!c) return false;
    c->vy = 10.0f;
    g->bank--;
    g->drops++;
    g->jackpot_pool += 1u;
    if (g->event_left <= 0.0f && (g->drops % 7u == 0u || rnd(g) % 13u == 0u)) {
        choose_event(g);
    }
    return true;
}

bool cp_buy_chaos(cp_game_t *g)
{
    if (g->paused || g->bank < 6u) return false;
    g->bank -= 6u;
    g->jackpot_pool += 6u;
    choose_event(g);
    return true;
}

static void payout(cp_game_t *g, cp_coin_t *c)
{
    if (c->x < CP_EDGE_GUTTER || c->x > CP_BOARD_WIDTH - CP_EDGE_GUTTER) {
        g->lost++;
    } else {
        uint32_t value = c->kind == CP_COIN_RUBY ? 15u :
                         c->kind == CP_COIN_GOLD ? 3u :
                         c->kind == CP_COIN_GIANT ? 9u : 1u;
        if (g->combo_left > 0.0f && g->combo < 5) g->combo++;
        else g->combo = 1;
        g->combo_left = 2.4f;
        uint32_t multiplier = g->event == CP_EVENT_GOLD_RUSH ? 3u : 1u;
        uint32_t award = value * multiplier * g->combo;
        g->bank += award;
        g->score += award;
        g->collected++;
        if (g->score > g->best) g->best = g->score;
        if (c->kind == CP_COIN_RUBY && (rnd(g) % 8u) == 0u) {
            begin_event(g, CP_EVENT_JACKPOT);
        }
    }
    c->active = false;
}

static void pair_contact(cp_coin_t *a, cp_coin_t *b)
{
    if (!a->active || !b->active || a->falling || b->falling) return;
    float dx = b->x - a->x;
    float dy = b->y - a->y;
    float dist2 = dx * dx + dy * dy;
    float radius = a->radius + b->radius;
    if (dist2 >= radius * radius) return;
    float dz = b->z - a->z;
    float thickness = (a->height + b->height) * 0.5f;
    float vpen = thickness - fabsf(dz);
    if (vpen <= 0.0f) return;
    float dist = sqrtf(maxf(dist2, 0.0001f));
    float hpen = radius - dist;
    float inv_a = 1.0f / a->mass;
    float inv_b = 1.0f / b->mass;
    float weight = inv_a + inv_b;
    /* SAT-style contact selection for finite cylinders, not point sprites. */
    if (vpen < hpen * 0.65f) {
        float sign = (dz > 0.001f || (fabsf(dz) < 0.001f && a < b)) ? 1.0f : -1.0f;
        float correction = (vpen + 0.001f) / weight;
        a->z -= sign * correction * inv_a;
        b->z += sign * correction * inv_b;
        a->vz *= 0.5f;
        b->vz *= 0.5f;
    } else {
        float nx = dist2 > 0.0001f ? dx / dist : 1.0f;
        float ny = dist2 > 0.0001f ? dy / dist : 0.0f;
        float correction = hpen * 0.88f / weight;
        a->x -= nx * correction * inv_a;
        a->y -= ny * correction * inv_a;
        b->x += nx * correction * inv_b;
        b->y += ny * correction * inv_b;
        float rel = (b->vx - a->vx) * nx + (b->vy - a->vy) * ny;
        if (rel < 0.0f) {
            float impulse = -1.13f * rel / weight;
            a->vx -= impulse * nx * inv_a;
            a->vy -= impulse * ny * inv_a;
            b->vx += impulse * nx * inv_b;
            b->vy += impulse * ny * inv_b;
        }
    }
}

static void simulate(cp_game_t *g, float dt)
{
    float previous_pusher = g->pusher_y;
    g->phase += dt * (2.0f * CP_PI / 2.1f);
    if (g->phase > 2.0f * CP_PI) g->phase -= 2.0f * CP_PI;
    g->pusher_y = 24.0f + 17.0f * (1.0f - cosf(g->phase)) * 0.5f;
    float pusher_speed = maxf(0.0f, (g->pusher_y - previous_pusher) / dt);

    if (g->event == CP_EVENT_EARTHQUAKE) {
        g->event_clock += dt;
        if (g->event_clock > 0.11f) {
            g->event_clock = 0;
            for (unsigned i = 0; i < CP_MAX_COINS; ++i) {
                cp_coin_t *c = &g->coins[i];
                if (c->active && !c->falling) {
                    c->vx += (unit(g) - 0.5f) * 27.0f;
                    c->vy += (unit(g) - 0.35f) * 23.0f;
                }
            }
        }
    }

    for (unsigned i = 0; i < CP_MAX_COINS; ++i) {
        cp_coin_t *c = &g->coins[i];
        if (!c->active) continue;
        if (!c->falling && c->y >= CP_FRONT_EDGE) c->falling = true;
        if (g->event == CP_EVENT_MAGNET || g->event == CP_EVENT_BLACK_HOLE) {
            float tx = 104.0f - c->x;
            c->vx += clampf(tx * (g->event == CP_EVENT_BLACK_HOLE ? 3.0f : 1.3f),
                            -90.0f, 90.0f) * dt;
            c->vy += (g->event == CP_EVENT_BLACK_HOLE ? 115.0f : 36.0f) * dt;
        }
        float grav = g->event == CP_EVENT_MOON ? CP_GRAVITY * 0.12f : CP_GRAVITY;
        c->vz -= grav * dt;
        c->x += c->vx * dt;
        c->y += c->vy * dt;
        c->z += c->vz * dt;
        c->vx *= 0.967f;
        c->vy *= c->falling ? 0.991f : 0.968f;
        if (!c->falling) {
            if (c->x < c->radius) {
                c->x = c->radius;
                c->vx = fabsf(c->vx) * 0.2f;
            }
            if (c->x > CP_BOARD_WIDTH - c->radius) {
                c->x = CP_BOARD_WIDTH - c->radius;
                c->vx = -fabsf(c->vx) * 0.2f;
            }
            if (c->y < g->pusher_y + c->radius && c->z < 7.0f) {
                c->y = g->pusher_y + c->radius;
                c->vy = maxf(c->vy, pusher_speed * 0.85f);
            }
            if (c->z < 0.0f) {
                c->z = 0.0f;
                c->vz = 0.0f;
            }
        }
    }

    /* Fixed solver budget: 72 coins, 2 iterations, no allocations. */
    for (unsigned iteration = 0; iteration < 2; ++iteration) {
        for (unsigned i = 0; i < CP_MAX_COINS; ++i) {
            if (!g->coins[i].active || g->coins[i].falling) continue;
            for (unsigned j = i + 1; j < CP_MAX_COINS; ++j) {
                pair_contact(&g->coins[i], &g->coins[j]);
            }
        }
        for (unsigned i = 0; i < CP_MAX_COINS; ++i) {
            cp_coin_t *c = &g->coins[i];
            if (!c->active || c->falling) continue;
            c->z = maxf(c->z, 0.0f);
            c->x = clampf(c->x, c->radius, CP_BOARD_WIDTH - c->radius);
            if (c->y < g->pusher_y + c->radius && c->z < 7.0f) {
                c->y = g->pusher_y + c->radius;
            }
        }
    }
    for (unsigned i = 0; i < CP_MAX_COINS; ++i) {
        cp_coin_t *c = &g->coins[i];
        if (!c->active) continue;
        if (!c->falling && c->y >= CP_FRONT_EDGE) c->falling = true;
        if (c->falling && c->z < -18.0f) payout(g, c);
    }
}

void cp_step(cp_game_t *g, float seconds)
{
    if (g->paused || seconds <= 0.0f) return;
    /* Use two fixed substeps per rendered 30 Hz frame; clamp long stalls. */
    float remaining = minf(seconds, CP_PHYS_DT * 3.0f);
    while (remaining > 0.00001f) {
        float dt = minf(remaining, CP_PHYS_DT);
        simulate(g, dt);
        remaining -= dt;
        g->game_time += dt;
        if (g->event_left > 0.0f) {
            g->event_left -= dt;
            if (g->event_left <= 0.0f) {
                g->event_left = 0.0f;
                g->event = CP_EVENT_NONE;
            }
        }
        if (g->combo_left > 0.0f) {
            g->combo_left -= dt;
            if (g->combo_left <= 0.0f) g->combo = 0;
        }
    }
}

const char *cp_event_name(cp_event_t event)
{
    switch (event) {
    case CP_EVENT_COIN_RAIN: return "COIN RAIN";
    case CP_EVENT_MAGNET: return "MAGNET";
    case CP_EVENT_EARTHQUAKE: return "EARTHQUAKE";
    case CP_EVENT_GOLD_RUSH: return "GOLD RUSH x3";
    case CP_EVENT_MOON: return "MOON GRAVITY";
    case CP_EVENT_PORTAL: return "WORMHOLE";
    case CP_EVENT_BLACK_HOLE: return "BLACK HOLE";
    case CP_EVENT_JACKPOT: return "JACKPOT!";
    default: return "PUSH YOUR LUCK";
    }
}
