#include "light_core.h"
#include <assert.h>

int main(void)
{
    light_vec3_t l = light_default_direction();
    assert(l.x_q10 < 0);
    assert(l.y_q10 < 0);
    assert(l.z_q10 > 0);

    light_vec3_t toward = { -512, -724, 512 };
    light_vec3_t away = { 512, 724, -512 };
    light_sample_t a = light_sample_surface(toward, 220);
    light_sample_t b = light_sample_surface(away, 220);
    assert(a.diffuse > b.diffuse);
    assert(a.specular >= b.specular);

    uint32_t lit = light_shade_rgb(0x808080, a, 0);
    uint32_t dark = light_shade_rgb(0x808080, b, 0);
    assert(lit != dark);

    light_sample_t s0 = light_sample_spinner(0, 180);
    light_sample_t s1 = light_sample_spinner(1800, 180);
    assert(s0.diffuse != s1.diffuse || s0.specular != s1.specular);

    assert(light_shadow_opacity(0) > light_shadow_opacity(30));
    return 0;
}
