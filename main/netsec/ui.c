#include "app.h"
#include "bsp_display.h"
#include "bsp_battery.h"
#include "lvgl.h"
#include "lwip/inet.h"
#include <stdio.h>
#include <string.h>

LV_FONT_DECLARE(netsec_font_14);
static lv_obj_t *screen,*title,*battery,*status,*footer,*rows[7],*panels[7],*chart;
static lv_obj_t *bars[13],*barlabels[13];
static lv_chart_series_t *series;
static bool used_rows[7];
static const char *english[]={"Wi-Fi scanner","Channel map","Signal finder","Packet monitor",
    "Deauth detector","BLE scanner","LAN inspector","Service discovery","Header captures",
    "EAPOL observer","Scan history","Settings"};
static const char *chinese[]={"Wi-Fi 扫描","信道占用","信号追踪","帧监视器","断网帧检测",
    "BLE 扫描","局域网信息","服务发现","帧头抓包","握手流程观察","扫描历史","设置"};
static const char *tr(const char *en,const char *zh) {return ns.settings.language?zh:en;}
static lv_obj_t *label(lv_obj_t *parent,int x,int y,int w) {
    lv_obj_t *o=lv_label_create(parent);lv_obj_set_pos(o,x,y);lv_obj_set_width(o,w);
    lv_obj_set_style_text_font(o,&netsec_font_14,0);
    lv_obj_set_style_text_color(o,lv_color_hex(0xe9e9de),0);
    lv_label_set_long_mode(o,LV_LABEL_LONG_DOT);lv_label_set_text(o,"");return o;
}
void ns_ui_init(void) {
    screen=lv_obj_create(NULL);lv_obj_set_style_bg_color(screen,lv_color_hex(0x13181b),0);
    lv_obj_set_style_text_font(screen,&netsec_font_14,0);lv_obj_remove_flag(screen,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *brand=label(screen,20,14,156);lv_label_set_text(brand,"POCKET NETSEC");
    lv_obj_set_style_text_color(brand,lv_color_hex(0xff9838),0);
    battery=label(screen,177,14,45);title=label(screen,18,47,210);
    for(int i=0;i<7;i++) {
        panels[i]=lv_obj_create(screen);lv_obj_set_pos(panels[i],12,77+i*26);
        lv_obj_set_size(panels[i],216,24);lv_obj_set_style_pad_all(panels[i],0,0);
        lv_obj_set_style_radius(panels[i],3,0);lv_obj_set_style_border_width(panels[i],0,0);
        lv_obj_remove_flag(panels[i],LV_OBJ_FLAG_SCROLLABLE);
        rows[i]=label(panels[i],6,-2,204);
    }
    status=label(screen,16,265,208);lv_obj_set_height(status,20);
    lv_obj_set_style_text_color(status,lv_color_hex(0x87b3a8),0);
    footer=label(screen,21,289,201);lv_obj_set_style_text_color(footer,lv_color_hex(0xff9838),0);
    chart=lv_chart_create(screen);lv_obj_set_pos(chart,18,151);lv_obj_set_size(chart,204,108);
    lv_chart_set_type(chart,LV_CHART_TYPE_LINE);lv_chart_set_point_count(chart,60);
    lv_chart_set_range(chart,LV_CHART_AXIS_PRIMARY_Y,0,80);
    lv_obj_set_style_bg_color(chart,lv_color_hex(0x1e282b),0);
    lv_obj_set_style_size(chart,0,0,LV_PART_INDICATOR);
    series=lv_chart_add_series(chart,lv_color_hex(0xff9838),LV_CHART_AXIS_PRIMARY_Y);
    lv_obj_add_flag(chart,LV_OBJ_FLAG_HIDDEN);
    for(int i=0;i<13;i++) {
        bars[i]=lv_obj_create(screen);lv_obj_set_size(bars[i],11,1);
        lv_obj_set_style_bg_color(bars[i],lv_color_hex(0xff9838),0);
        lv_obj_set_style_border_width(bars[i],0,0);lv_obj_set_style_radius(bars[i],1,0);
        lv_obj_remove_flag(bars[i],LV_OBJ_FLAG_SCROLLABLE);
        barlabels[i]=label(screen,15+i*16,243,16);lv_label_set_text_fmt(barlabels[i],"%d",i+1);
        lv_obj_add_flag(bars[i],LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(barlabels[i],LV_OBJ_FLAG_HIDDEN);
    }
    /* Verify representative Chinese and negative coverage in the actual linked font. */
    lv_font_glyph_dsc_t glyph={0};
    bool han=lv_font_get_glyph_dsc(&netsec_font_14,&glyph,0x4e2d,0) && !glyph.is_placeholder;
    printf("NETSEC_FONT U+4E2D %s\n",han?"PASS":"FAIL");
    lv_screen_load(screen);
}
static void row(int i,const char *text,bool selected) {
    used_rows[i]=true;
    if(strcmp(lv_label_get_text(rows[i]),text))lv_label_set_text(rows[i],text);
    lv_obj_set_style_bg_color(panels[i],lv_color_hex(selected?0xff9838:0x13181b),0);
    lv_obj_set_style_text_color(rows[i],lv_color_hex(selected?0x13181b:0xe9e9de),0);
}
static void hex_adv(char *out,size_t cap,const uint8_t *data,size_t n) {
    size_t j=0;for(size_t i=0;i<n && j+3<cap;i++)j+=snprintf(out+j,cap-j,"%02X ",data[i]);out[j]=0;
}
void ns_ui_render(void) {
    char text[256],mac[18],other[18];
    memset(used_rows,0,sizeof(used_rows));
    const char *const *names=ns.settings.language?chinese:english;
    lv_label_set_text(title,ns.page==NS_HOME?tr("Observe. Learn. Diagnose.","观察 / 学习 / 诊断"):names[ns.page-1]);
    static int bat=-1;static unsigned calls;
    if((calls++%20)==0)bat=bsp_battery_soc();
    if(bat<0)lv_label_set_text(battery,"--%");else lv_label_set_text_fmt(battery,"%d%%",bat);
    lv_label_set_text(status,ns.status);
    lv_label_set_text(footer,tr("UP/DN  OK   hold OK: back","上下选择 确认 长按返回"));
    for(int i=0;i<7;i++) {
        bool scroll=ns.detail && ((ns.page==NS_SERVICES && (i==1 || i==2)) ||
            ((ns.page==NS_WIFI || ns.page==NS_BLE) && i==0));
        lv_label_set_long_mode(rows[i],scroll?LV_LABEL_LONG_SCROLL_CIRCULAR:LV_LABEL_LONG_DOT);
        lv_obj_set_style_bg_color(panels[i],lv_color_hex(0x13181b),0);
        lv_obj_set_style_text_color(rows[i],lv_color_hex(0xe9e9de),0);
        lv_obj_remove_flag(panels[i],LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_add_flag(chart,LV_OBJ_FLAG_HIDDEN);
    for(int i=0;i<13;i++) {lv_obj_add_flag(bars[i],LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(barlabels[i],LV_OBJ_FLAG_HIDDEN);}
    switch(ns.page) {
    case NS_HOME: {
        int first=(ns.home_selected/7)*7;
        for(int i=0;i<7 && first+i<12;i++) {snprintf(text,sizeof(text),"%02d  %s",first+i+1,names[first+i]);row(i,text,first+i==ns.home_selected);}
        lv_label_set_text_fmt(status,"ESP32-C3 | RAM %luK",(unsigned long)(ns.free_heap/1024));break;
    }
    case NS_WIFI:
        if(ns.detail && ns.ap_count) {
            ns_ap_t *a=&ns.aps[ns.selected];ns_mac(mac,a->bssid);
            row(0,a->ssid[0]?a->ssid:tr("Hidden SSID","隐藏网络"),false);row(1,mac,false);
            snprintf(text,sizeof(text),"CH %u    RSSI %d dBm",a->channel,a->rssi);row(2,text,false);
            snprintf(text,sizeof(text),"Security: %s",ns_auth(a->auth));row(3,text,false);
            row(4,tr("OK: select for signal/EAPOL","确认设为信号和握手目标"),true);
            row(5,tr("UP/DN: previous / next AP","上下切换网络"),false);
            row(6,tr("hold OK: scanner list","长按确认返回列表"),false);
        } else {
            int first=ns.selected/7*7;
            for(int i=0;i<7 && first+i<(int)ns.ap_count;i++) {
                ns_ap_t *a=&ns.aps[first+i];snprintf(text,sizeof(text),"%d CH%02u %s",a->rssi,a->channel,a->ssid[0]?a->ssid:"<hidden>");row(i,text,first+i==ns.selected);
            }
            if(!ns.ap_count)row(1,tr("OK: passive scan","确认开始被动扫描"),false);
        }break;
    case NS_CHANNELS: {
        unsigned best=1;uint32_t max=1;
        for(int ch=1;ch<=ns.channel_max;ch++) {if(ns.scores[ch]>max)max=ns.scores[ch];if(ns.scores[ch]<ns.scores[best])best=ch;}
        snprintf(text,sizeof(text),"%lu APs | lower score: CH%u",(unsigned long)ns.scan_total,best);row(0,text,false);
        snprintf(text,sizeof(text),"CH%u AP:%u overlap:%lu",ns.selected+1,ns.channels[ns.selected+1],(unsigned long)ns.scores[ns.selected+1]);row(1,text,true);
        row(2,tr("AP estimate, not RF utilization","AP 扫描估算 非频谱利用率"),false);
        for(int i=3;i<7;i++)lv_obj_add_flag(panels[i],LV_OBJ_FLAG_HIDDEN);
        for(int i=0;i<ns.channel_max;i++) {
            int h=ns.scores[i+1]*77/max;if(h<1)h=1;
            lv_obj_set_pos(bars[i],17+i*16,238-h);lv_obj_set_height(bars[i],h);
            lv_obj_set_style_bg_color(bars[i],lv_color_hex(i==ns.selected?0x76d8b2:0xff9838),0);
            lv_obj_remove_flag(bars[i],LV_OBJ_FLAG_HIDDEN);lv_obj_remove_flag(barlabels[i],LV_OBJ_FLAG_HIDDEN);
        }break;
    }
    case NS_SIGNAL:
        row(0,ns.target_set?ns.target.ssid:tr("Select AP in Wi-Fi scanner","先在扫描详情选择目标"),false);
        if(ns.signal_count)snprintf(text,sizeof(text),"%d dBm | CH %u | 1s/sample",ns.signal[ns.signal_count-1],ns.settings.channel);
        else snprintf(text,sizeof(text),"Waiting for target beacons...");
        row(1,text,false);
        row(2,tr("-100 bottom / -20 top","下限 -100 上限 -20 dBm"),false);
        for(int i=3;i<7;i++)lv_obj_add_flag(panels[i],LV_OBJ_FLAG_HIDDEN);
        if(ns.target_set) {
            lv_obj_remove_flag(chart,LV_OBJ_FLAG_HIDDEN);
            for(unsigned i=0;i<60;i++) {
                int v= i<ns.signal_count?ns.signal[i]+100:LV_CHART_POINT_NONE;
                if(i<ns.signal_count && ns.signal[i]==-127)v=LV_CHART_POINT_NONE;
                if(v!=LV_CHART_POINT_NONE) {if(v<0)v=0;if(v>80)v=80;}
                lv_chart_set_value_by_id(chart,series,i,v);
            }
            lv_chart_refresh(chart);
        }break;
    case NS_MONITOR:case NS_CAPTURE:case NS_DETECT:
        snprintf(text,sizeof(text),"CH%u  %s  %lu fps",ns.settings.channel,ns.settings.hop?"HOP":"LOCK",(unsigned long)ns.stats.pps);row(0,text,true);
        snprintf(text,sizeof(text),"MGMT %lu  DATA %lu",(unsigned long)ns.stats.frames[0],(unsigned long)ns.stats.frames[2]);row(1,text,false);
        snprintf(text,sizeof(text),"CTRL %lu Beacon %lu",(unsigned long)ns.stats.frames[1],(unsigned long)ns.stats.beacon);row(2,text,false);
        snprintf(text,sizeof(text),"Probe Req %lu / Resp %lu",(unsigned long)ns.stats.probe_req,(unsigned long)ns.stats.probe_resp);row(3,text,false);
        if(ns.page==NS_DETECT) {
            snprintf(text,sizeof(text),"Deauth %lu Disassoc %lu",(unsigned long)ns.stats.deauth,(unsigned long)ns.stats.disassoc);row(4,text,false);
            snprintf(text,sizeof(text),"Alerts %lu | threshold %u/s",(unsigned long)ns.stats.alerts,ns.settings.threshold);row(5,text,ns.stats.burst.alarm);
            ns_mac(mac,ns.stats.source);snprintf(text,sizeof(text),"%s reason %u",mac,ns.stats.reason);row(6,text,false);
        } else if(ns.page==NS_CAPTURE) {
            snprintf(text,sizeof(text),"%s saved %lu / ring 64",ns.capture_on?"REC":"STOP",(unsigned long)ns.stats.captured);row(4,text,ns.capture_on);
            snprintf(text,sizeof(text),"Overwrites %lu | headers only",(unsigned long)ns.stats.overwritten);row(5,text,false);
            row(6,tr("OK: record  UP hold: export","确认录制 长按上导出"),false);
        } else {
            snprintf(text,sizeof(text),"Deauth %lu / Disassoc %lu",(unsigned long)ns.stats.deauth,(unsigned long)ns.stats.disassoc);row(4,text,false);
            snprintf(text,sizeof(text),"Rejected %lu",(unsigned long)ns.stats.malformed);row(5,text,false);
            row(6,tr("OK: pause | UP/DN: channel","确认暂停 上下切换信道"),false);
        }break;
    case NS_BLE:
        if(ns.detail && ns.ble_count) {
            ns_ble_t *b=&ns.ble[ns.selected];uint8_t m[6];for(int i=0;i<6;i++)m[i]=b->mac[5-i];ns_mac(mac,m);
            row(0,b->name[0]?b->name:tr("Unnamed advertiser","无名称广播"),false);row(1,mac,false);
            snprintf(text,sizeof(text),"RSSI %d | %s addr",b->rssi,b->addr_type?"random":"public");row(2,text,false);
            snprintf(text,sizeof(text),"Legacy ADV type %u | %u bytes",b->event_type,b->len);row(3,text,false);
            if(ns.detail_page%3==2) {
                ns_ble_meta_t meta;ns_ble_meta(b->data,b->len,&meta);
                snprintf(text,sizeof(text),"Flags %s%02X | TX %s%d",meta.has_flags?"":"n/a ",meta.flags,meta.has_tx?"":"n/a ",meta.tx);row(4,text,false);
                snprintf(text,sizeof(text),"Company %s%04X UUID %s%04X",meta.has_company?"":"-",meta.company,meta.has_uuid?"":"-",meta.uuid);row(5,text,false);
            } else {
                unsigned start=(ns.detail_page%3)*16;size_t n=b->len>start?b->len-start:0;if(n>16)n=16;
                hex_adv(text,sizeof(text),b->data+start,n>8?8:n);row(4,text,false);
                if(n>8)hex_adv(text,sizeof(text),b->data+start+8,n-8);else text[0]=0;row(5,text,false);
            }
            row(6,tr("OK: next ADV bytes page","确认切换广播数据页"),true);
        } else {
            int first=ns.selected/7*7;
            for(int i=0;i<7 && first+i<(int)ns.ble_count;i++) {
                ns_ble_t *b=&ns.ble[first+i];snprintf(text,sizeof(text),"%d %s",b->rssi,b->name[0]?b->name:"<no name>");row(i,text,first+i==ns.selected);
            }
            if(!ns.ble_count)row(1,tr("Passive scan in progress...","正在被动扫描广播..."),false);
            lv_label_set_text_fmt(status,"%u devices | drops %lu",(unsigned)ns.ble_count,(unsigned long)__atomic_load_n(&ns.ble_dropped,__ATOMIC_RELAXED));
        }break;
    case NS_LAN: {
        char info[256];snprintf(info,sizeof(info),"%s",ns.lan_text);char *save=NULL,*p=strtok_r(info,"\n",&save);
        for(int i=0;i<7 && p;i++,p=strtok_r(NULL,"\n",&save))row(i,p,false);
        row(6,tr("DOWN: service discovery","按下键进入服务发现"),true);
        break;
    }
    case NS_SERVICES:
        if(ns.detail && ns.service_count) {
            ns_service_t *s=&ns.services[ns.selected];row(0,s->kind,false);row(1,s->name,false);
            char detail[129];snprintf(detail,sizeof(detail),"%s",s->detail);
            /* A dedicated detail page scrolls full long metadata vertically. */
            lv_label_set_long_mode(rows[1],LV_LABEL_LONG_SCROLL_CIRCULAR);
            row(2,detail,false);lv_label_set_long_mode(rows[2],LV_LABEL_LONG_SCROLL_CIRCULAR);
            struct in_addr ip={.s_addr=s->ip};snprintf(text,sizeof(text),"%s : %u",inet_ntoa(ip),s->port);row(3,text,false);
            row(5,tr("Advertised record, not verified","广播记录 未验证服务内容"),false);
            row(6,tr("OK: refresh discovery","确认重新发现"),true);
        } else {
            int first=ns.selected/7*7;
            for(int i=0;i<7 && first+i<(int)ns.service_count;i++) {ns_service_t *s=&ns.services[first+i];snprintf(text,sizeof(text),"%s %s",s->kind,s->name);row(i,text,first+i==ns.selected);}
            if(!ns.service_count)row(1,tr("OK: discover on connected LAN","确认发现已连接 LAN 服务"),false);
        }break;
    case NS_EAPOL: {
        row(0,ns.target_set?ns.target.ssid:tr("All pairs on locked channel","固定信道上的所有会话"),false);
        snprintf(text,sizeof(text),"CH%u M1:%lu M2:%lu",ns.settings.channel,(unsigned long)ns.stats.eapol[0],(unsigned long)ns.stats.eapol[1]);row(1,text,false);
        snprintf(text,sizeof(text),"M3:%lu M4:%lu (observed)",(unsigned long)ns.stats.eapol[2],(unsigned long)ns.stats.eapol[3]);row(2,text,false);
        ns_session_t *s=&ns.stats.sessions[ns.selected%NS_SESSION_MAX];
        if(s->used) {
            ns_mac(mac,s->bssid);ns_mac(other,s->station);row(3,mac,false);row(4,other,false);
            snprintf(text,sizeof(text),"%s %s %s %s | repeat %lu",s->seen&1?"M1":"--",s->seen&2?"M2":"--",s->seen&4?"M3":"--",s->seen&8?"M4":"--",(unsigned long)s->repeats);row(5,text,true);
        }else row(4,tr("Waiting for a normal reconnect","等待自有网络正常重连"),false);
        row(6,tr("Stages only; no keys stored","只观察阶段 不保存密钥"),false);break;
    }
    case NS_HISTORY:
        if(ns.detail && ns.history_count) {
            ns_history_t *h=&ns.history[ns.selected];
            snprintf(text,sizeof(text),"Boot %08lX +%lus",(unsigned long)h->boot,(unsigned long)h->seconds);row(0,text,false);
            snprintf(text,sizeof(text),"%u APs strongest %d dBm",h->aps,h->strongest);row(1,text,false);
            for(int i=0;i<5;i++) {
                int a=i*3+1;
                if(a==13)snprintf(text,sizeof(text),"CH13:%u | low score CH%u",h->count[13],h->best);
                else snprintf(text,sizeof(text),"CH%02d:%u CH%02d:%u CH%02d:%u",a,h->count[a],a+1,h->count[a+1],a+2,h->count[a+2]);
                row(i+2,text,false);
            }
        } else {
            int first=ns.selected/7*7;
            for(int i=0;i<7 && first+i<(int)ns.history_count;i++) {ns_history_t *h=&ns.history[first+i];snprintf(text,sizeof(text),"+%lus %u APs CH%u",(unsigned long)h->seconds,h->aps,h->best);row(i,text,first+i==ns.selected);}
            if(!ns.history_count)row(1,tr("No saved scans","暂无扫描记录"),false);
        }break;
    case NS_SETTINGS: {
        const char *regions[]={"WORLD 1-11","CN 1-13","EU 1-13"};
        snprintf(text,sizeof(text),"%s: %s",tr("Language","语言"),ns.settings.language?"中文":"English");row(0,text,ns.selected==0);
        snprintf(text,sizeof(text),"%s: %u%%",tr("Brightness","亮度"),ns.settings.brightness);row(1,text,ns.selected==1);
        snprintf(text,sizeof(text),"%s: CH %u",tr("Channel","信道"),ns.settings.channel);row(2,text,ns.selected==2);
        snprintf(text,sizeof(text),"%s: %s",tr("Hopping","跳频"),ns.settings.hop?"ON":"OFF");row(3,text,ns.selected==3);
        snprintf(text,sizeof(text),"%s: %u/s",tr("Alert","告警阈值"),ns.settings.threshold);row(4,text,ns.selected==4);
        snprintf(text,sizeof(text),"%s: %s",tr("Region","地区"),regions[ns.settings.region]);row(5,text,ns.selected==5);
        snprintf(text,sizeof(text),"%s: %s",tr("Save history","保存历史"),ns.settings.history_enabled?"ON":"OFF");row(6,text,ns.selected==6);break;
    }
    }
    for(int i=0;i<7;i++)if(!used_rows[i])row(i,"",false);
}
