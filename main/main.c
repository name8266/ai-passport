#include "bsp_display.h"
#include "bsp_button.h"
#include "bsp_battery.h"
#include "worldcam_protocol.h"
#include "lvgl.h"
#include "src/misc/cache/instance/lv_image_cache.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

LV_FONT_DECLARE(worldcam_font_16);
extern const lv_image_dsc_t worldcam_map;
#define WIFI_READY BIT0
#define WIFI_SETUP BIT1
#define WIFI_FAILED BIT2
#define JSON_LIMIT 4096
#define NAME_BYTES 192

typedef enum { PAGE_MAP, PAGE_VIEW, PAGE_SETUP } page_t;
typedef enum { JOB_INDEX, JOB_INFO, JOB_FRAME } job_kind_t;
typedef struct { bsp_btn_t btn; bsp_btn_ev_t event; } input_t;
typedef struct { job_kind_t kind; unsigned generation; uint16_t index; bool full; char gateway[160]; } job_t;
typedef struct {
    job_t job;
    wc_point_t *points;
    size_t count;
    uint8_t *pixels;
    uint16_t width,height;
    uint32_t acquired;
    char name[NAME_BYTES],country[96];
    bool available,verified;
    const char *error;
} result_t;
static QueueHandle_t s_inputs,s_jobs,s_results;
static EventGroupHandle_t s_wifi;
static TaskHandle_t s_worker;
static nvs_handle_t s_nvs;
static char s_gateway[160],s_ap_name[32],s_ap_password[9];
static httpd_handle_t s_httpd;
static page_t s_page;
static wc_point_t *s_points;
static size_t s_count,s_selected;
static unsigned s_generation;
static int64_t s_next_refresh;
static bool s_busy,s_full,s_capitals_only;
static unsigned s_random_retries;
static char s_name[NAME_BYTES],s_country[96];
static uint8_t *s_pixels;
static lv_image_dsc_t s_image_desc;
static lv_obj_t *s_title,*s_body,*s_country_label,*s_status,*s_help,*s_image,*s_battery,*s_map,*s_marker;

