/* Render the real firmware UI with fixture data; this is not a board test. */
#include "app.h"
#include "lvgl.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
ns_app_t ns;
static uint8_t frame[240*320*4],buffer[240*24*4];
int bsp_battery_soc(void) {return 78;}
const char *ns_auth(uint8_t mode) {(void)mode;return "WPA2/WPA3";}
void ns_mac(char out[18],const uint8_t m[6]) {snprintf(out,18,"%02X:%02X:%02X:%02X:%02X:%02X",m[0],m[1],m[2],m[3],m[4],m[5]);}
static void flush(lv_display_t *d,const lv_area_t *a,uint8_t *pixels) {
    for(int y=a->y1;y<=a->y2;y++) {
        size_t n=(a->x2-a->x1+1)*4;
        memcpy(frame+(y*240+a->x1)*4,pixels+(y-a->y1)*n,n);
    }
    lv_display_flush_ready(d);
}
static void save(const char *folder,int page) {
    char path[512];snprintf(path,sizeof(path),"%s/page-%02d.ppm",folder,page);
    FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n240 320\n255\n");
    for(unsigned i=0;i<240*320;i++) {uint8_t rgb[]={frame[i*4+2],frame[i*4+1],frame[i*4]};fwrite(rgb,1,3,f);}fclose(f);
}
int main(int argc,char **argv) {
    assert(argc==2);lv_init();lv_display_t *d=lv_display_create(240,320);
    lv_display_set_color_format(d,LV_COLOR_FORMAT_XRGB8888);
    lv_display_set_buffers(d,buffer,NULL,sizeof(buffer),LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(d,flush);
    ns.settings=(ns_settings_t){.language=1,.channel=6,.threshold=10,.brightness=80,.history_enabled=1};
    ns.channel_max=11;ns.free_heap=120*1024;
    ns.ap_count=5;for(unsigned i=0;i<5;i++) {
        snprintf(ns.aps[i].ssid,sizeof(ns.aps[i].ssid),"学习实验网络-%u",i+1);
        ns.aps[i].rssi=-40-i*7;ns.aps[i].channel=i*2+1;
    }
    ns.target=ns.aps[0];ns.target_set=true;ns.scan_total=5;
    for(unsigned i=1;i<14;i++){ns.channels[i]=i%4;ns.scores[i]=(i*53)%250;}
    ns.signal_count=60;for(unsigned i=0;i<60;i++)ns.signal[i]=-42-(i%8)*3;
    ns.stats.frames[0]=2314;ns.stats.frames[1]=174;ns.stats.frames[2]=4110;ns.stats.pps=43;
    ns.stats.beacon=1400;ns.stats.probe_req=92;ns.stats.probe_resp=107;ns.stats.deauth=12;
    ns.ble_count=3;for(unsigned i=0;i<3;i++){snprintf(ns.ble[i].name,sizeof(ns.ble[i].name),"实验 BLE 设备 %u",i+1);ns.ble[i].rssi=-38-i*11;}
    snprintf(ns.lan_text,sizeof(ns.lan_text),"IP  192.0.2.10\nMASK 255.255.255.0\nGW  192.0.2.1\nDNS 192.0.2.1\nDHCP client running\nOK: reconnect");
    ns.service_count=1;snprintf(ns.services[0].kind,8,"mDNS");snprintf(ns.services[0].name,97,"Lab Printer._ipp._tcp.local");
    snprintf(ns.services[0].detail,129,"Host: lab-printer.local");ns.services[0].port=631;
    ns.history_count=1;ns.history[0].aps=5;ns.history[0].best=11;ns.history[0].seconds=35;
    ns_ui_init();
    for(int page=0;page<=12;page++) {
        ns.page=page;ns.selected=0;ns.detail=false;ns.home_selected=0;
        snprintf(ns.status,sizeof(ns.status),"Host preview / fixture data");ns_ui_render();
        for(unsigned i=0;i<5;i++){lv_tick_inc(50);lv_timer_handler();}
        lv_refr_now(d);save(argv[1],page);
        lv_mem_monitor_t memory;lv_mem_monitor(&memory);
        assert(memory.free_size>1024);assert(lv_mem_test()==LV_RESULT_OK);
        printf("UI page %d: free LVGL %zu bytes\n",page,memory.free_size);
    }
    ns.page=NS_WIFI;ns.detail=true;ns_ui_render();lv_refr_now(d);save(argv[1],13);
    ns.page=NS_SERVICES;ns.detail=true;ns_ui_render();lv_refr_now(d);save(argv[1],14);
    /* Exercise repeated redraws of the actual page widgets without reallocating screens. */
    for(int i=0;i<500;i++){ns.page=i%13;ns.detail=false;ns_ui_render();lv_tick_inc(25);lv_timer_handler();}
    assert(lv_mem_test()==LV_RESULT_OK);puts("NetSec real UI host render: PASS");
}
