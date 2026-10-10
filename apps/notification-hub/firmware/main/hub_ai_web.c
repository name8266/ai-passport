/* Mobile-first control panel served directly by the device setup hotspot. */
#include "hub_ai.h"
#include "hub_form.h"
#include "hub_web_dashboard.h"
#include "hub_app_catalog.h"
#include <inttypes.h>
#include "hub_sound.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG="hub_ai_web";
static httpd_handle_t server;
static char csrf_nonce[20];
static hub_web_dashboard_provider_t dashboard_provider;
/* Several KiB: keep this snapshot off the small HTTP server task stack. */
static hub_web_dashboard_t dashboard_page_snapshot;
static hub_web_screen_t screen_snapshot;

void hub_ai_web_set_dashboard_provider(hub_web_dashboard_provider_t provider) {
    dashboard_provider=provider;
}
static esp_err_t reject(httpd_req_t *req,const char *code,const char *message) {
    httpd_resp_set_status(req,code);
    httpd_resp_set_type(req,"text/plain; charset=utf-8");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    return httpd_resp_sendstr(req,message);
}
static void put(httpd_req_t *r,const char *s) {(void)httpd_resp_sendstr_chunk(r,s);}
static void html_escaped(httpd_req_t *r,const char *s) {
    char buffer[96];size_t used=0;
    for(const char *p=s;*p;p++) {
        const char *replacement=NULL;size_t replacement_len=0;
        switch(*p) {
            case '&':replacement="&amp;";replacement_len=5;break;
            case '<':replacement="&lt;";replacement_len=4;break;
            case '>':replacement="&gt;";replacement_len=4;break;
            case '"':replacement="&quot;";replacement_len=6;break;
            case '\'':replacement="&#39;";replacement_len=5;break;
            default:replacement=p;replacement_len=1;break;
        }
        if(used+replacement_len>sizeof(buffer)) {
            (void)httpd_resp_send_chunk(r,buffer,(ssize_t)used);used=0;
        }
        memcpy(buffer+used,replacement,replacement_len);used+=replacement_len;
    }
    if(used)(void)httpd_resp_send_chunk(r,buffer,(ssize_t)used);
}
static void html_input(httpd_req_t *r,const char *name,const char *label,
                       const char *value,const char *type,const char *placeholder) {
    put(r,"<label for=\"");put(r,name);put(r,"\">");put(r,label);
    put(r,"</label><input id=\"");put(r,name);put(r,"\" name=\"");put(r,name);
    put(r,"\" type=\"");put(r,type);put(r,"\" maxlength=\"190\" value=\"");
    html_escaped(r,value);
    put(r,"\" placeholder=\"");html_escaped(r,placeholder);
    put(r,"\" autocomplete=\"off\">");
}
static void html_check(httpd_req_t *r,const char *name,const char *label,bool checked) {
    put(r,"<label class=\"toggle-row\"><span>");put(r,label);
    put(r,"</span><input class=\"toggle\" type=\"checkbox\" name=\"");put(r,name);
    put(r,"\" value=\"1\"");if(checked)put(r," checked");put(r,"></label>");
}
static void issue_nonce(void) {
    /* One boot-scoped token allows multiple open pages without invalidation. */
    if(csrf_nonce[0]) return;
    uint32_t a=esp_random(),b=esp_random();
    snprintf(csrf_nonce,sizeof(csrf_nonce),"%08lx%08lx",(unsigned long)a,(unsigned long)b);
}
static void page_head(httpd_req_t *req,bool dark,const char *title,const char *subtitle) {
    httpd_resp_set_type(req,"text/html; charset=utf-8");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    httpd_resp_set_hdr(req,"X-Content-Type-Options","nosniff");
    httpd_resp_set_hdr(req,"Content-Security-Policy",
        "default-src 'none'; style-src 'unsafe-inline'; form-action 'self'; base-uri 'none'; frame-ancestors 'none'");
    put(req,"<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">");
    put(req,"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1,viewport-fit=cover\">");
    put(req,"<meta name=\"color-scheme\" content=\"light dark\"><meta name=\"theme-color\" content=\"");
    put(req,dark?"#000000\"><title>Notify Hub</title>":"#f2f2f7\"><title>Notify Hub</title>");
    put(req,"<style>\n"
        ":root{color-scheme:light;--bg:#f2f2f7;--card:#fff;--field:#f2f2f7;--ink:#1c1c1e;--muted:#8e8e93;--line:#e5e5ea;--blue:#007aff;--green:#34c759;--red:#ff3b30;--note:#eaf3ff;--notetext:#285077}\n"
        "*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:16px -apple-system,BlinkMacSystemFont,system-ui,sans-serif;padding:env(safe-area-inset-top) 16px calc(30px + env(safe-area-inset-bottom));-webkit-font-smoothing:antialiased}\n"
        ".wrap{width:100%;max-width:560px;margin:0 auto}.top{padding:20px 4px 16px}.eyebrow{font-size:13px;color:var(--muted);font-weight:600;letter-spacing:.02em}.top h1{font-size:30px;line-height:1.1;letter-spacing:-.5px;margin:7px 0 8px}.sub{color:var(--muted);font-size:14px;line-height:1.45}\n"
        ".notice{margin:4px 0 20px;padding:14px 16px;border-radius:18px;background:var(--note);color:var(--notetext);font-size:13px;line-height:1.5}.section{margin:20px 0}.section-title{font-size:13px;font-weight:650;color:var(--muted);letter-spacing:.03em;margin:0 0 8px 12px;text-transform:uppercase}.card{overflow:hidden;background:var(--card);border-radius:20px;padding:4px 16px;box-shadow:0 1px 2px #00000008}.field{padding:12px 0;border-bottom:1px solid var(--line)}.field:last-child{border-bottom:0}\n"
        "label{display:block;font-size:14px;font-weight:550;margin:0 0 8px}.hint,small{display:block;color:var(--muted);font-size:12px;line-height:1.45;margin-top:6px}input:not([type=checkbox]),select{appearance:none;width:100%;min-height:44px;border:0;border-radius:12px;background:var(--field);color:var(--ink);font-family:inherit;font-size:16px;padding:10px 12px;outline:none}input:focus,select:focus{box-shadow:0 0 0 2px color-mix(in srgb,var(--blue) 36%,transparent)}select{background-image:linear-gradient(45deg,transparent 50%,var(--muted) 50%),linear-gradient(135deg,var(--muted) 50%,transparent 50%);background-position:calc(100% - 17px) 19px,calc(100% - 12px) 19px;background-size:5px 5px;background-repeat:no-repeat}\n"
        ".toggle-row{display:flex;align-items:center;justify-content:space-between;gap:14px;min-height:52px;margin:0}.toggle-row span{font-size:15px;font-weight:500}.toggle{appearance:none;position:relative;width:51px;height:31px;flex:0 0 51px;border-radius:18px;background:#d1d1d6;transition:background .16s}.toggle:checked{background:var(--green)}.toggle:before{content:\"\";position:absolute;width:27px;height:27px;left:2px;top:2px;border-radius:50%;background:#fff;box-shadow:0 1px 3px #0003;transition:transform .16s}.toggle:checked:before{transform:translateX(20px)}\n"
        ".actions{position:sticky;bottom:8px;padding:8px 0 14px;background:linear-gradient(transparent,var(--bg) 20%)}button{width:100%;min-height:50px;border:0;border-radius:15px;background:var(--blue);color:#fff;font-size:16px;font-weight:650}button:active{filter:brightness(.92)}button:disabled{opacity:.45}.danger{border:1px solid color-mix(in srgb,var(--red) 22%,var(--line));padding:15px;border-radius:16px}.danger p{font-size:13px;color:var(--muted);line-height:1.5;margin:0 0 12px}.danger button{background:var(--red)}.foot{text-align:center;color:var(--muted);font-size:12px;padding:16px 4px}.tabs{display:flex;gap:8px;margin:0 0 18px}.tabs a{flex:1;text-align:center;text-decoration:none;color:var(--blue);background:var(--card);border-radius:13px;padding:11px;font-size:14px;font-weight:600}.notice-item{padding:14px 0;border-bottom:1px solid var(--line)}.notice-item:last-child{border:0}.notice-meta{font-size:12px;color:var(--muted);margin-bottom:5px}.notice-title{font-size:16px;font-weight:650;margin-bottom:5px;overflow-wrap:anywhere}.notice-body{font-size:14px;line-height:1.5;white-space:pre-wrap;overflow-wrap:anywhere}.status-grid{display:grid;grid-template-columns:1fr 1fr;gap:8px}.status-chip{background:var(--field);padding:10px 12px;border-radius:12px;font-size:13px;color:var(--muted)}.status-chip b{display:block;color:var(--ink);font-size:14px;margin-bottom:2px}.pager{display:flex;gap:10px;margin:12px 0}.pager a{flex:1;text-align:center;text-decoration:none;color:var(--blue);background:var(--card);border-radius:13px;padding:12px;font-weight:600}\n"
        "@media(prefers-reduced-motion:reduce){*{transition:none!important}}\n"
        );
    if(dark)put(req,"</style></head><body style=\"color-scheme:dark;--bg:#000;--card:#1c1c1e;--field:#2c2c2e;--ink:#f5f5f7;--muted:#98989d;--line:#38383a;--blue:#0a84ff;--green:#30d158;--red:#ff453a;--note:#17283b;--notetext:#dcecff\">");
    else put(req,"</style></head><body>");
    put(req,"<main class=\"wrap\"><header class=\"top\"><div class=\"eyebrow\">NOTIFY HUB</div><h1>");
    html_escaped(req,title);put(req,"</h1><div class=\"sub\">");html_escaped(req,subtitle);put(req,"</div></header>");
    put(req,"<nav class=\"tabs\"><a href=\"/\">通知与摘要</a><a href=\"/settings\">设备设置</a><a href=\"/device\">设备遥控</a></nav>");
    put(req,"<aside class=\"notice\">连接设备热点即可管理；AI 默认关闭。开启后，筛选后的通知内容会发送到你配置的 HTTPS 模型接口。</aside>");
}
static esp_err_t settings_page(httpd_req_t *req) {
    hub_ai_settings_t settings;
    hub_ai_connection_t conn={0};
    if(!hub_ai_read_settings(&settings))return reject(req,"500 Internal Server Error","配置无效，请检查设备设置");
    (void)hub_ai_load_connection(&conn);
    issue_nonce();
    page_head(req,settings.theme==1,"设备设置","网络、显示、提示音、摘要与存储策略");
    put(req,"<form method=\"POST\" action=\"/save\" autocomplete=\"off\"><input type=\"hidden\" name=\"csrf\" value=\"");
    html_escaped(req,csrf_nonce);put(req,"\">");

    put(req,"<section class=\"section\"><h2 class=\"section-title\">外观</h2><div class=\"card\"><div class=\"field\"><label for=\"theme\">显示风格</label><select id=\"theme\" name=\"theme\">");
    put(req,settings.theme==0?"<option value=\"light\" selected>浅色 · iOS</option><option value=\"dark\">深色 · iOS</option>":"<option value=\"light\">浅色 · iOS</option><option value=\"dark\" selected>深色 · iOS</option>");
    put(req,"</select><small>设备无法读取 iPhone 的外观状态；在这里选择与手机搭配的模式。</small></div></div></section>");

    put(req,"<section class=\"section\"><h2 class=\"section-title\">显示屏</h2><div class=\"card\"><div class=\"field\"><label for=\"brightness\">屏幕亮度</label><select id=\"brightness\" name=\"brightness\">");
    static const uint8_t brightness_values[]={20,40,60,80,100};
    for(size_t i=0;i<sizeof(brightness_values)/sizeof(brightness_values[0]);i++) {
        char option[80];uint8_t value=brightness_values[i];
        snprintf(option,sizeof(option),"<option value=\"%u\"%s>%u%%</option>",
                 value,settings.brightness==value?" selected":"",value);put(req,option);
    }
    put(req,"</select><small>保存后设备重启并应用屏幕亮度。</small></div></div></section>");

    put(req,"<section class=\"section\"><h2 class=\"section-title\">通知与声音</h2><div class=\"card\"><div class=\"field\">");
    html_check(req,"sound_enabled","通知到达时播放提示音",settings.sound.enabled);
    put(req,"</div><div class=\"field\"><label for=\"sound_tone\">提示音</label><select id=\"sound_tone\" name=\"sound_tone\">");
    static const char *tones[]={"轻快","柔和","双音"};
    for(unsigned i=0;i<HUB_TONE_COUNT;i++) {
        char option[96];snprintf(option,sizeof(option),"<option value=\"%u\"%s>%s</option>",i,settings.sound.tone==i?" selected":"",tones[i]);put(req,option);
    }
    put(req,"</select></div><div class=\"field\"><label for=\"sound_volume\">音量</label><select id=\"sound_volume\" name=\"sound_volume\">");
    for(unsigned i=10;i<=60;i+=10) {
        char option[80];snprintf(option,sizeof(option),"<option value=\"%u\"%s>%u%%</option>",i,settings.sound.volume==i?" selected":"",i);put(req,option);
    }
    put(req,"</select></div></div></section>");

    put(req,"<section class=\"section\"><h2 class=\"section-title\">网络</h2><div class=\"card\"><div class=\"field\">");
    html_input(req,"ssid","家庭 Wi-Fi 名称",conn.ssid,"text","留空以继续使用当前网络");
    put(req,"</div><div class=\"field\">");
    html_input(req,"wifi_password","Wi-Fi 密码","","password","留空以保留当前密码");
    put(req,"<small>设备设置热点始终保持可用。保存网络或 AI 设置后设备会重启。</small></div></div></section>");

    put(req,"<section class=\"section\"><h2 class=\"section-title\">AI 总结</h2><div class=\"card\"><div class=\"field\">");
    html_check(req,"enabled","允许将筛选后的通知发送给 AI",settings.enabled);
    put(req,"</div><div class=\"field\">");
    html_input(req,"endpoint","HTTPS 接口地址",settings.endpoint,"url","https://…/chat/completions");
    put(req,"</div><div class=\"field\">");
    html_input(req,"model","模型名称",settings.model,"text","模型 ID");
    put(req,"</div><div class=\"field\">");
    html_input(req,"api_key","API Key","","password","留空以保留已保存的密钥");
    put(req,"<small>");put(req,settings.api_key[0]?"密钥已保存，不会回显。":"尚未配置 API Key。");put(req,"</small></div><div class=\"field\">");
    char number[24];snprintf(number,sizeof(number),"%d",settings.interval_minutes);
    html_input(req,"interval","兼容配置（当前每3条新通知自动总结）",number,"number","");
    put(req,"</div><div class=\"field\">");
    snprintf(number,sizeof(number),"%d",settings.max_records);
    html_input(req,"max_records","旧版单批配置（公测17固定3条）",number,"number","");
    put(req,"</div><div class=\"field\">");
    html_input(req,"excluded_apps","排除的应用标识（逗号分隔）",settings.excluded_apps,"text","例如 com.example.app");
    put(req,"</div><div class=\"field\">");
    html_check(req,"redact_sensitive","跳过含验证码、密码等敏感词的通知",settings.redact_sensitive);
    put(req,"</div><div class=\"field\"><label for=\"retention_days\">通知保留期限</label><select id=\"retention_days\" name=\"retention_days\">");
    static const uint16_t retention_values[]={7,30,90,180,365};
    for(size_t i=0;i<sizeof(retention_values)/sizeof(retention_values[0]);i++) {
        char option[80];uint16_t value=retention_values[i];
        snprintf(option,sizeof(option),"<option value=\"%u\"%s>%u 天</option>",
                 value,settings.retention_days==value?" selected":"",value);put(req,option);
    }
    put(req,"</select><small>已汇总的通知会自动回收，未处理通知继续按保留期限清理；摘要保留至已读清除。</small></div></div></section><div class=\"actions\"><button type=\"submit\">保存设置</button></div></form>");

    put(req,"<section class=\"section\"><h2 class=\"section-title\">通知档案</h2><div class=\"danger\"><p>清空此设备的通知历史和 AI 摘要，释放整个 4 MiB 存档区。此操作不能撤销；Wi-Fi、管理员和 AI 配置会保留。</p><form method=\"POST\" action=\"/clear\"><input type=\"hidden\" name=\"csrf\" value=\"");
    html_escaped(req,csrf_nonce);
    put(req,"\"><label for=\"confirm\">输入 CLEAR 确认</label><input id=\"confirm\" name=\"confirm\" maxlength=\"8\" autocomplete=\"off\" required><div style=\"height:10px\"></div><button type=\"submit\">清空通知档案</button></form></div></section>");
    put(req,"<footer class=\"foot\">Notify Hub · 配置保存在设备本地</footer></main></body></html>");
    return httpd_resp_sendstr_chunk(req,NULL);
}
static bool dashboard_cursor(httpd_req_t *req,uint32_t *cursor) {
    *cursor=UINT32_MAX;
    size_t query_len=httpd_req_get_url_query_len(req);
    if(!query_len) return true;
    if(query_len>=64) return false;
    char query[64]={0},value[20]={0};
    if(httpd_req_get_url_query_str(req,query,sizeof(query))!=ESP_OK) return false;
    esp_err_t err=httpd_query_key_value(query,"cursor",value,sizeof(value));
    if(err==ESP_ERR_NOT_FOUND) return true;
    if(err!=ESP_OK || !value[0]) return false;
    char *end=NULL;unsigned long parsed=strtoul(value,&end,10);
    if(!end || *end || parsed>UINT32_MAX) return false;
    *cursor=(uint32_t)parsed;
    return true;
}
static void notification_time(const hub_archive_record_t *record,uint32_t now,
                              char *out,size_t cap) {
    if(record->reserved!=HUB_ARCHIVE_TIME_EPOCH || !record->elapsed_seconds) {
        snprintf(out,cap,"时间未知");return;
    }
    if(now<record->elapsed_seconds) {snprintf(out,cap,"刚刚");return;}
    uint32_t age=now-record->elapsed_seconds;
    if(age<60) snprintf(out,cap,"刚刚");
    else if(age<3600) snprintf(out,cap,"%u 分钟前",(unsigned)(age/60));
    else if(age<86400) snprintf(out,cap,"%u 小时前",(unsigned)(age/3600));
    else snprintf(out,cap,"%u 天前",(unsigned)(age/86400));
}
static esp_err_t dashboard_page(httpd_req_t *req) {
    uint32_t cursor;
    if(!dashboard_cursor(req,&cursor)) return reject(req,"400 Bad Request","分页参数无效");
    memset(&dashboard_page_snapshot,0,sizeof(dashboard_page_snapshot));
    bool loaded=dashboard_provider && dashboard_provider(cursor,&dashboard_page_snapshot);
    issue_nonce();
    bool dark=loaded && dashboard_page_snapshot.theme==1;
    page_head(req,dark,"通知与 AI 摘要","最近收到的通知会显示在这里，按时间倒序排列");

    put(req,"<section class=\"section\"><h2 class=\"section-title\">设备状态</h2><div class=\"card\"><div class=\"status-grid\"><div class=\"status-chip\"><b>");
    put(req,loaded?(dashboard_page_snapshot.phone_connected?"手机已连接":"等待手机") : "状态暂不可用");
    put(req,"</b>蓝牙通知</div><div class=\"status-chip\"><b>");
    put(req,loaded?(dashboard_page_snapshot.wifi_online?"网络已连接":"热点运行中") : "");
    put(req,"</b>Wi‑Fi</div><div class=\"status-chip\"><b>");
    if(!loaded) put(req,"");
    else if(dashboard_page_snapshot.ai_busy) put(req,"正在总结");
    else if(dashboard_page_snapshot.ai_failed) put(req,"最近请求失败");
    else put(req,dashboard_page_snapshot.ai_enabled?"已开启":"已关闭");
    put(req,"</b>AI 总结</div><div class=\"status-chip\"><b>");
    if(!loaded) put(req,"");
    else if(dashboard_page_snapshot.archive_error) put(req,"存档异常");
    else if(dashboard_page_snapshot.archive_full) put(req,"存档已满");
    else put(req,"正常");
    put(req,"</b>本地存档</div></div>");
    if(loaded && dashboard_page_snapshot.archive_dropped)
        put(req,"<small>通知高峰期间有记录未能及时写入；建议减少通知量或稍后检查设备。</small>");
    put(req,"</div></section>");

    put(req,"<section class=\"section\"><h2 class=\"section-title\">最近摘要</h2><div class=\"card\">");
    if(loaded && dashboard_page_snapshot.has_summary) {
        const hub_ai_digest_t *d=&dashboard_page_snapshot.summary;
        if(d->hidden) put(req,"<div class=\"hint\">摘要已隐藏，但仍会参与下一轮汇总；设备长按上键可查看。</div>");
        else {
            put(req,"<div class=\"notice-body\">");
            html_escaped(req,d->summary);put(req,"</div>");
            if(d->task_count)put(req,"<small>待完成事项</small>");
            for(uint8_t i=0;i<d->task_count;i++) {
                put(req,"<div class=\"notice-body\">• ");
                html_escaped(req,d->tasks[i].task);
                if(d->tasks[i].source[0]) {
                    put(req," · ");html_escaped(req,d->tasks[i].source);
                }
                put(req,"</div>");
            }
        }
        put(req,"<div class=\"status-grid\" style=\"padding:12px 0 4px\">");
        put(req,"<form method=\"POST\" action=\"/summary/ack\"><input type=\"hidden\" name=\"csrf\" value=\"");
        html_escaped(req,csrf_nonce);
        put(req,"\"><button type=\"submit\">已读清除</button></form>");
        put(req,"<form method=\"POST\" action=\"/summary/hide\"><input type=\"hidden\" name=\"csrf\" value=\"");
        html_escaped(req,csrf_nonce);
        put(req,"\"><button type=\"submit\"");
        if(d->hidden)put(req," disabled");
        put(req,">关闭显示</button></form></div>");
    } else if(loaded && dashboard_page_snapshot.ai_busy) {
        put(req,"<div class=\"hint\">正在整理最近通知…</div>");
    } else {
        put(req,"<div class=\"hint\">没有待阅读摘要；累计3条新通知后自动汇总。</div>");
    }
    if(loaded && dashboard_page_snapshot.ai_enabled) {
        put(req,"<form method=\"POST\" action=\"/summary/run\" style=\"padding:12px 0 4px\"><input type=\"hidden\" name=\"csrf\" value=\"");
        html_escaped(req,csrf_nonce);put(req,"\"><button type=\"submit\"");
        if(dashboard_page_snapshot.ai_busy) put(req," disabled");
        put(req,">立即总结</button></form>");
    }
    put(req,"</div></section>");

    put(req,"<section class=\"section\"><h2 class=\"section-title\">通知 · 最新在前</h2><div class=\"card\">");
    if(!loaded) put(req,"<div class=\"hint\">暂时无法读取通知存档。请稍后刷新；设备热点和本地设置仍可使用。</div>");
    else if(!dashboard_page_snapshot.record_count) put(req,"<div class=\"hint\">还没有通知。先在 iPhone 蓝牙设置中连接 Notify Hub 并允许共享通知。</div>");
    for(uint8_t i=0;loaded && i<dashboard_page_snapshot.record_count;i++) {
        const hub_archive_record_t *record=&dashboard_page_snapshot.records[i];
        char time_text[32];notification_time(record,dashboard_page_snapshot.now_epoch,time_text,sizeof(time_text));
        put(req,"<article class=\"notice-item\"><div class=\"notice-meta\">");
        html_escaped(req,hub_catalog_category(record->app));put(req," · ");
        html_escaped(req,hub_catalog_display(record->app));put(req," · ");
        html_escaped(req,time_text);put(req,"</div><div class=\"notice-title\">");
        html_escaped(req,record->title[0]?record->title:"通知");put(req,"</div>");
        if(record->body[0]) {put(req,"<div class=\"notice-body\">");html_escaped(req,record->body);put(req,"</div>");}
        put(req,"</article>");
    }
    if(loaded && dashboard_page_snapshot.archive_rows) {
        char count[80];snprintf(count,sizeof(count),"<small>存档序号共 %" PRIu32 " 条 · 保留 %u 天</small>",
            dashboard_page_snapshot.archive_rows,(unsigned)dashboard_page_snapshot.retention_days);put(req,count);
    }
    put(req,"</div>");
    if(loaded && cursor!=UINT32_MAX) put(req,"<nav class=\"pager\"><a href=\"/\">回到最新</a></nav>");
    if(loaded && dashboard_page_snapshot.has_older) {
        char link[96];snprintf(link,sizeof(link),"<nav class=\"pager\"><a href=\"/?cursor=%" PRIu32 "\">更早的通知</a></nav>",dashboard_page_snapshot.next_cursor);put(req,link);
    }
    put(req,"</section><footer class=\"foot\">存档在设备本地自动清理 · Notify Hub</footer></main></body></html>");
    return httpd_resp_sendstr_chunk(req,NULL);
}
static bool read_form(httpd_req_t *req,char **out,size_t *length) {
    if(req->content_len<=0 || req->content_len>3072)return false;
    char *body=calloc(1,(size_t)req->content_len+1);
    if(!body)return false;
    int pos=0;
    while(pos<req->content_len) {
        int got=httpd_req_recv(req,body+pos,req->content_len-pos);
        if(got<=0) {free(body);return false;}
        pos+=got;
    }
    *length=(size_t)req->content_len;
    *out=body;return true;
}
static esp_err_t summary_run(httpd_req_t *req) {
    char *body=NULL;size_t body_len=0;
    if(!read_form(req,&body,&body_len)) return reject(req,"413 Payload Too Large","请求内容无法读取");
    bool valid=hub_form_validate_csrf(body,csrf_nonce);
    memset(body,0,body_len);free(body);
    if(!valid) return reject(req,"422 Unprocessable Entity","页面已过期，请刷新后重试");
    if(!hub_app_request_ai_summary()) return reject(req,"409 Conflict","AI 未开启或设备正忙，请检查设置后重试");
    httpd_resp_set_status(req,"303 See Other");
    httpd_resp_set_type(req,"text/plain; charset=utf-8");
    httpd_resp_set_hdr(req,"Location","/");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    return httpd_resp_sendstr(req,"已提交总结任务");
}
static esp_err_t summary_state_action(httpd_req_t *req) {
    char *body=NULL;size_t len=0;
    if(!read_form(req,&body,&len))
        return reject(req,"413 Payload Too Large","请求无效");
    bool valid=hub_form_validate_csrf(body,csrf_nonce);
    memset(body,0,len);free(body);
    if(!valid)return reject(req,"422 Unprocessable Entity","表单已过期，请刷新");
    bool acknowledge=strcmp(req->uri,"/summary/ack")==0;
    if(!(acknowledge?hub_app_ack_ai_digest():hub_app_hide_ai_digest()))
        return reject(req,"409 Conflict","设备忙，未能保存操作，请重试");
    httpd_resp_set_status(req,"303 See Other");
    httpd_resp_set_hdr(req,"Location","/");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    return httpd_resp_sendstr(req,acknowledge?"正在清除摘要":"正在隐藏摘要");
}
static esp_err_t device_page(httpd_req_t *req) {
    if(!hub_app_screen_snapshot(&screen_snapshot))
        return reject(req,"503 Service Unavailable","设备屏幕忙，请稍后刷新");
    issue_nonce();
    page_head(req,screen_snapshot.dark,"设备遥控","同步屏幕文字，远程操作三个按键");
    put(req,"<section class=\"section\"><h2 class=\"section-title\">当前屏幕文字</h2><div class=\"card\"><div class=\"notice-item\">");
    const char *lines[]={screen_snapshot.heading,screen_snapshot.battery,
        screen_snapshot.status,screen_snapshot.page,screen_snapshot.app,
        screen_snapshot.title,screen_snapshot.body,screen_snapshot.help};
    for(unsigned i=0;i<sizeof(lines)/sizeof(lines[0]);i++) {
        if(!lines[i][0]) continue;
        put(req,"<div class=\"notice-body\">");html_escaped(req,lines[i]);put(req,"</div>");
    }
    put(req,"</div></div><small>这是设备当前文字内容，不是屏幕像素截图。按键与实体按键一致，操作完成后自动更新文字。</small><nav class=\"pager\"><a href=\"/device\">刷新屏幕内容</a></nav></section>");
    put(req,"<form method=\"POST\" action=\"/device/control\"><input type=\"hidden\" name=\"csrf\" value=\"");
    html_escaped(req,csrf_nonce);put(req,"\"><div class=\"status-grid\">");
    const char *labels[]={"上键","下键","确认键","长按上键 · 摘要","长按下键","长按确认 · 返回","试听当前提示音"};
    for(unsigned i=0;i<7;i++) {
        char button[96];snprintf(button,sizeof(button),"<button name=\"action\" value=\"%u\">",i);
        put(req,button);put(req,labels[i]);put(req,"</button>");
    }
    put(req,"</div></form><p class=\"hint\">长按上键进入摘要；摘要页按确认=已读清除，长按确认=仅关闭显示。</p></main></body></html>");
    return httpd_resp_send_chunk(req,NULL,0);
}
static esp_err_t device_control(httpd_req_t *req) {
    char *body=NULL;size_t length=0;unsigned action=0;
    if(!read_form(req,&body,&length)) return reject(req,"413 Payload Too Large","无法读取操作");
    bool valid=hub_form_parse_control(body,csrf_nonce,&action);
    memset(body,0,length);free(body);
    if(!valid) return reject(req,"422 Unprocessable Entity","操作无效，请刷新页面");
    if(action==6) hub_sound_preview();
    else if(!hub_app_remote_button(action))
        return reject(req,"409 Conflict","操作尚未确认完成，请刷新屏幕查看，勿重复提交");
    httpd_resp_set_status(req,"303 See Other");
    httpd_resp_set_hdr(req,"Location","/device");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    return httpd_resp_sendstr(req,"操作已提交");
}
static void reboot(void *unused) {
    (void)unused;vTaskDelay(pdMS_TO_TICKS(1500));esp_restart();
}
static esp_err_t save(httpd_req_t *req) {
    char *body=NULL;
    size_t body_len=0;
    if(!read_form(req,&body,&body_len))return reject(req,"413 Payload Too Large","配置内容过长或无法读取");
    hub_ai_settings_t cfg;
    hub_ai_connection_t wifi={0};
    bool valid=hub_ai_read_settings(&cfg);
    if(valid)(void)hub_ai_load_connection(&wifi);
    if(valid)valid=hub_form_parse(body,csrf_nonce,&cfg,&wifi);
    if(valid)valid=hub_ai_save_settings(&cfg,&wifi);
    memset(&cfg,0,sizeof(cfg));
    memset(body,0,body_len);free(body);
    if(!valid)return reject(req,"422 Unprocessable Entity","设置无效或保存失败，请检查后重试");
    httpd_resp_set_type(req,"text/html; charset=utf-8");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    esp_err_t done=httpd_resp_sendstr(req,"<!doctype html><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width\"><h2>设置已保存</h2><p>设备正在重启。重新连接热点后打开 192.168.4.1。</p>");
    if(xTaskCreate(reboot,"hub_reboot",2048,NULL,3,NULL)!=pdPASS)
        ESP_LOGW(TAG,"Settings saved; reboot manually to apply");
    return done;
}
static void reboot_after_clear(void *unused) {
    (void)unused;
    for(unsigned i=0;i<600;i++) {
        int status=hub_ai_archive_clear_status();
        if(status==2) {vTaskDelay(pdMS_TO_TICKS(800));esp_restart();}
        if(status==3 || status==0) break;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    vTaskDelete(NULL);
}
static esp_err_t clear_archive(httpd_req_t *req) {
    char *body=NULL;
    size_t body_len=0;
    if(!read_form(req,&body,&body_len))return reject(req,"413 Payload Too Large","确认内容无效");
    bool confirmed=hub_form_confirm_archive_clear(body,csrf_nonce);
    memset(body,0,body_len);free(body);
    if(!confirmed)return reject(req,"422 Unprocessable Entity","请返回输入 CLEAR 并重新确认");
    if(!hub_ai_archive_clear_request())return reject(req,"409 Conflict","设备正忙，请稍后重试");
    httpd_resp_set_type(req,"text/html; charset=utf-8");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    esp_err_t done=httpd_resp_sendstr(req,"<!doctype html><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width\"><h2>正在清空通知档案</h2><p>设备完成后将自动重启；本地网络和 API 设置会保留。</p>");
    if(xTaskCreate(reboot_after_clear,"hub_clear_reboot",2048,NULL,3,NULL)!=pdPASS)
        ESP_LOGE(TAG,"Archive clear requested but restart monitor unavailable");
    return done;
}
static esp_err_t clear_status(httpd_req_t *req) {
    int status=hub_ai_archive_clear_status();
    const char *message=status==1?"通知档案正在清空":status==2?"清空完成，设备即将重启":status==3?"清空失败，请查看设备状态":"没有正在执行的清理任务";
    httpd_resp_set_type(req,"text/plain; charset=utf-8");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    return httpd_resp_sendstr(req,message);
}
void hub_ai_web_start(void) {
    if(server)return;
    httpd_config_t cfg=HTTPD_DEFAULT_CONFIG();
    cfg.stack_size=5120;cfg.max_uri_handlers=12;cfg.max_open_sockets=3;cfg.lru_purge_enable=true;
    if(httpd_start(&server,&cfg)!=ESP_OK) {ESP_LOGE(TAG,"Could not start embedded admin");return;}
    httpd_uri_t device={.uri="/device",.method=HTTP_GET,.handler=device_page};
    httpd_uri_t control={.uri="/device/control",.method=HTTP_POST,.handler=device_control};
    httpd_uri_t index={.uri="/",.method=HTTP_GET,.handler=dashboard_page};
    httpd_uri_t admin={.uri="/admin",.method=HTTP_GET,.handler=settings_page};
    httpd_uri_t settings={.uri="/settings",.method=HTTP_GET,.handler=settings_page};
    httpd_uri_t submit={.uri="/save",.method=HTTP_POST,.handler=save};
    httpd_uri_t clear={.uri="/clear",.method=HTTP_POST,.handler=clear_archive};
    httpd_uri_t status={.uri="/clear/status",.method=HTTP_GET,.handler=clear_status};
    httpd_uri_t summary={.uri="/summary/run",.method=HTTP_POST,.handler=summary_run};
    httpd_uri_t summary_ack={.uri="/summary/ack",.method=HTTP_POST,.handler=summary_state_action};
    httpd_uri_t summary_hide={.uri="/summary/hide",.method=HTTP_POST,.handler=summary_state_action};
    if(httpd_register_uri_handler(server,&device)!=ESP_OK ||
       httpd_register_uri_handler(server,&control)!=ESP_OK ||
       httpd_register_uri_handler(server,&index)!=ESP_OK ||
       httpd_register_uri_handler(server,&admin)!=ESP_OK ||
       httpd_register_uri_handler(server,&settings)!=ESP_OK ||
       httpd_register_uri_handler(server,&submit)!=ESP_OK ||
       httpd_register_uri_handler(server,&clear)!=ESP_OK ||
       httpd_register_uri_handler(server,&status)!=ESP_OK ||
       httpd_register_uri_handler(server,&summary)!=ESP_OK ||
        httpd_register_uri_handler(server,&summary_ack)!=ESP_OK ||
        httpd_register_uri_handler(server,&summary_hide)!=ESP_OK) {
        ESP_LOGE(TAG,"Could not register all device admin routes");
        (void)httpd_stop(server);server=NULL;return;
    }
    ESP_LOGI(TAG,"Embedded admin running at http://192.168.4.1/");
}
bool hub_ai_web_pause(void) {
    if(!server)return true;
    if(httpd_stop(server)!=ESP_OK)return false;
    server=NULL;vTaskDelay(pdMS_TO_TICKS(20));return true;
}