static lv_obj_t *label(int x,int y,int width,uint32_t color)
{
    lv_obj_t *obj=lv_label_create(lv_screen_active());
    lv_obj_set_pos(obj,x,y);lv_obj_set_width(obj,width);
    lv_obj_set_style_text_color(obj,lv_color_hex(color),0);
    lv_label_set_text(obj,"");return obj;
}
static void visible(lv_obj_t *obj,bool show)
{
    if(show)lv_obj_remove_flag(obj,LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(obj,LV_OBJ_FLAG_HIDDEN);
}
static void draw(const char *status)
{
    if(!bsp_lvgl_lock(-1))return;
    bool full=s_page==PAGE_VIEW&&s_full;
    visible(s_title,!full);visible(s_help,!full);visible(s_battery,!full);
    visible(s_map,s_page==PAGE_MAP);visible(s_marker,s_page==PAGE_MAP&&s_count);
    visible(s_body,s_page!=PAGE_VIEW);visible(s_country_label,s_page!=PAGE_SETUP&&!full);
    visible(s_image,s_page==PAGE_VIEW&&s_pixels);
    visible(s_status,!full||!s_pixels);
    char text[480]={0};
    if(s_page==PAGE_MAP){
        lv_label_set_text(s_title,s_capitals_only?"各国首都":"世界之窗");
        lv_obj_set_pos(s_country_label,18,192);
        lv_label_set_text(s_country_label,s_country);
        lv_obj_set_pos(s_body,18,216);
        lv_obj_set_height(s_body,26);
        lv_label_set_long_mode(s_body,LV_LABEL_LONG_DOT);
        if(s_count){
            int x,y;wc_map_project(s_points[s_selected].lat,s_points[s_selected].lon,&x,&y);
            lv_obj_set_pos(s_marker,12+x-5,53+y-5);
            snprintf(text,sizeof(text),"%s",s_name[0]?s_name:"正在加载地点名称");
            lv_label_set_text(s_body,text);
        }else lv_label_set_text(s_body,"按OK加载地点目录");
        lv_label_set_text(s_help,"前后选 · OK查看 · 长上随机\n长下筛首都 · 长OK设置");
    }else if(s_page==PAGE_VIEW){
        lv_label_set_text(s_title,s_name[0]?s_name:"地点画面");
        lv_obj_set_pos(s_country_label,18,54);
        lv_label_set_text(s_country_label,s_country);
        lv_obj_set_pos(s_image,s_full?0:24,s_full?80:82);
        lv_label_set_text(s_help,"OK刷新 · 长OK返回地图\n长下全屏 · 长上随机游览");
    }else{
        lv_label_set_text(s_title,"连接世界之窗");
        lv_obj_set_pos(s_body,18,64);lv_obj_set_height(s_body,182);
        lv_label_set_long_mode(s_body,LV_LABEL_LONG_WRAP);
        snprintf(text,sizeof(text),"手机连接热点：\n%s\n密码：%s\n\n浏览器打开：\n192.168.4.1\n配置网络和网关地址",s_ap_name,s_ap_password);
        lv_label_set_text(s_body,text);
        lv_label_set_text(s_help,"需要2.4GHz无线网络\n长按OK返回地图");
    }
    lv_obj_set_pos(s_status,18,full?252:248);
    lv_label_set_text(s_status,status?status:"");
    bsp_lvgl_unlock();
}
static void button(bsp_btn_t btn,bsp_btn_ev_t event,void *user)
{
    (void)user;if(event!=BSP_BTN_CLICK&&event!=BSP_BTN_LONG)return;
    input_t input={btn,event};(void)xQueueSend(s_inputs,&input,0);
}
static void clear_pixels(void)
{
    if(!s_pixels||!bsp_lvgl_lock(-1))return;
    lv_image_set_src(s_image,NULL);
    lv_image_cache_drop(&s_image_desc);
    free(s_pixels);s_pixels=NULL;
    bsp_lvgl_unlock();
}
static void queue_job(job_kind_t kind)
{
    if(kind!=JOB_INDEX&&!s_count)return;
    job_t job={.kind=kind,.generation=++s_generation,.full=s_full};
    if(s_count)job.index=s_points[s_selected].index;
    snprintf(job.gateway,sizeof(job.gateway),"%s",s_gateway);
    s_busy=true;
    if(kind==JOB_FRAME){s_next_refresh=esp_timer_get_time()+60000000;clear_pixels();}
    xQueueOverwrite(s_jobs,&job);
    draw(kind==JOB_FRAME?"正在获取画面…":kind==JOB_INDEX?"正在加载地图地点…":"正在查询地点…");
}
static void select_location(size_t index,bool watch)
{
    s_selected=index;s_name[0]=0;s_country[0]=0;s_page=watch?PAGE_VIEW:PAGE_MAP;
    clear_pixels();queue_job(JOB_INFO);
}
static void random_tour(bool retry)
{
    size_t chosen;
    if(!wc_random_index(s_points,s_count,esp_random(),s_selected,&chosen)){draw("暂无可游览的公开画面");return;}
    if(!retry)s_random_retries=3;
    select_location(chosen,true);
}
static bool read_exact(esp_http_client_handle_t client,void *data,size_t bytes)
{
    size_t received=0;
    while(received<bytes){int n=esp_http_client_read(client,(char *)data+received,bytes-received);if(n<=0)return false;received+=(size_t)n;}
    return true;
}
static bool copy_text(cJSON *row,const char *key,char *dest,size_t size)
{
    cJSON *value=cJSON_GetObjectItemCaseSensitive(row,key);
    if(!cJSON_IsString(value)||!value->valuestring[0]||strlen(value->valuestring)>=size||!wc_utf8_valid(value->valuestring))return false;
    snprintf(dest,size,"%s",value->valuestring);return true;
}
static void worker(void *arg)
{
    (void)arg;job_t job;
    for(;;){
        xQueueReceive(s_jobs,&job,portMAX_DELAY);
        result_t result={.job=job};
        if(!(xEventGroupGetBits(s_wifi)&WIFI_READY)){result.error="无线网络未连接";goto done;}
        char url[240];
        if(job.kind==JOB_INDEX)snprintf(url,sizeof(url),"%s/api/index",job.gateway);
        else snprintf(url,sizeof(url),"%s/api/%s/%u%s",job.gateway,job.kind==JOB_FRAME?"frame-location":"location",job.index,job.kind==JOB_FRAME&&job.full?"?full=1":"");
        esp_http_client_config_t config={.url=url,.timeout_ms=12000,.buffer_size=1024,.disable_auto_redirect=true};
        esp_http_client_handle_t client=esp_http_client_init(&config);
        if(!client){result.error="内存不足";goto done;}
        if(esp_http_client_open(client,0)!=ESP_OK){result.error="无法连接配套网关";goto cleanup;}
        int64_t length=esp_http_client_fetch_headers(client);
        if(esp_http_client_get_status_code(client)!=200){result.error="画面不可用，按OK重试";goto cleanup;}
        if(job.kind==JOB_FRAME){
            uint8_t header[WC_HEADER_BYTES];
            if(!read_exact(client,header,sizeof(header))||!wc_frame_info(header,sizeof(header),&result.width,&result.height,&result.acquired)){
                result.error="画面格式不正确";goto cleanup;
            }
            size_t bytes=(size_t)result.width*result.height*2;
            if(length!=(int64_t)(WC_HEADER_BYTES+bytes)||result.width!=(job.full?WC_FULL_WIDTH:WC_WIDTH)||result.height!=(job.full?WC_FULL_HEIGHT:WC_HEIGHT)){
                result.error="画面尺寸不正确";goto cleanup;
            }
            result.pixels=malloc(bytes);
            if(!result.pixels){result.error="内存不足";goto cleanup;}
            if(!read_exact(client,result.pixels,bytes)){free(result.pixels);result.pixels=NULL;result.error="画面下载不完整";}
        }else if(job.kind==JOB_INDEX){
            uint8_t header[WC_INDEX_HEADER_BYTES];
            if(!read_exact(client,header,sizeof(header))||!wc_index_header(header,sizeof(header),&result.count)||
                length!=(int64_t)(WC_INDEX_HEADER_BYTES+result.count*WC_INDEX_RECORD_BYTES)){
                result.error="地点目录格式不正确";goto cleanup;
            }
            result.points=calloc(result.count,sizeof(wc_point_t));
            if(!result.points){result.error="内存不足";goto cleanup;}
            for(size_t i=0;i<result.count;++i){
                uint8_t record[WC_INDEX_RECORD_BYTES];
                if(!read_exact(client,record,sizeof(record))||!wc_index_point(record,sizeof(record),&result.points[i])||result.points[i].index!=i){
                    result.error="地点坐标不正确";break;
                }
            }
        }else{
            if(length<=0||length>JSON_LIMIT){result.error="地点资料过长";goto cleanup;}
            char *body=malloc((size_t)length+1);
            if(!body){result.error="内存不足";goto cleanup;}
            if(!read_exact(client,body,(size_t)length)){free(body);result.error="地点资料下载失败";goto cleanup;}
            body[length]=0;cJSON *root=cJSON_Parse(body);free(body);
            if(!copy_text(root,"name_zh",result.name,sizeof(result.name))||!copy_text(root,"country_zh",result.country,sizeof(result.country)))result.error="地点文字不支持";
            result.available=cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root,"available"));
            result.verified=cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root,"verified_available"));
            cJSON_Delete(root);
        }
