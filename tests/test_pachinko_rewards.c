#include "pachinko_rewards.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static void test_progressive_jackpot(void)
{
    pk_rewards_t r;
    pk_rewards_init(&r);
    assert(r.meta.pool == PK_POOL_SEED);
    pk_rewards_on_shot(&r);
    assert(r.meta.pool == PK_POOL_SEED + 12);
    uint8_t jackpot[3] = {4, 4, 4};
    pk_reward_result_t win = pk_rewards_spin(&r, jackpot);
    assert(win.prize == PK_PRIZE_JACKPOT);
    assert(win.points == PK_POOL_SEED + 12 + 30);
    assert(win.extra_balls == 2 && win.fever_ticks == 250);
    assert(r.meta.pool == PK_POOL_SEED);
    assert(r.meta.total_jackpots == 1 && r.meta.medals == 12);
    assert(r.event == PK_PRIZE_JACKPOT && r.event_ticks == 100);
    for (int i = 0; i < 100; ++i) pk_rewards_tick(&r);
    assert(r.event_ticks == 0);
}

static void test_award_tiers_and_pool_reserve(void)
{
    pk_rewards_t r;
    pk_rewards_init(&r);
    for (int i = 0; i < 100; ++i) pk_rewards_on_shot(&r);
    const uint32_t before = r.meta.pool + 30;
    uint8_t big[3] = {2, 2, 2};
    pk_reward_result_t award = pk_rewards_spin(&r, big);
    assert(award.prize == PK_PRIZE_BIG);
    assert(award.points == 300 + (before - PK_POOL_SEED) / 10);
    assert(r.meta.pool == before - (before - PK_POOL_SEED) / 10);
    uint32_t super_before = r.meta.pool + 30;
    uint8_t super[3] = {3, 3, 3};
    award = pk_rewards_spin(&r, super);
    assert(award.prize == PK_PRIZE_SUPER);
    assert(award.points == 750 + (super_before - PK_POOL_SEED) / 4);
    assert(r.meta.pool >= PK_POOL_SEED);

    uint8_t pair[3] = {0, 0, 1};
    award = pk_rewards_spin(&r, pair);
    assert(award.prize == PK_PRIZE_PAIR && award.points == 45);
    uint8_t mini[3] = {1, 1, 1};
    award = pk_rewards_spin(&r, mini);
    assert(award.prize == PK_PRIZE_MINI && award.points == 120);
    uint8_t blank[3] = {0, 1, 2};
    award = pk_rewards_spin(&r, blank);
    assert(award.prize == PK_PRIZE_NONE && award.points == 0);
}

static void test_visible_center_pity(void)
{
    pk_rewards_t r;
    pk_rewards_init(&r);
    for (int i = 0; i < (int)PK_PITY_SPINS - 1; ++i) {
        uint8_t no_award[3] = {0, 1, 2};
        pk_reward_result_t award = pk_rewards_spin(&r, no_award);
        assert(award.prize == PK_PRIZE_NONE);
    }
    assert(r.no_big_streak == PK_PITY_SPINS - 1);
    uint8_t forced[3] = {0, 1, 2};
    pk_reward_result_t reward = pk_rewards_spin(&r, forced);
    assert(reward.prize == PK_PRIZE_BIG);
    assert(forced[0] == 2 && forced[1] == 2 && forced[2] == 2);
    assert(r.no_big_streak == 0);
}

static void test_missions_and_persistence(void)
{
    pk_rewards_t r;
    pk_rewards_init(&r);
    assert(r.mission_goal == PK_GOAL_PEG && r.mission_target == 12);
    for (int i = 0; i < 11; ++i) {
        assert(pk_rewards_record(&r, PK_GOAL_PEG).prize == PK_PRIZE_NONE);
    }
    pk_reward_result_t reward = pk_rewards_record(&r, PK_GOAL_PEG);
    assert(reward.prize == PK_PRIZE_MISSION && reward.points == 350);
    assert(r.mission_stage == 2 && r.mission_goal == PK_GOAL_CENTER);
    assert(r.mission_progress == 0);
    assert(pk_rewards_record(&r, PK_GOAL_PEG).prize == PK_PRIZE_NONE);
    assert(pk_rewards_record(&r, PK_GOAL_CENTER).prize == PK_PRIZE_NONE);
    reward = pk_rewards_record(&r, PK_GOAL_CENTER);
    assert(reward.prize == PK_PRIZE_MISSION && reward.points == 450);
    assert(r.mission_stage == 3 && r.mission_goal == PK_GOAL_BUMPER);
    assert(pk_rewards_record(&r, PK_GOAL_BUMPER).prize == PK_PRIZE_NONE);
    reward = pk_rewards_record(&r, PK_GOAL_BUMPER);
    assert(reward.prize == PK_PRIZE_MISSION && reward.extra_balls == 2);

    const pk_reward_meta_t meta = r.meta;
    pk_rewards_new_round(&r);
    assert(r.mission_stage == 1 && r.mission_progress == 0);
    assert(r.meta.medals == meta.medals);
    assert(r.meta.pool == meta.pool);

    pk_reward_meta_t bad = { .pool = UINT32_MAX, .medals = 42 };
    pk_rewards_import(&r, &bad);
    assert(r.meta.pool == PK_POOL_CAP && r.meta.medals == 42);
}

int main(void)
{
    test_progressive_jackpot();
    test_award_tiers_and_pool_reserve();
    test_visible_center_pity();
    test_missions_and_persistence();
    puts("pachinko reward tests: PASS");
    return 0;
}
