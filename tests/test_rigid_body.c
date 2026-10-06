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

    uint8_t fa = rigid_die_top_face(&a);
    uint8_t fb = rigid_die_top_face(&b);
    assert(fa >= 1 && fa <= 6);
    assert(fb >= 1 && fb <= 6);

    rigid_die_t flat = {0};
    flat.orientation = t3_quat_identity();
    assert(rigid_die_top_face(&flat) == 1);
    return 0;
}