cleanup:
        esp_http_client_close(client);esp_http_client_cleanup(client);
done:
        xQueueSend(s_results,&result,portMAX_DELAY);
        ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
    }
}
static void wifi_event(void *arg, esp_event_base_t base, int32_t event, void *data)
{
    (void)arg; (void)data;
    if (base == WIFI_EVENT && event == WIFI_EVENT_STA_START) esp_wifi_connect();
    if (base == WIFI_EVENT && event == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_wifi, WIFI_READY);
        xEventGroupSetBits(s_wifi, WIFI_FAILED);
    }
    if (base == IP_EVENT && event == IP_EVENT_STA_GOT_IP) {
        xEventGroupClearBits(s_wifi, WIFI_FAILED);
        xEventGroupSetBits(s_wifi, WIFI_READY);
    }
}
static esp_err_t setup_page(httpd_req_t *req)
{
    static const char html[] = "<!doctype html><html lang='zh-CN'><meta name='viewport' content='width=device-width'><title>世界之窗网络配置</title><style>body{font:16px system-ui;max-width:480px;margin:40px auto;padding:20px;background:#071820;color:#eef7f9}input,button{box-sizing:border-box;width:100%;padding:12px;margin:10px 0}button{background:#4de4bd;border:0}</style><h1>世界之窗网络配置</h1><p>连接2.4GHz无线网络。配套网关需在同一局域网电脑上运行。</p><form><label>无线网络名称<input name='ssid' maxlength='32' required></label><label>密码<input name='password' type='password' maxlength='63'></label><label>网关地址<input name='gateway' placeholder='http://192.168.1.10:8787' required></label><button>保存并连接</button></form><p id='status'></p><script>document.querySelector('form').onsubmit=async e=>{e.preventDefault();try{const r=await fetch('/setup',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(Object.fromEntries(new FormData(e.target)))});document.querySelector('#status').textContent=await r.text()}catch(e){document.querySelector('#status').textContent='请查看设备屏幕上的连接状态。'}}</script></html>";
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
}
static esp_err_t save_setup(httpd_req_t *req)
{
    if (req->content_len <= 0 || req->content_len >= 512) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "配置长度无效");
    char body[512];
    size_t read = 0;
    while (read < (size_t)req->content_len) {
        int n = httpd_req_recv(req, body + read, req->content_len - read);
        if (n <= 0) return ESP_FAIL;
        read += (size_t)n;
    }
    body[read] = 0;
    cJSON *root = cJSON_Parse(body);
    cJSON *ssid = cJSON_GetObjectItemCaseSensitive(root, "ssid");
    cJSON *password = cJSON_GetObjectItemCaseSensitive(root, "password");
    cJSON *gateway = cJSON_GetObjectItemCaseSensitive(root, "gateway");
    if (!cJSON_IsString(ssid) || !cJSON_IsString(password) || !cJSON_IsString(gateway) ||
        !ssid->valuestring[0] || strlen(ssid->valuestring) > 32 || strlen(password->valuestring) > 63 ||
        (strlen(password->valuestring) > 0 && strlen(password->valuestring) < 8) || !wc_gateway_valid(gateway->valuestring)) {
        cJSON_Delete(root);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "请检查网络及http://主机:端口地址，末尾不要加斜杠");
    }
    esp_err_t err = nvs_set_str(s_nvs, "ssid", ssid->valuestring);
    if (err == ESP_OK) err = nvs_set_str(s_nvs, "password", password->valuestring);
    if (err == ESP_OK) err = nvs_set_str(s_nvs, "gateway", gateway->valuestring);
    if (err == ESP_OK) err = nvs_commit(s_nvs);
    cJSON_Delete(root);
    if (err != ESP_OK) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "无法保存配置");
    // Input task performs configuration after the response is sent, without
    // racing a network worker's gateway read or a second HTTP request.
    xEventGroupSetBits(s_wifi, WIFI_SETUP);
    return httpd_resp_sendstr(req, "已保存。请查看设备屏幕；连接失败时可在此更正。");
}
static void start_setup(void)
{
    wifi_config_t config = {0};
    snprintf((char *)config.ap.ssid, sizeof(config.ap.ssid), "%s", s_ap_name);
    snprintf((char *)config.ap.password, sizeof(config.ap.password), "%s", s_ap_password);
    config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    config.ap.max_connection = 1;
    config.ap.channel = 1;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &config));
    if (!s_httpd) {
        httpd_config_t http_config = HTTPD_DEFAULT_CONFIG();
        http_config.max_uri_handlers = 2;
        http_config.recv_wait_timeout = 5;
        if (httpd_start(&s_httpd, &http_config) == ESP_OK) {
            httpd_uri_t get = {.uri = "/", .method = HTTP_GET, .handler = setup_page};
            httpd_uri_t post = {.uri = "/setup", .method = HTTP_POST, .handler = save_setup};
            ESP_ERROR_CHECK(httpd_register_uri_handler(s_httpd, &get));
            ESP_ERROR_CHECK(httpd_register_uri_handler(s_httpd, &post));
        }
    }
    s_page = PAGE_SETUP;
    s_busy = false;
    ++s_generation;
    draw(s_httpd ? "网络配置" : "配置服务失败，请重启");
}
static void connect_saved(void)
{
    wifi_config_t config = {0};
    char ssid[33] = {0}, password[64] = {0};
    size_t size = sizeof(ssid);
    nvs_get_str(s_nvs, "ssid", ssid, &size);
    size = sizeof(password); nvs_get_str(s_nvs, "password", password, &size);
    if (!ssid[0]) { start_setup(); return; }
    memcpy(config.sta.ssid, ssid, strlen(ssid));
    memcpy(config.sta.password, password, strlen(password));
    config.sta.threshold.authmode = password[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    esp_wifi_disconnect();
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &config));
    esp_wifi_connect();
}
static void handle_input(input_t input)
{
    if(input.event==BSP_BTN_LONG){
        if(input.btn==BSP_BTN_OK){
            ++s_generation;s_busy=false;s_random_retries=0;
            if(s_page==PAGE_MAP){start_setup();return;}
            s_page=PAGE_MAP;clear_pixels();
            if(s_count)queue_job(JOB_INFO);else draw(NULL);
        }else if(input.btn==BSP_BTN_UP&&s_page!=PAGE_SETUP)random_tour(false);
        else if(input.btn==BSP_BTN_DOWN&&s_page!=PAGE_SETUP){
            if(s_page==PAGE_VIEW){s_full=!s_full;if(s_count)queue_job(JOB_FRAME);}
            else{
                s_capitals_only=!s_capitals_only;
                if(s_count){
                    size_t next=s_selected;
                    for(size_t i=0;i<s_count;++i){if(!s_capitals_only||(s_points[next].flags&WC_FLAG_CAPITAL))break;next=wc_wrap_index(next,1,s_count);}
                    select_location(next,false);
                }else draw(NULL);
            }
        }
        return;
    }
    if(input.event!=BSP_BTN_CLICK||s_page==PAGE_SETUP)return;
    if(input.btn==BSP_BTN_UP||input.btn==BSP_BTN_DOWN){
        if(!s_count){draw("按OK加载地图地点");return;}
        s_random_retries=0;int direction=input.btn==BSP_BTN_UP?-1:1;
        size_t next=s_selected;
        for(size_t i=0;i<s_count;++i){next=wc_wrap_index(next,direction,s_count);if(!s_capitals_only||(s_points[next].flags&WC_FLAG_CAPITAL))break;}
        select_location(next,s_page==PAGE_VIEW);
    }else if(input.btn==BSP_BTN_OK){
        s_random_retries=0;
        if(!s_count){queue_job(JOB_INDEX);return;}
        if(!(s_points[s_selected].flags&WC_FLAG_HAS_SOURCE)){draw("尚未找到公开可用画面");return;}
        if(s_page==PAGE_MAP){s_page=PAGE_VIEW;queue_job(JOB_INFO);}
        else queue_job(JOB_FRAME);
    }
}
static void apply_result(result_t *result)
{
    bool continue_frame=false,continue_info=false,retry_random=false;
    if(result->job.generation!=s_generation)goto release;
    s_busy=false;
    if(result->error){
        draw(result->error);
        if(s_page==PAGE_VIEW&&result->job.kind==JOB_FRAME&&s_random_retries>0){--s_random_retries;retry_random=true;}
        goto release;
    }
    if(result->job.kind==JOB_FRAME){
        if(!bsp_lvgl_lock(-1))goto release;
        lv_image_set_src(s_image,NULL);lv_image_cache_drop(&s_image_desc);free(s_pixels);
        s_pixels=result->pixels;result->pixels=NULL;
        s_image_desc=(lv_image_dsc_t){.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_RGB565,
            .w=result->width,.h=result->height,.stride=result->width*2},.data_size=(uint32_t)result->width*result->height*2,.data=s_pixels};
        lv_image_set_src(s_image,&s_image_desc);bsp_lvgl_unlock();
        time_t timestamp=result->acquired;struct tm fetched;gmtime_r(&timestamp,&fetched);
        char status[64];snprintf(status,sizeof(status),"获取于%02d:%02d UTC · 快照",fetched.tm_hour,fetched.tm_min);
        draw(status);s_random_retries=0;s_next_refresh=esp_timer_get_time()+60000000;
    }else if(result->job.kind==JOB_INDEX){
        free(s_points);s_points=result->points;result->points=NULL;s_count=result->count;s_selected=0;
        continue_info=s_count>0;
        if(!s_count)draw("地点目录为空");
    }else{
        snprintf(s_name,sizeof(s_name),"%s",result->name);snprintf(s_country,sizeof(s_country),"%s",result->country);
        draw(!result->available?"尚未找到公开可用画面":result->verified?"位置为简略地图坐标":"来源暂不可用，可重试");
        continue_frame=s_page==PAGE_VIEW&&result->available;
    }
