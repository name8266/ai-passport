#include <assert.h>
#include <stdint.h>
#include "tiny3d_render.h"

int main(void)
{
    static uint16_t pixels[T3D_DICE_CANVAS_W * T3D_DICE_CANVAS_H];
    t3d_surface_t surface = {
        .pixels = pixels,
        .width = T3D_DICE_CANVAS_W,
        .height = T3D_DICE_CANVAS_H,
        .stride = T3D_DICE_CANVAS_W,
    };

    rigid_die_t dice[2];
    rigid_die_init(&dice[0], 11, 0, 1);
    rigid_die_init(&dice[1], 22, 1, 1);
    dice[0].pos_q8 = (t3_vec3_t){-28 * 256, 18 * 256, -4 * 256};
    dice[1].pos_q8 = (t3_vec3_t){ 28 * 256, 24 * 256,  5 * 256};

    t3d_render_dice_scene(&surface, dice);

    uint32_t changes = 0;
    uint16_t first = pixels[0];
    for (unsigned i = 1; i < T3D_DICE_CANVAS_W * T3D_DICE_CANVAS_H; ++i) {
        if (pixels[i] != first) ++changes;
    }
    assert(changes > 1000);
    return 0;
}
