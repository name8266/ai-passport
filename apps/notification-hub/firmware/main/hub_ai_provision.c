/* Physical USB serial bootstrap commands. Prototype only: NVS is not
 * encrypted by default; production needs real authenticated provisioning.
 * Values are never logged; the sender's terminal may still echo passwords.
 */
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG="hub_setup";
static bool set_setting(const char *key,const char *value,size_t max) {
    if(!value || !value[0] || strlen(value)>max) return false;
    nvs_handle_t h;
    if(nvs_open("hub_ai",NVS_READWRITE,&h)!=ESP_OK) return false;
    esp_err_t err=nvs_set_str(h,key,value);
    if(err==ESP_OK) err=nvs_commit(h);
    nvs_close(h);
    return err==ESP_OK;
}
static void usb_setup(void *arg) {
    (void)arg;
    char line[320];
    puts("Passport AI provisioning: hub help");
    while(1) {
        if(!fgets(line,sizeof(line),stdin)) {
            clearerr(stdin);
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        line[strcspn(line,"\r\n")]=0;
        if(strcmp(line,"hub help")==0) {
            puts("hub wifi <SSID>|<PASSWORD>");
            puts("hub server https://example.com");
            puts("hub token <DEVICE_TOKEN>");
            puts("hub restart");
            puts("Privacy: terminal echo and NVS Flash may expose Wi-Fi/password/token.");
        } else if(strncmp(line,"hub wifi ",9)==0) {
            char *value=line+9, *separator=strchr(value,'|');
            if(!separator) {puts("ERROR: expected SSID|PASSWORD");continue;}
            *separator++=0;
            bool ok=set_setting("ssid",value,32) &&
                set_setting("pass",separator,64);
            puts(ok?"Wi-Fi saved (restart required)":"Wi-Fi config invalid");
        } else if(strncmp(line,"hub server ",11)==0) {
            const char *url=line+11;
            bool ok=strncmp(url,"https://",8)==0 &&
                strchr(url,'?')==NULL && strchr(url,'#')==NULL &&
                strchr(url,' ') == NULL &&
                url[strlen(url)-1]!='/' &&
                set_setting("gateway",url,175);
            puts(ok?"Gateway saved (restart required)":"HTTPS gateway URL invalid");
        } else if(strncmp(line,"hub token ",10)==0) {
            bool ok=set_setting("token",line+10,128);
            puts(ok?"Device token saved (restart required)":"Device token invalid");
        } else if(strcmp(line,"hub restart")==0) {
            puts("Restarting...");
            vTaskDelay(pdMS_TO_TICKS(250));
            esp_restart();
        } else if(line[0]) {
            puts("Unknown command. Type hub help");
        }
        /* Reduce the lifetime of the plaintext staging buffer. */
        memset(line,0,sizeof(line));
    }
}
void hub_ai_provision_start(void) {
    if(xTaskCreate(usb_setup,"hub_usb_setup",3328,NULL,2,NULL)!=pdPASS)
        ESP_LOGW(TAG,"USB provisioning task not started");
}
