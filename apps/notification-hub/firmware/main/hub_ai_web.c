/*
 * Self-contained Passport web administration at http://192.168.4.1/.
 * WPA2 SoftAP + generated admin credentials displayed physically on
 * Passport; browser Basic Auth + one-use CSRF nonce. No external web server.
 *
 * Prototype security: HTTP admin has no application-layer TLS. Do not expose
 * port 80 to the public Internet. NVS secrets are plaintext without full
 * Flash encryption; production demands further hardening.
 */
#include "hub_ai.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/base64.h"

static const char *TAG="hub_ai_web";
static httpd_handle_t server;
static char csrf_nonce[20];

static bool constant_equals(const char *a,const char *b) {
    size_t al=strlen(a),bl=strlen(b),max=al>bl?al:bl;
    unsigned int diff=(unsigned int)(al^bl);
    for(size_t i=0;i<max;i++) {
        uint8_t ac=i<al?(uint8_t)a[i]:0;
        uint8_t bc=i<bl?(uint8_t)b[i]:0;
        diff|=ac^bc;
    }
    return diff==0;
}
static bool is_authorized(httpd_req_t *req) {
    char admin[17];
    if(!hub_ai_get_access(NULL,0,NULL,0,admin,sizeof(admin))) return false;
    char basic[40];
    snprintf(basic,sizeof(basic),"admin:%s",admin);
    unsigned char encoded[80];
    size_t n=0;
    if(mbedtls_base64_encode(encoded,sizeof(encoded),&n,
                             (const unsigned char *)basic,strlen(basic))!=0)
        return false;
    encoded[n]=0;
    char expected[96];
    snprintf(expected,sizeof(expected),"Basic %s",encoded);
    size_t len=httpd_req_get_hdr_value_len(req,"Authorization");
    if(len==0 || len>=sizeof(expected)) return false;
    char provided[96]={0};
    if(httpd_req_get_hdr_value_str(req,"Authorization",
                                  provided,sizeof(provided))!=ESP_OK)return false;
    return constant_equals(provided,expected);
}
static esp_err_t reject(httpd_req_t *req,const char *code,const char *message) {
    httpd_resp_set_status(req,code);
    httpd_resp_set_type(req,"text/plain; charset=utf-8");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    return httpd_resp_sendstr(req,message);
}
static bool authenticate(httpd_req_t *req) {
    if(is_authorized(req)) return true;
    httpd_resp_set_hdr(req,"WWW-Authenticate","Basic realm=\"Passport AI\"");
    (void)reject(req,"401 Unauthorized","Device administrator login required");
    return false;
}
static void put(httpd_req_t *r,const char *s) {
    (void)httpd_resp_sendstr_chunk(r,s);
}
static void html_escaped(httpd_req_t *r,const char *s) {
    for(const char *p=s;*p;p++) {
        switch(*p) {
            case '&':put(r,"&amp;");break;
            case '<':put(r,"&lt;");break;
            case '>':put(r,"&gt;");break;
            case '"':put(r,"&quot;");break;
            case '\'':put(r,"&#39;");break;
            default:{
                char c[2]={*p,0};
                put(r,c);
            }break;
        }
    }
}
static void html_input(httpd_req_t *r,const char *name,const char *value) {
    put(r,"<label>");
    html_escaped(r,name);
    put(r,"</label><input name=\"");
    html_escaped(r,name);
    put(r,"\" maxlength=\"190\" value=\"");
    html_escaped(r,value);
    put(r,"\">");
}
static int hex_digit(char c) {
    if(c>='0'&&c<='9')return c-'0';
    if(c>='a'&&c<='f')return c-'a'+10;
    if(c>='A'&&c<='F')return c-'A'+10;
    return -1;
}
static bool decode_component(const char *src,size_t n,char *dst,size_t cap) {
    size_t j=0;
    if(cap==0)return false;
    for(size_t i=0;i<n;i++) {
        uint8_t value=(uint8_t)src[i];
        if(value=='+') value=' ';
        else if(value=='%') {
            if(i+2>=n) return false;
            int hi=hex_digit(src[++i]),lo=hex_digit(src[++i]);
            if(hi<0||lo<0)return false;
            value=(uint8_t)((hi<<4)|lo);
        }
        if(value==0 || value=='\r' || value=='\n' ||
           (value<32 && value!='\t') || j+1>=cap)return false;
        dst[j++]=(char)value;
    }
    dst[j]=0;
    return true;
}
static bool field(const char *form,const char *key,char *out,size_t cap) {
    if(cap==0)return false;
    out[0]=0;
    size_t target=strlen(key);
    const char *p=form;
    while(p && *p) {
        const char *end=strchr(p,'&');
        if(!end)end=p+strlen(p);
        const char *eq=memchr(p,'=',(size_t)(end-p));
        if(eq && (size_t)(eq-p)==target && memcmp(p,key,target)==0)
            return decode_component(eq+1,(size_t)(end-eq-1),out,cap);
        p=*end?end+1:NULL;
    }
    return false;
}
static void issue_nonce(void) {
    uint32_t a=esp_random(),b=esp_random();
    snprintf(csrf_nonce,sizeof(csrf_nonce),"%08lx%08lx",
             (unsigned long)a,(unsigned long)b);
}
static esp_err_t page(httpd_req_t *req) {
    if(!authenticate(req))return ESP_OK;
    hub_ai_settings_t settings;
    hub_ai_connection_t conn;
    if(!hub_ai_read_settings(&settings))return reject(req,"500 Internal Server Error","Invalid settings");
    (void)hub_ai_load_connection(&conn);
    issue_nonce();
    httpd_resp_set_type(req,"text/html; charset=utf-8");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    httpd_resp_set_hdr(req,"X-Content-Type-Options","nosniff");
    httpd_resp_set_hdr(req,"Content-Security-Policy",
                       "default-src 'none'; style-src 'unsafe-inline'; form-action 'self'; base-uri 'none'");
    put(req,"<!doctype html><html lang=\"zh-CN\"><meta charset=\"utf-8\">");
    put(req,"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">");
    put(req,"<title>Passport AI</title><style>"
       "body{font:16px system-ui;background:#101826;color:#eef5fa;max-width:640px;"
       "padding:18px;margin:auto}label{display:block;margin:14px 0 5px}"
       "input{box-sizing:border-box;width:100%;padding:10px;border-radius:7px;"
       "background:#223449;border:1px solid #60708a;color:white}"
       "input[type=checkbox]{width:auto}button{background:#83e0b8;color:#102030;"
       "padding:12px;border:0;border-radius:8px;font-weight:bold}"
       "small{color:#9db2c5}aside{padding:12px;border-left:3px solid #f9bc7b;"
       "background:#293444}</style><h2>Passport · AI 通知总结</h2>");
    put(req,"<aside>后台直接运行在 Passport 内，无须 NAS/Mac。"
       "默认关闭 AI。启用后，经过筛选的通知预览会直接发送到你指定的模型提供商。"
       "本地网页基于 WPA2 热点和管理员密码，尚非生产级加密。</aside>");
    put(req,"<form method=\"POST\" action=\"/save\" autocomplete=\"off\">"
       "<input type=\"hidden\" name=\"csrf\" value=\"");
    html_escaped(req,csrf_nonce);
    put(req,"\"><h3>Wi-Fi 连接</h3>");
    html_input(req,"ssid",conn.ssid);
    put(req,"<label>Wi-Fi 密码（留空则维持旧密码）</label>"
       "<input type=\"password\" name=\"wifi_password\" maxlength=\"64\">");
    put(req,"<h3>AI 供应商</h3>");
    html_input(req,"endpoint",settings.endpoint);
    html_input(req,"model",settings.model);
    put(req,"<label>API Key（留空则保留当前密钥）</label>"
       "<input type=\"password\" name=\"api_key\" maxlength=\"190\">");
    put(req,"<small>");
    put(req,settings.api_key[0]?"已配置 API Key（不会回显）":"尚未配置 API Key");
    put(req,"</small><label><input type=\"checkbox\" name=\"enabled\" value=\"1\"");
    if(settings.enabled)put(req," checked");
    put(req,"> 明确允许联网 AI 总结</label>");
    char number[32];
    snprintf(number,sizeof(number),"%d",settings.interval_minutes);
    html_input(req,"interval",number);
    snprintf(number,sizeof(number),"%d",settings.max_records);
    html_input(req,"max_records",number);
    put(req,"<label>排除应用标识（用英文逗号分隔）</label>");
    html_input(req,"excluded_apps",settings.excluded_apps);
    put(req,"<label><input type=\"checkbox\" name=\"redact_sensitive\" value=\"1\"");
    if(settings.redact_sensitive)put(req," checked");
    put(req,"> 不向 AI 发送包含验证码、密码等敏感词的通知</label>");
    put(req,"<p><button type=\"submit\">保存并重启 Passport</button></p>"
       "</form><small>管理入口：http://192.168.4.1。修改模型或周期无需重新刷机。"
       "首次需要连上 Passport 热点再配置可上网的 Wi-Fi。</small></html>");
    return httpd_resp_sendstr_chunk(req,NULL);
}
static void reboot(void *unused) {
    (void)unused;
    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();
}
static esp_err_t save(httpd_req_t *req) {
    if(!authenticate(req))return ESP_OK;
    if(req->content_len<=0 || req->content_len>1700)
        return reject(req,"413 Payload Too Large","Configuration form too large");
    char *body=calloc(1,req->content_len+1);
    if(!body)return reject(req,"500 Internal Server Error","Out of memory");
    int pos=0;
    while(pos<req->content_len) {
        int got=httpd_req_recv(req,body+pos,req->content_len-pos);
        if(got<=0) {free(body);return reject(req,"400 Bad Request","Invalid form");}
        pos+=got;
    }
    char token[32],enabled[4],redact[4],minutes[16],max[16],wifi_pass[65],api[192];
    char tmp[192],ssid[33];
    bool valid=field(body,"csrf",token,sizeof(token)) &&
        constant_equals(token,csrf_nonce) && csrf_nonce[0];
    /* Consume nonce immediately; cannot replay a privileged save. */
    memset(csrf_nonce,0,sizeof(csrf_nonce));
    hub_ai_settings_t cfg;
    hub_ai_connection_t wifi;
    if(!hub_ai_read_settings(&cfg)) valid=false;
    (void)hub_ai_load_connection(&wifi);
    if(valid && field(body,"ssid",ssid,sizeof(ssid)))
        snprintf(wifi.ssid,sizeof(wifi.ssid),"%s",ssid);
    if(valid && field(body,"wifi_password",wifi_pass,sizeof(wifi_pass)) &&
       wifi_pass[0]) snprintf(wifi.password,sizeof(wifi.password),"%s",wifi_pass);
    if(valid && field(body,"endpoint",tmp,sizeof(tmp)))
        snprintf(cfg.endpoint,sizeof(cfg.endpoint),"%s",tmp);
    if(valid && field(body,"model",tmp,sizeof(tmp)))
        snprintf(cfg.model,sizeof(cfg.model),"%s",tmp);
    if(valid && field(body,"api_key",api,sizeof(api)) && api[0])
        snprintf(cfg.api_key,sizeof(cfg.api_key),"%s",api);
    if(valid && field(body,"excluded_apps",tmp,sizeof(tmp)))
        snprintf(cfg.excluded_apps,sizeof(cfg.excluded_apps),"%s",tmp);
    if(valid && field(body,"interval",minutes,sizeof(minutes)))
        cfg.interval_minutes=atoi(minutes);
    if(valid && field(body,"max_records",max,sizeof(max)))
        cfg.max_records=atoi(max);
    cfg.enabled=field(body,"enabled",enabled,sizeof(enabled)) &&
        strcmp(enabled,"1")==0;
    cfg.redact_sensitive=field(body,"redact_sensitive",redact,sizeof(redact)) &&
        strcmp(redact,"1")==0;
    if(!wifi.ssid[0] && cfg.enabled) valid=false;
    if(valid) valid=hub_ai_save_settings(&cfg,&wifi);
    memset(&cfg,0,sizeof(cfg));
    memset(body,0,req->content_len);
    free(body);
    if(!valid)return reject(req,"422 Unprocessable Entity",
                            "Invalid config. Return, inspect inputs and retry.");
    httpd_resp_set_type(req,"text/html; charset=utf-8");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    esp_err_t done=httpd_resp_sendstr(req,
        "<meta charset=\"utf-8\"><h2>设置已保存</h2>"
        "<p>Passport 正在重启以应用新设置；重新连接其 Wi-Fi 后打开 192.168.4.1。</p>");
    if(xTaskCreate(reboot,"hub_reboot",2048,NULL,3,NULL)!=pdPASS)
        ESP_LOGW(TAG,"Settings saved; reboot manually to apply");
    return done;
}
void hub_ai_web_start(void) {
    if(server)return;
    httpd_config_t cfg=HTTPD_DEFAULT_CONFIG();
    cfg.stack_size=5120;
    cfg.max_uri_handlers=4;
    cfg.max_open_sockets=3;
    cfg.lru_purge_enable=true;
    if(httpd_start(&server,&cfg)!=ESP_OK) {
        ESP_LOGE(TAG,"Could not start embedded admin");
        return;
    }
    httpd_uri_t index={.uri="/",.method=HTTP_GET,.handler=page};
    httpd_uri_t admin={.uri="/admin",.method=HTTP_GET,.handler=page};
    httpd_uri_t submit={.uri="/save",.method=HTTP_POST,.handler=save};
    (void)httpd_register_uri_handler(server,&index);
    (void)httpd_register_uri_handler(server,&admin);
    (void)httpd_register_uri_handler(server,&submit);
    ESP_LOGI(TAG,"Embedded AI admin running at http://192.168.4.1/");
}
