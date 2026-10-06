#include "motion_core.h"
#include <assert.h>

int main(void)
{
    motion_slot_profile_t a = motion_slot_profile(1, 1, 0);
    motion_slot_profile_t b = motion_slot_profile(2, 1, 0);
    assert(a.stop_ms >= 820 && a.stop_ms <= 1280);
    assert(a.tick_start_ms >= 36 && a.tick_start_ms <= 54);
    assert(a.stop_ms != b.stop_ms || a.velocity_q8 != b.velocity_q8);

    motion_slot_profile_t r0 = motion_slot_profile(99, 1, 0);
    motion_slot_profile_t r2 = motion_slot_profile(99, 1, 2);
    assert(r2.stop_ms > r0.stop_ms);

    motion_die_profile_t d0 = motion_die_profile(123, 0, 1);
    motion_die_profile_t d1 = motion_die_profile(123, 1, 1);
    assert(d0.x_q8 != d1.x_q8);
    assert(d0.restitution >= 132 && d0.restitution <= 188);

    motion_plinko_profile_t p = motion_plinko_profile(456, 1);
    assert(p.gravity_q8 >= 10 && p.gravity_q8 <= 14);
    assert(p.restitution >= 82 && p.restitution <= 126);

    motion_roulette_profile_t q1 = motion_roulette_profile(9, 1);
    motion_roulette_profile_t q2 = motion_roulette_profile(10, 1);
    assert(q1.duration_ms >= 1550 && q1.duration_ms <= 2500);
    assert(q1.duration_ms != q2.duration_ms ||
           q1.ball_velocity_q8 != q2.ball_velocity_q8);
    return 0;
}
