// 数字生命实验的确定性和存档安全回归测试；可在无 ESP-IDF 主机上运行。
#include "binary_life.h"
#include <assert.h>
#include <string.h>

static unsigned alive_count(const bl_world_t *w)
{
    unsigned count = 0;
    for (unsigned i = 0; i < BL_CELLS; ++i) count += w->cells[i].alive;
    return count;
}

int main(void)
{
    bl_world_t a, b, loaded;
    bl_stats_t stats;
    bl_snapshot_t snapshot;
    char bits[17];

    bl_reset(&a, 123456789u);
    bl_reset(&b, 123456789u);
    assert(memcmp(&a, &b, sizeof(a)) == 0);
    assert(a.seed == 123456789u);
    assert(a.mutation_level == 2u);
    assert(alive_count(&a) > 0);

    // 同种子、同参数、同时间步严格一致；重放不依赖系统全局随机数。
    for (unsigned i = 0; i < 150u; ++i) {
        bl_step(&a);
        bl_step(&b);
    }
    assert(a.generation == 150u);
    assert(memcmp(&a, &b, sizeof(a)) == 0);
    assert(a.mutations <= a.births);
    bl_stats(&a, &stats);
    assert(stats.population == alive_count(&a));
    assert(stats.genotypes <= stats.population);
    assert(stats.mean_energy <= 120u);

    // 暂停时任何世界数值都不变化。
    a.paused = 1;
    b.paused = 1;
    bl_step(&a);
    assert(memcmp(&a, &b, sizeof(a)) == 0);
    a.paused = 0;

    // 稳定的二进制基因字串。
    bl_genome_bits(0xA5F0u, bits);
    assert(strcmp(bits, "1010010111110000") == 0);
    bl_genome_bits(0u, bits);
    assert(strcmp(bits, "0000000000000000") == 0);

    // 快照在中断与重启后保持 RNG、年龄、资源、累计事件及控制参数。
    bl_snapshot_encode(&a, &snapshot);
    memset(&loaded, 0, sizeof(loaded));
    assert(bl_snapshot_decode(&loaded, &snapshot));
    assert(memcmp(&a, &loaded, sizeof(a)) == 0);
    bl_step(&a);
    bl_step(&loaded);
    assert(memcmp(&a, &loaded, sizeof(a)) == 0);

    // 任意 bit flip、版本漂移及无效参数必须拒绝，不污染原有世界。
    bl_snapshot_encode(&a, &snapshot);
    snapshot.world.cells[BL_CELLS / 2].genome ^= 1u;
    assert(!bl_snapshot_decode(&loaded, &snapshot));
    assert(memcmp(&a, &loaded, sizeof(a)) == 0);
    bl_snapshot_encode(&a, &snapshot);
    ++snapshot.version;
    assert(!bl_snapshot_decode(&loaded, &snapshot));

    // 单一亲代只能在一代产生一个子代；子代不会在同代继续繁殖。
    bl_reset(&a, 7u);
    for (unsigned i = 0; i < BL_CELLS; ++i) {
        a.cells[i].alive = 0;
        a.cells[i].energy = 0;
    }
    unsigned middle = (BL_SIDE / 2) * BL_SIDE + BL_SIDE / 2;
    a.cells[middle].alive = 1;
    a.cells[middle].genome = 0u;
    a.cells[middle].energy = 120u;
    a.cells[middle].resource = 100u;
    a.mutation_level = 0u;
    bl_step(&a);
    assert(a.births == 1u);
    assert(alive_count(&a) == 2u);
    assert(a.mutations == 0u);

    // 人为构造唯一基因型与重复基因型的统计校验。
    memset(&a, 0, sizeof(a));
    a.cells[0].alive = a.cells[1].alive = a.cells[2].alive = 1;
    a.cells[0].genome = a.cells[1].genome = 0x0110u;
    a.cells[2].genome = 0xAEEFu;
    a.cells[0].energy = 30; a.cells[1].energy = 60;
    a.cells[2].energy = 90;
    bl_stats(&a, &stats);
    assert(stats.population == 3u && stats.genotypes == 2u);
    assert(stats.mean_energy == 60u && stats.representative == 0xAEEFu);

    return 0;
}
