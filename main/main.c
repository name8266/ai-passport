/* Pocket NetSec: a bounded, passive-first diagnostic application. */
#include "netsec/app.h"
#include "bsp_i2c.h"
#include "bsp_display.h"
#include "bsp_battery.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

ns_app_t ns;
static uint64_t last_scan,connect_deadline;
static bool serial_ready;
static void result(esp_err_t e,const char *success) {
    snprintf(ns.status,sizeof(ns.status),"%s",e==ESP_OK?success:esp_err_to_name(e));
}
static void button(bsp_btn_t btn,bsp_btn_ev_t ev,void *user) {
    (void)user;if(ev!=BSP_BTN_CLICK && ev!=BSP_BTN_LONG)return;
    ns_event_t e={.type=NSE_KEY,.key={btn,ev}};(void)xQueueSend(ns.events,&e,0);
}
static bool settings_valid(const ns_settings_t *s) {
    return s->version==1 && s->channel>=1 && s->channel<=13 && s->brightness>=20 &&
        s->brightness<=100 && s->language<=1 && s->region<=2 && s->hop<=1 &&
        s->history_enabled<=1 && s->threshold>=5 && s->threshold<=100 &&
        (s->region || s->channel<=11);
}
static void load(void) {
    ns.settings=(ns_settings_t){.version=1,.channel=1,.hop=0,.brightness=80,
        .language=1,.region=0,.history_enabled=1,.threshold=10};
    nvs_handle_t h;
    if(nvs_open("pocket_netsec",NVS_READONLY,&h)!=ESP_OK)return;
    ns_settings_t s;size_t len=sizeof(s);
    if(nvs_get_blob(h,"settings",&s,&len)==ESP_OK && len==sizeof(s) && settings_valid(&s))ns.settings=s;
    len=sizeof(ns.history);
    if(nvs_get_blob(h,"history",ns.history,&len)==ESP_OK && len==sizeof(ns.history)) {
        uint8_t count=0;(void)nvs_get_u8(h,"hist_count",&count);ns.history_count=count>NS_HISTORY_MAX?0:count;
    }
    nvs_close(h);
}
void ns_persist_settings(void) {
    nvs_handle_t h;esp_err_t e=nvs_open("pocket_netsec",NVS_READWRITE,&h);
    if(e==ESP_OK) {e=nvs_set_blob(h,"settings",&ns.settings,sizeof(ns.settings));if(e==ESP_OK)e=nvs_commit(h);
        nvs_close(h);}
    result(e,"Settings saved");
}
void ns_persist_history(void) {
    nvs_handle_t h;esp_err_t e=nvs_open("pocket_netsec",NVS_READWRITE,&h);
    if(e==ESP_OK) {
        e=nvs_set_blob(h,"history",ns.history,sizeof(ns.history));
        if(e==ESP_OK)e=nvs_set_u8(h,"hist_count",ns.history_count);
        if(e==ESP_OK)e=nvs_commit(h);
        nvs_close(h);
    }
    if(e!=ESP_OK)result(e,"");
}
void ns_go(ns_page_t page) {
    /* Keep a connected LAN for the service-discovery page. All other switches
       stop the previous radio before changing ownership or UI state. */
    bool keep_lan=(ns.page==NS_LAN || ns.page==NS_SERVICES) && (page==NS_LAN || page==NS_SERVICES);
    if(!keep_lan) {
        esp_err_t e=ns_radio_stop();if(e!=ESP_OK) {result(e,"");return;}
        ns.radio_generation++;
    }else ns_discovery_stop();
    if(ns.page==NS_WIFI || ns.page==NS_CHANNELS)ns_persist_history();
    if(ns.page==NS_MONITOR || ns.page==NS_DETECT || ns.page==NS_CAPTURE)ns_persist_settings();
    ns.page=page;ns.selected=0;ns.detail=false;ns.paused=false;ns.detail_page=0;
    snprintf(ns.status,sizeof(ns.status),"Ready");
    esp_err_t e=ESP_OK;
    switch(page) {
    case NS_WIFI:case NS_CHANNELS:
        e=ns_scan_start();last_scan=esp_timer_get_time();break;
    case NS_SIGNAL:
        ns.signal_count=0;
        if(!ns.target_set) {snprintf(ns.status,sizeof(ns.status),"Select AP in scanner first");return;}
        ns_stats_clear();e=ns_sniff_start();break;
    case NS_MONITOR:case NS_DETECT:case NS_CAPTURE:case NS_EAPOL:
        ns_stats_clear();e=ns_sniff_start();break;
    case NS_BLE:e=ns_ble_start();break;
    case NS_LAN:
        if(!ns.connected)e=ns_lan_connect();
        connect_deadline=esp_timer_get_time()+15000000;
        ns_lan_info();break;
    case NS_SERVICES:
        if(ns.connected)ns_discovery_start();else snprintf(ns.status,sizeof(ns.status),"Connect in LAN inspector first");break;
    default:break;
    }
    if(e!=ESP_OK)result(e,"");
}
static int item_count(void) {
    switch(ns.page) {
    case NS_WIFI:return ns.ap_count;
    case NS_BLE:return ns.ble_count;
    case NS_CHANNELS:return ns.channel_max;
    case NS_SERVICES:return ns.service_count;
    case NS_EAPOL:return NS_SESSION_MAX;
    case NS_HISTORY:return ns.history_count;
    case NS_SETTINGS:return 7;
    default:return 0;
    }
}
static void key(bsp_btn_t btn,bsp_btn_ev_t ev) {
    if(ev==BSP_BTN_LONG) {
        if(btn==BSP_BTN_OK) {
            if(ns.detail) {ns.detail=false;return;}
            ns_go(NS_HOME);return;
        }
        if(btn==BSP_BTN_UP && ns.page==NS_CAPTURE)ns_capture_export();
        if(btn==BSP_BTN_DOWN && ns.page==NS_CAPTURE) {ns_capture_enable(false);ns_capture_clear();result(ESP_OK,"Capture cleared");}
        if(btn==BSP_BTN_DOWN && ns.page==NS_HISTORY) {memset(ns.history,0,sizeof(ns.history));ns.history_count=0;ns_persist_history();}
        if(btn==BSP_BTN_UP && ns.page==NS_EAPOL) {ns.target_set=false;ns_go(NS_EAPOL);}
        if(btn==BSP_BTN_UP && ns.page==NS_SERVICES)ns_go(NS_LAN);
        return;
    }
    if(ns.page==NS_HOME) {
        if(btn==BSP_BTN_UP)ns.home_selected=(ns.home_selected+11)%12;
        if(btn==BSP_BTN_DOWN)ns.home_selected=(ns.home_selected+1)%12;
        if(btn==BSP_BTN_OK)ns_go(ns.home_selected+1);
        return;
    }
    if(ns.page==NS_LAN && btn==BSP_BTN_DOWN) {ns_go(NS_SERVICES);return;}
    bool sniff=ns.page==NS_MONITOR || ns.page==NS_DETECT || ns.page==NS_CAPTURE;
    if(sniff) {
        if(btn==BSP_BTN_OK && ns.page==NS_CAPTURE)ns_capture_enable(!ns.capture_on);
        else if(btn==BSP_BTN_OK) {ns.paused=!ns.paused;result(esp_wifi_set_promiscuous(!ns.paused),ns.paused?"Paused":"Observing");}
        else if(ns.wifi_ready) {
            ns.settings.hop=0;
            uint8_t c=btn==BSP_BTN_UP?(ns.settings.channel==1?ns.channel_max:ns.settings.channel-1):ns.settings.channel%ns.channel_max+1;
            result(ns_set_channel(c),"Channel locked");
        }
        return;
    }
    int count=item_count();
    if(btn!=BSP_BTN_OK && count) {
        ns.selected=(ns.selected+(btn==BSP_BTN_UP?count-1:1))%count;return;
    }
    if(btn!=BSP_BTN_OK)return;
    switch(ns.page) {
    case NS_WIFI:
        if(!ns.ap_count) {result(ns_scan_start(),"Scanning...");break;}
        if(!ns.detail)ns.detail=true;
        else {ns.target=ns.aps[ns.selected];ns.target_set=true;result(ESP_OK,"Target selected: signal / EAPOL");}break;
    case NS_CHANNELS:result(ns_scan_start(),"Scanning...");break;
    case NS_SIGNAL:
        ns.paused=!ns.paused;if(ns.wifi_ready)result(esp_wifi_set_promiscuous(!ns.paused),ns.paused?"Paused":"Tracking");break;
    case NS_BLE:
        if(ns.detail)ns.detail_page++;else if(ns.ble_count)ns.detail=true;break;
    case NS_LAN:
        ns_go(NS_HOME);ns_go(NS_LAN);break;
    case NS_SERVICES:
        if(!ns.detail && ns.service_count)ns.detail=true;else {ns.detail=false;ns_discovery_start();}break;
    case NS_EAPOL:ns_stats_clear();result(ESP_OK,"Observation counters reset");break;
    case NS_HISTORY:if(ns.history_count)ns.detail=true;break;
    case NS_SETTINGS:
        switch(ns.selected) {
        case 0:ns.settings.language^=1;break;
        case 1:ns.settings.brightness=ns.settings.brightness==100?20:ns.settings.brightness+20;bsp_display_backlight(ns.settings.brightness);break;
        case 2:ns.settings.channel=ns.settings.channel%ns.channel_max+1;break;
        case 3:ns.settings.hop^=1;break;
        case 4:ns.settings.threshold=ns.settings.threshold>=100?5:ns.settings.threshold+5;break;
        case 5:ns.settings.region=(ns.settings.region+1)%3;ns.channel_max=ns.settings.region?13:11;if(ns.settings.channel>ns.channel_max)ns.settings.channel=1;break;
        case 6:ns.settings.history_enabled^=1;break;
        }
        ns_persist_settings();break;
    default:break;
    }
}
void ns_command(char *line) {
    if(!strcmp(line,"HELP")) {
        puts("NETSEC commands: HELP, STATUS, SCAN, WIFI <ssid>|<password>, CONNECT, FORGET, PCAP, CLEAR, HISTORY, TARGET <index>, PAGE <1-12>");
    } else if(!strcmp(line,"STATUS")) {
        printf("NETSEC page=%d channel=%u aps=%u BLE=%u heap=%lu min=%lu\n",ns.page,ns.settings.channel,(unsigned)ns.ap_count,(unsigned)ns.ble_count,(unsigned long)ns.free_heap,(unsigned long)ns.min_heap);
    } else if(!strcmp(line,"SCAN")) {
        ns_go(NS_WIFI);
    } else if(!strncmp(line,"WIFI ",5)) {
        char *sep=strchr(line+5,'|');
        if(!sep) {puts("NETSEC invalid WIFI format");return;}*sep=0;
        size_t slen=strlen(line+5),plen=strlen(sep+1);
        if(!slen || slen>32 || plen>64 || (plen && plen<8)) {puts("NETSEC invalid SSID/password length");return;}
        nvs_handle_t h;esp_err_t e=nvs_open("pocket_netsec",NVS_READWRITE,&h);
        if(e==ESP_OK) {
            e=nvs_set_str(h,"ssid",line+5);if(e==ESP_OK)e=nvs_set_str(h,"password",sep+1);
            if(e==ESP_OK)e=nvs_commit(h);
            nvs_close(h);
        }
        /* Never echo input or credentials. Plain local USB + plaintext NVS. */
        memset(sep+1,0,plen);result(e,"Network saved; open LAN to connect");puts(e==ESP_OK?"NETSEC network saved":"NETSEC save failed");
    } else if(!strcmp(line,"CONNECT")) {ns_go(NS_LAN);printf("NETSEC %s\n",ns.status);}
    else if(!strcmp(line,"FORGET")) {
        esp_err_t stopped=ns_radio_stop();
        if(stopped!=ESP_OK) {result(stopped,"");puts("NETSEC radio stop failed; retry FORGET");return;}
        ns.radio_generation++;ns.page=NS_HOME;
        nvs_handle_t h;esp_err_t e=nvs_open("pocket_netsec",NVS_READWRITE,&h);
        if(e==ESP_OK) { (void)nvs_erase_key(h,"ssid");(void)nvs_erase_key(h,"password");e=nvs_commit(h);nvs_close(h); }
        result(e,"Saved network removed");printf("NETSEC %s\n",ns.status);
    } else if(!strcmp(line,"PCAP"))ns_capture_export();
    else if(!strcmp(line,"CLEAR")) {ns_capture_enable(false);ns_capture_clear();ns_stats_clear();}
    else if(!strcmp(line,"HISTORY")) {
        for(size_t i=0;i<ns.history_count;i++)printf("HISTORY %u boot=%08lX seconds=%lu aps=%u best=%u\n",(unsigned)i,(unsigned long)ns.history[i].boot,(unsigned long)ns.history[i].seconds,ns.history[i].aps,ns.history[i].best);
    } else if(!strncmp(line,"TARGET ",7)) {
        char *end;long i=strtol(line+7,&end,10);
        if(*end==0 && i>=0 && i<(long)ns.ap_count) {ns.target=ns.aps[i];ns.target_set=true;puts("NETSEC target selected");}
        else puts("NETSEC invalid target index");
    } else if(!strncmp(line,"PAGE ",5)) {
        char *end;long p=strtol(line+5,&end,10);
        if(*end==0 && p>=1 && p<=12) {ns_go(p);printf("NETSEC %s\n",ns.status);}
        else puts("NETSEC invalid page");
    } else puts("NETSEC unknown command; HELP");
}
static void serial_poll(void) {
    static char line[160];static size_t used;static bool overflow;
    if(!serial_ready)return;
    uint8_t bytes[64];int n=usb_serial_jtag_read_bytes(bytes,sizeof(bytes),0);
    for(int i=0;i<n;i++) {
        char c=bytes[i];
        if(c=='\r' || c=='\n') {
            if(used || overflow) {
                line[used]=0;if(!overflow)ns_command(line);else puts("NETSEC line too long");
                memset(line,0,sizeof(line));used=0;overflow=false;
            }
        } else if(c==8 || c==127) {if(used)used--;}
        else if((unsigned char)c>=32) {if(used+1<sizeof(line))line[used++]=c;else overflow=true;}
    }
    memset(bytes,0,sizeof(bytes));
}
static void worker(void *arg) {
    (void)arg;uint64_t rendered=0;
    ns_event_t e;
    for(;;) {
        uint64_t now=esp_timer_get_time();
        if(xQueueReceive(ns.events,&e,pdMS_TO_TICKS(25))==pdTRUE) {
            if(e.type!=NSE_KEY && e.generation!=ns.radio_generation)continue;
            switch(e.type) {
            case NSE_KEY:key(e.key.btn,e.key.ev);break;
            case NSE_SCAN:ns_scan_done();break;
            case NSE_IP:
                if(ns.page==NS_LAN || ns.page==NS_SERVICES) {ns.connected=true;ns.lan_connecting=false;ns_lan_info();result(ESP_OK,"LAN connected");}break;
            case NSE_DISCONNECT:
                if(ns.page==NS_LAN || ns.page==NS_SERVICES) {
                    ns.connected=false;ns.lan_connecting=false;ns_discovery_stop();ns_lan_info();
                    snprintf(ns.status,sizeof(ns.status),"Disconnected reason %d; OK retry",e.code);
                }break;
            case NSE_BLE:if(ns.page==NS_BLE && !ns.ble_stopping)ns_ble_event(&e.ble);break;
            case NSE_BLE_READY:
                if(ns.page==NS_BLE && ns.ble_initialized && !ns.ble_stopping) {
                    ns.ble_ready=true;result(ns_ble_resume(),"Passive BLE scan");
                }break;
            case NSE_BLE_RESET:if(ns.page==NS_BLE) {ns.ble_ready=false;snprintf(ns.status,sizeof(ns.status),"BLE reset %d",e.code);}break;
            }
        }
        serial_poll();now=esp_timer_get_time();
        ns_radio_tick(now);ns_discovery_tick(now);
        if((ns.page==NS_WIFI || ns.page==NS_CHANNELS) && !ns.detail && !ns.scanning && now-last_scan>=6000000) {
            result(ns_scan_start(),"Passive scan...");last_scan=now;
        }
        if(ns.scanning && now-last_scan>=10000000) {
            esp_wifi_scan_stop();esp_wifi_clear_ap_list();ns.scanning=false;result(ESP_ERR_TIMEOUT,"");last_scan=now;
        }
        if(ns.lan_connecting && now>connect_deadline) {
            esp_wifi_disconnect();ns.lan_connecting=false;result(ESP_ERR_TIMEOUT,"");
        }
        if(now-rendered>=250000) {
            ns_stats_read(&ns.stats);ns.free_heap=esp_get_free_heap_size();ns.min_heap=esp_get_minimum_free_heap_size();
            if(bsp_lvgl_lock(50)) {ns_ui_render();bsp_lvgl_unlock();}rendered=now;
        }
    }
}
void app_main(void) {
    ns.boot_id=esp_random();ns.channel_max=11;
    ns.events=xQueueCreate(32,sizeof(ns_event_t));if(!ns.events)return;
    esp_err_t e=nvs_flash_init(); /* No automatic erase of unrelated NVS data. */
    if(e==ESP_OK)load();else ns.settings=(ns_settings_t){.version=1,.channel=1,.brightness=80,.threshold=10,.language=1};
    ns.channel_max=ns.settings.region?13:11;
    if(esp_netif_init()!=ESP_OK || esp_event_loop_create_default()!=ESP_OK)return;
    bsp_i2c_init();(void)bsp_battery_init();
    if(bsp_display_init()!=ESP_OK || !bsp_lvgl_init())return;
    bsp_display_backlight(ns.settings.brightness);
    snprintf(ns.status,sizeof(ns.status),"%s",e==ESP_OK?"Ready":"NVS unavailable; settings not saved");
    if(bsp_lvgl_lock(1000)) {ns_ui_init();bsp_lvgl_unlock();}else return;
    usb_serial_jtag_driver_config_t cfg={.rx_buffer_size=256,.tx_buffer_size=1024};
    serial_ready=usb_serial_jtag_driver_install(&cfg)==ESP_OK;
    if(serial_ready)usb_serial_jtag_vfs_use_driver();
    puts("NETSEC ready. HELP for USB commands; input is not echoed.");
    if(xTaskCreate(worker,"netsec",8192,NULL,4,NULL)!=pdPASS) {
        snprintf(ns.status,sizeof(ns.status),"Worker allocation failed");return;
    }
    e=bsp_button_init(button,NULL);if(e!=ESP_OK)printf("NETSEC buttons unavailable: %s\n",esp_err_to_name(e));
}
