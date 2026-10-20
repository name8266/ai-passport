#include "lvgl.h"
#include "src/misc/cache/instance/lv_image_cache.h"
#include "brake_render.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint8_t frame[BH_FRAME_BYTES];
static uint16_t draw[240*40],screen[240*320];
static unsigned flushed;
static void flush(lv_display_t *d,const lv_area_t *area,uint8_t *data) {
    int w=area->x2-area->x1+1;
    for(int y=area->y1;y<=area->y2;++y)memcpy(screen+y*240+area->x1,data+(y-area->y1)*w*2,(size_t)w*2);
    ++flushed;lv_display_flush_ready(d);
}
int main(void) {
    lv_init();
    lv_display_t *display=lv_display_create(240,320);
    lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display,draw,NULL,sizeof(draw),LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display,flush);
    static lv_image_dsc_t source;
    source=(lv_image_dsc_t){.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_I8,.w=240,.h=320,.stride=240},
        .data=frame,.data_size=BH_FRAME_BYTES};
    lv_obj_t *image=lv_image_create(lv_screen_active());lv_image_set_src(image,&source);
    bh_view_t v={.page=BH_HOME,.battery=86,.rank=-1};bh_reset(&v.car,0);
    unsigned mismatch=0;
    for(int page=0;page<=BH_HELP;++page) {
        v.page=(bh_page_t)page;bh_render(frame,&v);lv_image_cache_drop(&source);lv_obj_invalidate(image);
        lv_refr_now(display);
        for(unsigned i=0;i<240*320;++i) {
            const uint8_t *p=frame+4*frame[BH_PALETTE_BYTES+i];
            uint16_t expected=((p[2]>>3)<<11)|((p[1]>>2)<<5)|(p[0]>>3);
            if(screen[i]!=expected)++mismatch;
        }
    }
    assert(flushed>=7*8 && mismatch==0);
    lv_mem_monitor_t monitor;lv_mem_monitor(&monitor);
    printf("Actual LVGL 9.5 headless indexed->RGB565, all pages exact pixel match, 24 KiB pool: PASS (free %zu, biggest %zu)\n",monitor.free_size,monitor.free_biggest_size);
    puts("Device tests: NOT RUN");
    lv_deinit();
}
