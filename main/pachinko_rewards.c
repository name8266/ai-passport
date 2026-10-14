#include "pachinko_rewards.h"

#include <limits.h>
#include <string.h>

/*
 * Pool is a virtual score bank, never convertible or redeemable.
 * Gameplay inputs are deterministic: PRNG is owned by the physics engine.
 * The 12-spin guarantee applies only to CENTER pocket visits, not every shot.
 */
static uint32_t saturating_add(uint32_t value, uint32_t increment, uint32_t cap)
{
    if (value >= cap) return cap;
    return increment > cap - value ? cap : value + increment;
}

static void set_event(pk_rewards_t *r, pk_prize_t kind, uint32_t score)
{
    r->event = kind;
    r->event_points = score;
    r->event_ticks = kind == PK_PRIZE_JACKPOT ? 100 : 55;
    ++r->event_seq;
}

static void mission_config(pk_rewards_t *r)
{
    unsigned stage = r->mission_stage ? r->mission_stage : 1u;
    /* Every third stage has a different challenge; difficulty rises slowly. */
    r->mission_goal = (pk_goal_t)((stage - 1u) % 3u);
    if (r->mission_goal == PK_GOAL_PEG) {
        r->mission_target = (uint16_t)(10u + (stage > 10u ? 10u : stage) * 2u);
    } else if (r->mission_goal == PK_GOAL_CENTER) {
        r->mission_target = (uint16_t)(2u + (stage / 6u));
    } else {
        r->mission_target = (uint16_t)(2u + (stage / 6u));
    }
    r->mission_progress = 0;
}

void pk_rewards_init(pk_rewards_t *r)
{
    memset(r, 0, sizeof(*r));
    r->meta.pool = PK_POOL_SEED;
    pk_rewards_new_round(r);
}

void pk_rewards_new_round(pk_rewards_t *r)
{
    const pk_reward_meta_t meta = r->meta;
    memset(r, 0, sizeof(*r));
    r->meta = meta;
    if (r->meta.pool < PK_POOL_SEED) r->meta.pool = PK_POOL_SEED;
    if (r->meta.pool > PK_POOL_CAP) r->meta.pool = PK_POOL_CAP;
    r->mission_stage = 1;
    mission_config(r);
}

void pk_rewards_import(pk_rewards_t *r, const pk_reward_meta_t *meta)
{
    if (!meta) return;
    r->meta = *meta;
    if (r->meta.pool < PK_POOL_SEED) r->meta.pool = PK_POOL_SEED;
    if (r->meta.pool > PK_POOL_CAP) r->meta.pool = PK_POOL_CAP;
}

void pk_rewards_on_shot(pk_rewards_t *r)
{
    /* Visible guaranteed pool growth: every manual fired ball adds 12. */
    r->meta.pool = saturating_add(r->meta.pool, 12u, PK_POOL_CAP);
}

void pk_rewards_tick(pk_rewards_t *r)
{
    if (r->event_ticks) --r->event_ticks;
}

void pk_rewards_add_lifetime(pk_rewards_t *r, uint32_t points)
{
    r->meta.lifetime_score =
        saturating_add(r->meta.lifetime_score, points, UINT32_MAX);
}

pk_reward_result_t pk_rewards_spin(pk_rewards_t *r, uint8_t symbols[3])
{
    pk_reward_result_t result = {0};
    /* Entrance to central slot adds an additional 30 to the progressive pool. */
    r->meta.pool = saturating_add(r->meta.pool, 30u, PK_POOL_CAP);
    ++r->spins;

    /*
     * After 11 consecutive center spins without BIG+, the 12th visibly
     * becomes a guaranteed BIG. No hidden cash odds or payback adjustments.
     */
    if (r->no_big_streak >= PK_PITY_SPINS - 1u) {
        symbols[0] = symbols[1] = symbols[2] = 2;
    }
    const bool triple = symbols[0] == symbols[1] && symbols[1] == symbols[2];
    const bool pair = symbols[0] == symbols[1] ||
                      symbols[0] == symbols[2] ||
                      symbols[1] == symbols[2];
    if (triple) {
        switch (symbols[0]) {
        case 4: { /* 777 jackpot: take entire progressive pot, then reseed. */
            result.prize = PK_PRIZE_JACKPOT;
            result.points = r->meta.pool;
            result.extra_balls = 2;
            result.fever_ticks = 250;
            r->meta.pool = PK_POOL_SEED;
            ++r->meta.total_jackpots;
            r->meta.medals = saturating_add(r->meta.medals, 12u, UINT32_MAX);
            break;
        }
        case 3: { /* SUPER: withdraw one fourth while preserving seed. */
            result.prize = PK_PRIZE_SUPER;
            const uint32_t draw = (r->meta.pool - PK_POOL_SEED) / 4u;
            result.points = 750u + draw;
            r->meta.pool -= draw;
            result.extra_balls = 1;
            result.fever_ticks = 250;
            r->meta.medals = saturating_add(r->meta.medals, 5u, UINT32_MAX);
            break;
        }
        case 2: { /* BIG: withdraw one tenth and grant a bonus ball. */
            result.prize = PK_PRIZE_BIG;
            const uint32_t draw = (r->meta.pool - PK_POOL_SEED) / 10u;
            result.points = 300u + draw;
            r->meta.pool -= draw;
            result.extra_balls = 1;
            r->meta.medals = saturating_add(r->meta.medals, 2u, UINT32_MAX);
            break;
        }
        default:
            result.prize = PK_PRIZE_MINI;
            result.points = 120u;
            r->meta.medals = saturating_add(r->meta.medals, 1u, UINT32_MAX);
            break;
        }
    } else if (pair) {
        result.prize = PK_PRIZE_PAIR;
        result.points = 45u;
    }

    if (result.prize >= PK_PRIZE_BIG) {
        r->no_big_streak = 0;
    } else if (r->no_big_streak < PK_PITY_SPINS - 1u) {
        ++r->no_big_streak;
    }
    if (result.prize != PK_PRIZE_NONE) {
        set_event(r, result.prize, result.points);
    }
    return result;
}

pk_reward_result_t pk_rewards_record(pk_rewards_t *r, pk_goal_t event)
{
    pk_reward_result_t result = {0};
    if (event != r->mission_goal) return result;
    if (r->mission_progress < r->mission_target) ++r->mission_progress;
    if (r->mission_progress < r->mission_target) return result;

    /* Achievement progress gives predictable rewards alongside random reels. */
    result.prize = PK_PRIZE_MISSION;
    result.points = 250u + (uint32_t)r->mission_stage * 100u;
    if (result.points > 1800u) result.points = 1800u;
    result.extra_balls = r->mission_stage % 3u == 0u ? 2u : 0u;
    r->meta.medals = saturating_add(r->meta.medals, 1u, UINT32_MAX);
    if (r->mission_stage < 99u) ++r->mission_stage;
    mission_config(r);
    set_event(r, result.prize, result.points);
    return result;
}
