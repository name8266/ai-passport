/* Renders the actual application widgets at 240x320 with the same 32 KiB LVGL
 * pool. This is a host framebuffer check, not an LCD/SPI/button/audio test. */
#include "battery_ui.h"
#include "lvgl.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint16_t buffer[240*20];
static unsigned char pixels[240*320*3];
static void flush(lv_display_t *display,const lv_area_t *area,uint8_t *data) {
    uint16_t *colors=(uint16_t *)data;
    for(int y=area->y1;y<=area->y2;++y)for(int x=area->x1;x<=area->x2;++x) {
        unsigned color=*colors++,offset=(y*240+x)*3;
        pixels[offset]=((color>>11)&31)*255/31;pixels[offset+1]=((color>>5)&63)*255/63;pixels[offset+2]=(color&31)*255/31;
    }
    lv_display_flush_ready(display);
}
static void save(const char *root,const char *name,const bat_db_t *db,const battery_ui_state_t *ui) {
    battery_ui_render(db,ui);lv_obj_invalidate(lv_screen_active());lv_refr_now(NULL);
    char path[256];snprintf(path,sizeof(path),"%s/device-%s.ppm",root,name);FILE *f=fopen(path,"wb");assert(f);
    fprintf(f,"P6\n240 320\n255\n");assert(fwrite(pixels,1,sizeof(pixels),f)==sizeof(pixels));assert(fclose(f)==0);
}
int main(int argc,char **argv) {
    assert(argc==2);lv_init();lv_display_t *display=lv_display_create(240,320);assert(display);
    lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);lv_display_set_buffers(display,buffer,NULL,sizeof(buffer),LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display,flush);battery_ui_init();
    bat_db_t db;bat_init(&db);db.count=1;db.assets[0]=(bat_asset_t){.id=33,.capacity_mah=2200,.soc=18,.health=98,.cycles=6};
    battery_ui_state_t ui={.writable=true,.device_soc=86,.epoch=1790767800,.timezone=480,.speaker_available=true};
    ui.care.total=140;ui.care.due_count=27;ui.care.pet.xp=70;ui.selected_reminder=(care_reminder_t){.id=33,.reason=CARE_LOW};
    save(argv[1],"home",&db,&ui);ui.pet_boop=2;save(argv[1],"happy",&db,&ui);ui.pet_boop=0;
    ui.view=BAT_VIEW_ASSET;save(argv[1],"asset",&db,&ui);ui.view=BAT_VIEW_ACTIONS;save(argv[1],"actions",&db,&ui);
    ui.view=BAT_VIEW_REMINDERS;save(argv[1],"reminders",&db,&ui);
    ui.view=BAT_VIEW_WEB;ui.web_running=true;strcpy(ui.ssid,"BatteryDesk-ABCD");strcpy(ui.password,"SYNTHETICKEY");save(argv[1],"web",&db,&ui);
    lv_mem_monitor_t memory;lv_mem_monitor(&memory);assert(memory.total_size>24*1024 && memory.free_biggest_size>4096);printf("Screen preview: PASS (240x320; LVGL pool %lu bytes, largest free %lu)\n",(unsigned long)memory.total_size,(unsigned long)memory.free_biggest_size);
    lv_deinit();return 0;
}
