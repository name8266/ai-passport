#include "app.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "esp_wifi_default.h"
#include "nvs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static ns_stats_t stats;
static ns_capture_t captures[NS_CAPTURE_MAX];
static unsigned capture_head, capture_count;
static bool capturing;
static uint16_t alert_threshold = 10;
static uint8_t filter_bssid[6];
static bool filter_enabled;
static uint64_t rate_time, hop_time;
static uint32_t rate_previous;
static esp_event_handler_instance_t wifi_handler, ip_handler;
static bool event_handlers_ready;

void ns_mac(char out[18], const uint8_t m[6]) {
    snprintf(out,18,"%02X:%02X:%02X:%02X:%02X:%02X",m[0],m[1],m[2],m[3],m[4],m[5]);
}
const char *ns_auth(uint8_t a) {
    switch(a) {
    case WIFI_AUTH_OPEN:return "OPEN";
    case WIFI_AUTH_WEP:return "WEP";
    case WIFI_AUTH_WPA_PSK:return "WPA";
    case WIFI_AUTH_WPA2_PSK:return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK:return "WPA/WPA2";
    case WIFI_AUTH_WPA2_ENTERPRISE:return "WPA2-EAP";
    case WIFI_AUTH_WPA3_PSK:return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK:return "WPA2/WPA3";
    case WIFI_AUTH_WAPI_PSK:return "WAPI";
    case WIFI_AUTH_OWE:return "OWE";
    default:return "OTHER";
    }
}
static void event_cb(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg;
    ns_event_t e={.generation=ns.radio_generation}; bool send=true;
    if(base==WIFI_EVENT && id==WIFI_EVENT_SCAN_DONE) e.type=NSE_SCAN;
    else if(base==WIFI_EVENT && id==WIFI_EVENT_STA_DISCONNECTED) {
        e.type=NSE_DISCONNECT; e.code=((wifi_event_sta_disconnected_t*)data)->reason;
    } else if(base==IP_EVENT && id==IP_EVENT_STA_GOT_IP) e.type=NSE_IP;
    else send=false;
    if(send) (void)xQueueSend(ns.events,&e,0);
}
static void receive(void *buf,wifi_promiscuous_pkt_type_t type) {
    if(type==WIFI_PKT_MISC)return;
    wifi_promiscuous_pkt_t *pkt=buf;
    if(pkt->rx_ctrl.rx_state || pkt->rx_ctrl.sig_len<4)return;
    size_t len=pkt->rx_ctrl.sig_len-4; /* FCS excluded from header-only export. */
    ns_frame_t f;
    if(!ns_parse_frame(pkt->payload,len,&f)) {
        portENTER_CRITICAL(&mux);stats.malformed++;portEXIT_CRITICAL(&mux);return;
    }
    if(filter_enabled && (f.type==1 || memcmp(f.bssid,filter_bssid,6)))return;
    uint64_t now=esp_timer_get_time();
    portENTER_CRITICAL(&mux);
    stats.frames[f.type]++;
    if(f.type==0) {
        if(f.subtype==8) { stats.beacon++;stats.signal=pkt->rx_ctrl.rssi; }
        if(f.subtype==4)stats.probe_req++;
        if(f.subtype==5)stats.probe_resp++;
        if(f.subtype==10 || f.subtype==12) {
            if(f.subtype==10)stats.disassoc++;else stats.deauth++;
            stats.reason=f.reason;memcpy(stats.source,pkt->payload+10,6);
            if(ns_burst_add(&stats.burst,now,alert_threshold))stats.alerts++;
        }
    }
    if(f.eapol_message) {
        stats.eapol[f.eapol_message-1]++;
        ns_session_observe(stats.sessions,&f,now);
    }
    if(capturing) {
        ns_capture_t *c=&captures[capture_head];
        c->us=now;c->original=len;c->saved=f.header_len;
        c->channel=pkt->rx_ctrl.channel;c->rssi=pkt->rx_ctrl.rssi;
        memcpy(c->data,pkt->payload,c->saved);
        capture_head=(capture_head+1)%NS_CAPTURE_MAX;
        if(capture_count<NS_CAPTURE_MAX)capture_count++;else stats.overwritten++;
        stats.captured++;
    }
    portEXIT_CRITICAL(&mux);
}
esp_err_t ns_wifi_start(void) {
    if(ns.wifi_ready)return ESP_OK;
    if(ns.ble_initialized) { esp_err_t e=ns_ble_stop();if(e!=ESP_OK)return e; }
    if(!ns.netif) {
        ns.netif=esp_netif_create_default_wifi_sta();
        if(!ns.netif)return ESP_ERR_NO_MEM;
    }
    if(!event_handlers_ready) {
        esp_err_t e=esp_event_handler_instance_register(WIFI_EVENT,ESP_EVENT_ANY_ID,event_cb,NULL,&wifi_handler);
        if(e!=ESP_OK)return e;
        e=esp_event_handler_instance_register(IP_EVENT,IP_EVENT_STA_GOT_IP,event_cb,NULL,&ip_handler);
        if(e!=ESP_OK) { esp_event_handler_instance_unregister(WIFI_EVENT,ESP_EVENT_ANY_ID,wifi_handler);return e; }
        event_handlers_ready=true;
    }
    wifi_init_config_t cfg=WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t e=esp_wifi_init(&cfg);if(e!=ESP_OK)return e;
    if((e=esp_wifi_set_storage(WIFI_STORAGE_RAM))!=ESP_OK)goto fail;
    if((e=esp_wifi_set_mode(WIFI_MODE_STA))!=ESP_OK)goto fail;
    wifi_country_t country={.cc="01",.schan=1,.nchan=11,.policy=WIFI_COUNTRY_POLICY_MANUAL};
    if(ns.settings.region==1) { memcpy(country.cc,"CN",3);country.nchan=13; }
    if(ns.settings.region==2) { memcpy(country.cc,"DE",3);country.nchan=13; }
    ns.channel_max=country.nchan;
    if((e=esp_wifi_set_country(&country))!=ESP_OK)goto fail;
    if((e=esp_wifi_start())!=ESP_OK)goto fail;
    ns.wifi_ready=true;
    /* Sniffer mode requires modem sleep off. This is a diagnostic power tradeoff. */
    (void)esp_wifi_set_ps(WIFI_PS_NONE);
    return ESP_OK;
fail:
    esp_wifi_deinit();return e;
}
esp_err_t ns_radio_stop(void) {
    ns_discovery_stop();
    if(ns.ble_initialized)return ns_ble_stop();
    if(!ns.wifi_ready)return ESP_OK;
    ns_capture_enable(false);
    esp_err_t e=esp_wifi_set_promiscuous(false);if(e!=ESP_OK)return e;
    (void)esp_wifi_scan_stop();ns.scanning=false;
    (void)esp_wifi_disconnect();
    e=esp_wifi_stop();if(e!=ESP_OK)return e;
    e=esp_wifi_deinit();if(e!=ESP_OK)return e;
    ns.wifi_ready=false;ns.connected=false;ns.lan_connecting=false;
    return ESP_OK;
}
esp_err_t ns_scan_start(void) {
    esp_err_t e=ns_wifi_start();if(e!=ESP_OK)return e;
    if(ns.scanning)return ESP_OK;
    (void)esp_wifi_set_promiscuous(false);
    wifi_scan_config_t cfg={.show_hidden=true,.scan_type=WIFI_SCAN_TYPE_PASSIVE,
        .scan_time.passive=120};
    e=esp_wifi_scan_start(&cfg,false);
    if(e==ESP_OK) { ns.scanning=true;snprintf(ns.status,sizeof(ns.status),"Passive scan..."); }
    return e;
}
static int ap_compare(const void *a,const void *b) {
    return ((const ns_ap_t*)b)->rssi-((const ns_ap_t*)a)->rssi;
}
void ns_scan_done(void) {
    if(!ns.scanning || !ns.wifi_ready)return;
    ns.scanning=false;
    uint16_t total=0;
    esp_err_t e=esp_wifi_scan_get_ap_num(&total);
    if(e!=ESP_OK) { snprintf(ns.status,sizeof(ns.status),"Scan: %s",esp_err_to_name(e));return; }
    /* Driver returns its strongest bounded records, then frees the whole list. */
    static wifi_ap_record_t records[NS_AP_MAX];
    uint16_t count=NS_AP_MAX;
    e=esp_wifi_scan_get_ap_records(&count,records);
    if(e!=ESP_OK) { esp_wifi_clear_ap_list();snprintf(ns.status,sizeof(ns.status),"Scan read failed");return; }
    uint8_t channels[NS_AP_MAX];int8_t rssis[NS_AP_MAX];
    uint8_t selected_bssid[6]={0};
    bool preserve=ns.ap_count && ns.selected<(int)ns.ap_count;
    if(preserve)memcpy(selected_bssid,ns.aps[ns.selected].bssid,6);
    ns.ap_count=count;ns.scan_total=total;ns.scan_time=esp_timer_get_time()/1000000;
    for(size_t i=0;i<count;i++) {
        ns_ap_t *a=&ns.aps[i];
        ns_text(a->ssid,sizeof(a->ssid),records[i].ssid,strnlen((char*)records[i].ssid,32));
        memcpy(a->bssid,records[i].bssid,6);a->channel=records[i].primary;
        a->rssi=records[i].rssi;a->auth=records[i].authmode;
        channels[i]=a->channel;rssis[i]=a->rssi;
    }
    qsort(ns.aps,count,sizeof(ns_ap_t),ap_compare);
    bool found=false;
    if(preserve)for(size_t i=0;i<count;i++) {
        if(!memcmp(selected_bssid,ns.aps[i].bssid,6)) {ns.selected=i;found=true;break;}
    }
    if(ns.detail && !found)ns.detail=false;
    ns_channel_scores(channels,rssis,count,ns.channels,ns.scores);
    if(ns.selected>=(int)count)ns.selected=count?count-1:0;
    snprintf(ns.status,sizeof(ns.status),"%u found / %u retained",total,count);
    if(ns.settings.history_enabled) {
        memmove(ns.history+1,ns.history,(NS_HISTORY_MAX-1)*sizeof(ns_history_t));
        ns_history_t *h=&ns.history[0];memset(h,0,sizeof(*h));
        h->boot=ns.boot_id;h->seconds=ns.scan_time;h->aps=total;
        h->strongest=count?ns.aps[0].rssi:-127;h->best=1;
        for(int ch=2;ch<=ns.channel_max;ch++)if(ns.scores[ch]<ns.scores[h->best])h->best=ch;
        memcpy(h->count,ns.channels,sizeof(h->count));
        if(ns.history_count<NS_HISTORY_MAX)ns.history_count++;
        /* Write at most once per minute, plus an explicit save on leaving scanner. */
        static uint32_t saved;
        if(!saved || ns.scan_time-saved>=60) { ns_persist_history();saved=ns.scan_time; }
    }
}
esp_err_t ns_set_channel(uint8_t ch) {
    if(ch<1 || ch>ns.channel_max)return ESP_ERR_INVALID_ARG;
    esp_err_t e=esp_wifi_set_channel(ch,WIFI_SECOND_CHAN_NONE);
    if(e==ESP_OK)ns.settings.channel=ch;
    return e;
}
esp_err_t ns_sniff_start(void) {
    esp_err_t e=ns_wifi_start();if(e!=ESP_OK)return e;
    filter_enabled=ns.target_set && (ns.page==NS_SIGNAL || ns.page==NS_EAPOL);
    if(filter_enabled)memcpy(filter_bssid,ns.target.bssid,6);
    alert_threshold=ns.settings.threshold;
    uint8_t ch=filter_enabled?ns.target.channel:ns.settings.channel;
    if(ch>ns.channel_max) { snprintf(ns.status,sizeof(ns.status),"Channel outside selected region");return ESP_ERR_INVALID_ARG; }
    if((e=ns_set_channel(ch))!=ESP_OK)return e;
    wifi_promiscuous_filter_t f={.filter_mask=WIFI_PROMIS_FILTER_MASK_MGMT|WIFI_PROMIS_FILTER_MASK_DATA|WIFI_PROMIS_FILTER_MASK_CTRL};
    if((e=esp_wifi_set_promiscuous_filter(&f))!=ESP_OK)return e;
    f.filter_mask=WIFI_PROMIS_CTRL_FILTER_MASK_ALL;
    if((e=esp_wifi_set_promiscuous_ctrl_filter(&f))!=ESP_OK)return e;
    if((e=esp_wifi_set_promiscuous_rx_cb(receive))!=ESP_OK)return e;
    e=esp_wifi_set_promiscuous(true);
    rate_time=hop_time=esp_timer_get_time();rate_previous=0;
    return e;
}
void ns_stats_read(ns_stats_t *o) {
    portENTER_CRITICAL(&mux);*o=stats;portEXIT_CRITICAL(&mux);
}
void ns_stats_clear(void) {
    portENTER_CRITICAL(&mux);memset(&stats,0,sizeof(stats));stats.signal=-127;stats.captured=capture_count;portEXIT_CRITICAL(&mux);
    rate_previous=0;rate_time=esp_timer_get_time();
}
void ns_capture_enable(bool on) {
    portENTER_CRITICAL(&mux);capturing=on;portEXIT_CRITICAL(&mux);ns.capture_on=on;
}
void ns_capture_clear(void) {
    portENTER_CRITICAL(&mux);capture_head=capture_count=0;stats.captured=stats.overwritten=0;portEXIT_CRITICAL(&mux);
}
static void hex_line(const uint8_t *p,size_t n) {
    char line[2*(16+NS_SNAPLEN)+16];
    size_t j=0; memcpy(line,"PCAP_DATA ",10);j=10;
    const char *h="0123456789abcdef";
    for(size_t i=0;i<n;i++) { line[j++]=h[p[i]>>4];line[j++]=h[p[i]&15]; }
    line[j]=0;puts(line);
}
void ns_capture_export(void) {
    /* Freeze capture to avoid torn records. Serial framing tolerates ordinary logs. */
    ns_capture_enable(false);
    uint8_t bytes[16+NS_SNAPLEN];ns_pcap_header(bytes);
    printf("PCAP_BEGIN %u\n",capture_count);hex_line(bytes,24);
    unsigned first=(capture_head+NS_CAPTURE_MAX-capture_count)%NS_CAPTURE_MAX;
    for(unsigned i=0;i<capture_count;i++) {
        ns_capture_t c;
        portENTER_CRITICAL(&mux);c=captures[(first+i)%NS_CAPTURE_MAX];portEXIT_CRITICAL(&mux);
        size_t n=ns_pcap_record(bytes,c.us,c.data,c.saved,c.original);hex_line(bytes,n);
        if((i&7)==7)vTaskDelay(pdMS_TO_TICKS(1));
    }
    puts("PCAP_END");fflush(stdout);
    snprintf(ns.status,sizeof(ns.status),"Exported %u header records",capture_count);
}
void ns_radio_tick(uint64_t now) {
    if(!ns.wifi_ready)return;
    if(now-rate_time>=1000000) {
        portENTER_CRITICAL(&mux);
        uint32_t total=stats.frames[0]+stats.frames[1]+stats.frames[2];
        stats.pps=(uint64_t)(total-rate_previous)*1000000/(now-rate_time);
        ns_burst_tick(&stats.burst,now);
        if(ns.page==NS_SIGNAL) {
            if(ns.signal_count==60)memmove(ns.signal,ns.signal+1,59);else ns.signal_count++;
            ns.signal[ns.signal_count-1]=stats.signal;
            stats.signal=-127; /* A missing beacon produces a visible gap. */
        }
        portEXIT_CRITICAL(&mux);
        rate_previous=total;rate_time=now;
    }
    bool sniff=ns.page==NS_MONITOR || ns.page==NS_DETECT || ns.page==NS_CAPTURE;
    if(sniff && !ns.paused && ns.settings.hop && now-hop_time>=500000) {
        (void)ns_set_channel(ns.settings.channel%ns.channel_max+1);hop_time=now;
    }
}
esp_err_t ns_lan_connect(void) {
    esp_err_t e=ns_wifi_start();if(e!=ESP_OK)return e;
    nvs_handle_t h;
    if(nvs_open("pocket_netsec",NVS_READONLY,&h)!=ESP_OK)return ESP_ERR_NOT_FOUND;
    wifi_config_t c={0};char ssid[33]={0},password[65]={0};
    size_t slen=sizeof(ssid),plen=sizeof(password);
    e=nvs_get_str(h,"ssid",ssid,&slen);
    if(e==ESP_OK) {
        esp_err_t pe=nvs_get_str(h,"password",password,&plen);
        if(pe!=ESP_OK && pe!=ESP_ERR_NVS_NOT_FOUND)e=pe;
    }
    nvs_close(h);
    memcpy(c.sta.ssid,ssid,strnlen(ssid,32));
    memcpy(c.sta.password,password,strnlen(password,64));
    memset(password,0,sizeof(password));memset(ssid,0,sizeof(ssid));
    if(e==ESP_OK)e=esp_wifi_set_config(WIFI_IF_STA,&c);
    memset(&c,0,sizeof(c));
    if(e==ESP_OK)e=esp_wifi_connect();
    ns.lan_connecting=e==ESP_OK;
    return e;
}
void ns_lan_info(void) {
    esp_netif_ip_info_t ip;esp_netif_dns_info_t dns={0};esp_netif_dhcp_status_t dhcp=ESP_NETIF_DHCP_INIT;
    if(!ns.connected || esp_netif_get_ip_info(ns.netif,&ip)!=ESP_OK) {
        snprintf(ns.lan_text,sizeof(ns.lan_text),"No LAN connection\nSerial: WIFI <ssid>|<password>\nOK: connect / retry");return;
    }
    (void)esp_netif_get_dns_info(ns.netif,ESP_NETIF_DNS_MAIN,&dns);
    (void)esp_netif_dhcpc_get_status(ns.netif,&dhcp);
    snprintf(ns.lan_text,sizeof(ns.lan_text),"IP  " IPSTR "\nMASK " IPSTR "\nGW  " IPSTR "\nDNS " IPSTR "\nDHCP %s\nOK: reconnect",IP2STR(&ip.ip),IP2STR(&ip.netmask),IP2STR(&ip.gw),IP2STR(&dns.ip.u_addr.ip4),dhcp==ESP_NETIF_DHCP_STARTED?"client running":"stopped");
}
