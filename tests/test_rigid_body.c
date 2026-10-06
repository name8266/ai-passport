#include <assert.h>
#include "rigid_body.h"

int main(void)
{
    rigid_die_t a;
    rigid_die_t b;
    rigid_die_init(&a, 0xA11CEu, 0, 1);
    rigid_die_init(&b, 0xB0B0u, 1, 1);

    unsigned contacts = 0;
    for (int i = 0; i < 900; ++i) {
        if (rigid_die_step_60hz(&a) > 0) ++contacts;
        if (rigid_die_step_60hz(&b) > 0) ++contacts;
        (void)rigid_die_pair_step(&a, &b);
    }
    assert(a.pos_q8.y >= 0);
    assert(b.pos_q8.y >= 0);
    assert(contacts > 0);

    rigid_die_t flat = {0};
    flat.orientation = t3_quat_identity();
    assert(rigid_die_top_face(&flat) == 1);

    /* Regression: deterministic seed sweep must naturally settle and must not
     * collapse toward one local axis. This does not claim casino certification;
     * it catches coordinate-frame and orientation bugs in the tiny solver. */
    unsigned faces[7] = {0};
    for (uint32_t seed = 1; seed <= 240; ++seed) {
        rigid_die_t die;
        rigid_die_init(&die, seed, (uint8_t)(seed & 1u), 1);
        int step;
        for (step = 0; step < 360 && !die.sleeping; ++step)
            (void)rigid_die_step_60hz(&die);
        assert(die.sleeping);
        assert(step < 360);
        uint8_t face = rigid_die_top_face(&die);
        assert(face >= 1 && face <= 6);
        ++faces[face];
    }
    for (int face = 1; face <= 6; ++face) {
        assert(faces[face] >= 25);
        assert(faces[face] <= 55);
    }
    return 0;
}
