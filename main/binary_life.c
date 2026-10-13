#include "binary_life.h"
#include <string.h>
#include <stddef.h>

// 全部随机性仅来源于世界内部 RNG 状态，固定种子和操作序列可重放。
static uint32_t next_random(bl_world_t *w)
{
    uint32_t x = w->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    w->rng = x;
    return x;
}

static uint8_t minimum_u8(uint16_t a, uint16_t b)
{
    return (uint8_t)(a < b ? a : b);
}

void bl_reset(bl_world_t *w, uint32_t seed)
{
    if (!w) return;
    memset(w, 0, sizeof(*w));
    w->seed = seed ? seed : 0xA17E2026u;
    w->rng = w->seed;
    w->mutation_level = 2;
    w->climate = 1;

    // 空间初态的基因和食物随机但可重现；不是预设宠物/物种。
    for (size_t i = 0; i < BL_CELLS; ++i) {
        bl_cell_t *c = &w->cells[i];
        c->resource = (uint8_t)(24u + next_random(w) % 65u);
        if (next_random(w) % 100u < 22u) {
            c->alive = 1;
            c->genome = (uint16_t)next_random(w);
            c->energy = (uint8_t)(34u + next_random(w) % 37u);
        }
    }
}

static void die(bl_world_t *w, bl_cell_t *c)
{
    if (!c->alive) return;
    c->alive = 0;
    c->energy = 0;
    c->age = 0;
    c->resource = minimum_u8((uint16_t)c->resource + 9u, 120u);
    ++w->deaths;
}

void bl_step(bl_world_t *w)
{
    if (!w || w->paused) return;
    ++w->generation;

    // 全部出生个体必须从下一代开始计时，避免数组遍历方向造成加速生长。
    uint8_t newborn[(BL_CELLS + 7u) / 8u] = {0};
    for (size_t i = 0; i < BL_CELLS; ++i) {
        bl_cell_t *c = &w->cells[i];
        uint8_t supply = (uint8_t)(1u + ((i + w->generation / 60u) % 7u < 3u));
        if (w->climate && (w->generation / 100u) % 2u) supply = 1;
        c->resource = minimum_u8((uint16_t)c->resource + supply, 120u);
    }

    // 随机起点可减少固定次序的空间偏差；同代中非新生个体最多更新一次。
    size_t start = next_random(w) % BL_CELLS;
    for (size_t n = 0; n < BL_CELLS; ++n) {
        size_t i = (start + n) % BL_CELLS;
        bl_cell_t *c = &w->cells[i];
        if (!c->alive || (newborn[i >> 3] & (1u << (i & 7u)))) continue;

        uint16_t g = c->genome;
        uint8_t take = minimum_u8(c->resource, 1u + (g & 3u));
        c->resource -= take;
        uint8_t gain = (uint8_t)(take * (1u + ((g >> 4) & 3u)));
        uint8_t maintenance = (uint8_t)(2u + ((g >> 2) & 3u) + ((g >> 6) & 1u));
        uint16_t new_energy = (uint16_t)c->energy + gain;
        c->energy = new_energy > maintenance
            ? minimum_u8(new_energy - maintenance, 120u) : 0;
        if (c->age < 255u) ++c->age;

        uint8_t lifespan = (uint8_t)(30u + 6u * ((g >> 12) & 15u));
        if (c->energy == 0 || c->age >= lifespan) {
            die(w, c);
            continue;
        }

        uint8_t threshold = (uint8_t)(48u + 3u * ((g >> 8) & 15u));
        if (c->energy < threshold) continue;

        // 子代只能落在相邻空格；空间有限构成自然资源/空间选择压力。
        static const int8_t dx[8] = {-1,0,1,-1,1,-1,0,1};
        static const int8_t dy[8] = {-1,-1,-1,0,0,1,1,1};
        int x = (int)(i % BL_SIDE), y = (int)(i / BL_SIDE);
        unsigned first = next_random(w) % 8u;
        for (unsigned j = 0; j < 8u; ++j) {
            unsigned k = (first + j) % 8u;
            int nx = x + dx[k], ny = y + dy[k];
            if (nx < 0 || ny < 0 || nx >= (int)BL_SIDE || ny >= (int)BL_SIDE)
                continue;
            size_t child_index = (size_t)ny * BL_SIDE + (size_t)nx;
            bl_cell_t *child = &w->cells[child_index];
            if (child->alive) continue;

            uint16_t genome = g;
            uint16_t denominator = w->mutation_level == 1 ? 512u :
                                   w->mutation_level == 2 ? 128u :
                                   w->mutation_level == 3 ? 32u : 0u;
            if (denominator && next_random(w) % denominator == 0u) {
                genome ^= (uint16_t)(1u << (next_random(w) % 16u));
                ++w->mutations;
            }
            uint8_t split = c->energy / 2u;
            c->energy -= split;
            child->genome = genome;
            child->energy = split;
            child->age = 0;
            child->alive = 1;
            newborn[child_index >> 3] |= (uint8_t)(1u << (child_index & 7u));
            ++w->births;
            break;
        }
    }
}

void bl_stats(const bl_world_t *w, bl_stats_t *s)
{
    if (!w || !s) return;
    memset(s, 0, sizeof(*s));
    uint32_t total = 0;
    uint8_t highest = 0;
    for (size_t i = 0; i < BL_CELLS; ++i) {
        const bl_cell_t *c = &w->cells[i];
        if (!c->alive) continue;
        ++s->population;
        total += c->energy;
        if (c->age > s->oldest) s->oldest = c->age;
        if (c->energy >= highest) {
            highest = c->energy;
            s->representative = c->genome;
        }
        bool seen = false;
        for (size_t j = 0; j < i; ++j) {
            if (w->cells[j].alive && w->cells[j].genome == c->genome) {
                seen = true;
                break;
            }
        }
        if (!seen) ++s->genotypes;
    }
    if (s->population) s->mean_energy = (uint16_t)(total / s->population);
}

void bl_genome_bits(uint16_t genome, char output[17])
{
    if (!output) return;
    for (unsigned i = 0; i < 16u; ++i)
        output[i] = (genome & (1u << (15u - i))) ? '1' : '0';
    output[16] = '\0';
}

static uint32_t fnv1a(const void *ptr, size_t size)
{
    const uint8_t *bytes = ptr;
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < size; ++i)
        hash = (hash ^ bytes[i]) * 16777619u;
    return hash;
}

void bl_snapshot_encode(const bl_world_t *w, bl_snapshot_t *snapshot)
{
    if (!w || !snapshot) return;
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->magic = BL_SAVE_MAGIC;
    snapshot->version = BL_SAVE_VERSION;
    snapshot->world = *w;
    snapshot->checksum = fnv1a(snapshot, offsetof(bl_snapshot_t, checksum));
}

bool bl_snapshot_decode(bl_world_t *w, const bl_snapshot_t *snapshot)
{
    if (!w || !snapshot || snapshot->magic != BL_SAVE_MAGIC ||
        snapshot->version != BL_SAVE_VERSION ||
        snapshot->checksum != fnv1a(snapshot, offsetof(bl_snapshot_t, checksum)))
        return false;
    if (!snapshot->world.rng || snapshot->world.mutation_level > 3u ||
        snapshot->world.climate > 1u || snapshot->world.paused > 1u)
        return false;
    for (size_t i = 0; i < BL_CELLS; ++i) {
        const bl_cell_t *c = &snapshot->world.cells[i];
        if (c->alive > 1u || c->energy > 120u || c->resource > 120u) return false;
    }
    *w = snapshot->world;
    return true;
}