release:
    free(result->pixels);free(result->points);xTaskNotifyGive(s_worker);
    if(retry_random)random_tour(true);
    else if(continue_info)queue_job(JOB_INFO);
    else if(continue_frame)queue_job(JOB_FRAME);
}
static void input_task(void *arg)
{
    (void)arg;int64_t next_battery=0,next_connect=0;bool was_ready=false;
    for(;;){
        input_t input;if(xQueueReceive(s_inputs,&input,pdMS_TO_TICKS(30)))handle_input(input);
        result_t result;if(xQueueReceive(s_results,&result,0))apply_result(&result);
        EventBits_t bits=xEventGroupGetBits(s_wifi);
        if((bits&WIFI_SETUP)&&!s_busy){
            xEventGroupClearBits(s_wifi,WIFI_SETUP|WIFI_FAILED|WIFI_READY);
            size_t size=sizeof(s_gateway);nvs_get_str(s_nvs,"gateway",s_gateway,&size);
            ++s_generation;free(s_points);s_points=NULL;s_count=0;
            connect_saved();draw("正在连接无线网络…");
        }
        bool ready=(xEventGroupGetBits(s_wifi)&WIFI_READY)!=0;
        if(ready&&!was_ready){
            if(s_httpd){httpd_stop(s_httpd);s_httpd=NULL;}
            esp_wifi_set_mode(WIFI_MODE_STA);
            if(s_page==PAGE_SETUP)s_page=PAGE_MAP;
            if(!s_count)queue_job(JOB_INDEX);else draw("无线网络已连接");
        }
        was_ready=ready;int64_t now=esp_timer_get_time();
        if((bits&WIFI_FAILED)&&now>=next_connect){
            next_connect=now+10000000;esp_wifi_connect();
            if(s_page==PAGE_SETUP)draw("连接失败，请修改配置");else if(!s_busy)draw("无线网络已断开");
        }
        if(s_page==PAGE_VIEW&&!s_busy&&s_count&&(s_points[s_selected].flags&WC_FLAG_HAS_SOURCE)&&now>=s_next_refresh){
            s_next_refresh=now+60000000;queue_job(JOB_FRAME);
        }
        if(now>=next_battery){
            next_battery=now+30000000;int soc=bsp_battery_soc();
            if(bsp_lvgl_lock(-1)){if(soc<0)lv_label_set_text(s_battery,"--");else lv_label_set_text_fmt(s_battery,"%d%%",soc);bsp_lvgl_unlock();}
        }
    }
}
void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());ESP_ERROR_CHECK(nvs_open("worldcam",NVS_READWRITE,&s_nvs));
    ESP_ERROR_CHECK(bsp_display_init());if(!bsp_lvgl_init())return;bsp_battery_init();
    s_inputs=xQueueCreate(8,sizeof(input_t));s_jobs=xQueueCreate(1,sizeof(job_t));s_results=xQueueCreate(1,sizeof(result_t));s_wifi=xEventGroupCreate();
    if(!s_inputs||!s_jobs||!s_results||!s_wifi)abort();
    uint8_t mac[6];ESP_ERROR_CHECK(esp_netif_init());ESP_ERROR_CHECK(esp_event_loop_create_default());
    if(!esp_netif_create_default_wifi_sta()||!esp_netif_create_default_wifi_ap())abort();
    wifi_init_config_t wifi_config=WIFI_INIT_CONFIG_DEFAULT();ESP_ERROR_CHECK(esp_wifi_init(&wifi_config));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_STA,mac));
    snprintf(s_ap_name,sizeof(s_ap_name),"WorldCam-%02X%02X",mac[4],mac[5]);
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT,ESP_EVENT_ANY_ID,wifi_event,NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,IP_EVENT_STA_GOT_IP,wifi_event,NULL));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));ESP_ERROR_CHECK(esp_wifi_start());
    snprintf(s_ap_password,sizeof(s_ap_password),"%08lx",(unsigned long)esp_random());
    size_t size=sizeof(s_gateway);nvs_get_str(s_nvs,"gateway",s_gateway,&size);
    if(!bsp_lvgl_lock(-1))return;
    lv_obj_t *screen=lv_obj_create(NULL);lv_obj_remove_flag(screen,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen,lv_color_hex(0x071820),0);
    lv_obj_set_style_text_font(screen,&worldcam_font_16,0);lv_screen_load(screen);
    s_title=label(18,25,204,0x4DE4BD);lv_label_set_long_mode(s_title,LV_LABEL_LONG_DOT);
    s_battery=label(178,8,46,0xBDCED5);s_body=label(18,216,204,0xF0F7F9);
    s_country_label=label(18,192,204,0xBDCED5);lv_label_set_long_mode(s_country_label,LV_LABEL_LONG_DOT);
    s_status=label(18,248,204,0x4DE4BD);lv_label_set_long_mode(s_status,LV_LABEL_LONG_DOT);
    s_help=label(18,271,210,0xBDCED5);
    s_map=lv_image_create(screen);lv_image_set_src(s_map,&worldcam_map);lv_obj_set_pos(s_map,12,53);
    s_marker=lv_obj_create(screen);lv_obj_remove_flag(s_marker,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_size(s_marker,11,11);
    lv_obj_set_style_radius(s_marker,LV_RADIUS_CIRCLE,0);lv_obj_set_style_bg_color(s_marker,lv_color_hex(0xFFDE87),0);
    lv_obj_set_style_border_color(s_marker,lv_color_hex(0xFFFFFF),0);lv_obj_set_style_border_width(s_marker,2,0);
    s_image=lv_image_create(screen);lv_obj_set_pos(s_image,24,82);visible(s_image,false);
    bsp_lvgl_unlock();
    if(xTaskCreate(worker,"cam_net",7168,NULL,4,&s_worker)!=pdPASS)abort();
    if(wc_gateway_valid(s_gateway)){s_page=PAGE_MAP;draw("正在连接无线网络…");connect_saved();}else start_setup();
    ESP_ERROR_CHECK(bsp_button_init(button,NULL));
    if(xTaskCreate(input_task,"cam_ui",6144,NULL,5,NULL)!=pdPASS)abort();
}
