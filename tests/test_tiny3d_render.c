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

    assert(t3d_cube_mesh_valid());
    rigid_die_t dice[6];
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
    for(int i=0;i<6;i++) {
        rigid_die_init(&dice[i],(uint32_t)i+1,(uint8_t)i,1);
        dice[i].pos_q8.x=(i%3-1)*48*256;
        dice[i].pos_q8.z=(i/3?25:-25)*256;
        rigid_die_settle(&dice[i]);
    }
    t3d_render_dice_count(&surface,dice,6);
    /* A complete cube has a solid silhouette. This catches inconsistent
       camera projection/culling even when each face mesh is valid. */
    for (unsigned seed=1; seed<=100; seed++) {
        rigid_die_init(&dice[0], seed*777, 0, 1);
        rigid_die_settle(&dice[0]);
        dice[0].pos_q8=(t3_vec3_t){0, RIGID_DIE_HALF_Q8, 0};
        t3d_render_dice_count(&surface,dice,1);
        uint16_t background=pixels[20*surface.stride+20];
        for (int y=50; y<115; y++) {
            int left=surface.width, right=-1;
            for(int x=60;x<140;x++) {
                uint16_t color=pixels[y*surface.stride+x];
                if ((color>>11)>12 && ((color>>5)&63)>20) {
                    if(x<left)left=x;
                    right=x;
                }
            }
            for(int x=left;x<=right;x++)
                assert(pixels[y*surface.stride+x]!=background);
        }
    }
    uint8_t seen[37]={0};
    for(int i=0;i<37;i++){unsigned n=t3d_roulette_number((uint8_t)i);assert(n<37);assert(!seen[n]);seen[n]=1;}
    t3d_render_roulette(&surface,0,48,60);
    return 0;
}
