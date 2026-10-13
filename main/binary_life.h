// 数字生命实验核心：无硬件依赖，可由宿主机重复测试。
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BL_SIDE 16u
#define BL_CELLS (BL_SIDE * BL_SIDE)
#define BL_SAVE_MAGIC 0x424C4142u
#define BL_SAVE_VERSION 1u

// 16 位基因组：低四位吸收、次四位转化、再四位复制阈值、高四位寿命。
// 模型是具遗传和选择压力的抽象人工生命，并非现实生物学预测。
typedef struct {
    uint16_t genome;
    uint8_t energy;
    uint8_t age;
    uint8_t resource;
    uint8_t alive;
} bl_cell_t;

typedef struct {
    uint32_t seed;              // 初始实验种子，便于复现实验
    uint32_t rng;               // Xorshift32 当前状态，断电后连续运行
    uint32_t generation;
    uint32_t births;
    uint32_t deaths;
    uint32_t mutations;
    uint8_t mutation_level;     // 0=关闭, 1=1/512, 2=1/128, 3=1/32
    uint8_t climate;            // 0=稳定补给, 1=周期资源波动
    uint8_t paused;
    uint8_t reserved;
    bl_cell_t cells[BL_CELLS];
} bl_world_t;

typedef struct {
    uint16_t population;
    uint16_t genotypes;
    uint16_t mean_energy;
    uint16_t representative;    // 当前能量最高存活个体的基因
    uint8_t oldest;
} bl_stats_t;

typedef struct {
    uint32_t magic;
    uint32_t version;
    bl_world_t world;
    uint32_t checksum;          // FNV-1a 检测记录损坏
} bl_snapshot_t;

void bl_reset(bl_world_t *world, uint32_t seed);
void bl_step(bl_world_t *world);
void bl_stats(const bl_world_t *world, bl_stats_t *stats);
void bl_genome_bits(uint16_t genome, char output[17]);
void bl_snapshot_encode(const bl_world_t *world, bl_snapshot_t *snapshot);
bool bl_snapshot_decode(bl_world_t *world, const bl_snapshot_t *snapshot);
