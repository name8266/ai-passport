#include "pusher_store.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
static const char *TAG="coin_store";
static bool open_store(nvs_handle_t *h,nvs_open_mode_t mode){
 esp_err_t err=nvs_flash_init();if(err!=ESP_OK){ESP_LOGW(TAG,"NVS init failed: %s; no erase attempted",esp_err_to_name(err));return false;}
 err=nvs_open("cp_rogue",mode,h);if(err!=ESP_OK){nvs_flash_deinit();return false;}return true;
}
bool cp_store_load(cp_records_t *b){cp_records_init(b);nvs_handle_t h;if(!open_store(&h,NVS_READONLY))return false;
 size_t size=sizeof(*b);esp_err_t err=nvs_get_blob(h,"board_v1",b,&size);nvs_close(h);nvs_flash_deinit();
 bool ok=err==ESP_OK&&size==sizeof(*b)&&cp_records_valid(b);if(!ok)cp_records_init(b);
 ESP_LOGI(TAG,"Leaderboard load=%d generation=%lu first=%lu discovered=%08lx",ok,(unsigned long)b->generation,(unsigned long)b->rows[0].chips,(unsigned long)b->discovered);return ok;
}
bool cp_store_write(const cp_records_t *b){if(!cp_records_valid(b))return false;nvs_handle_t h;if(!open_store(&h,NVS_READWRITE))return false;
 esp_err_t err=nvs_set_blob(h,"board_v1",b,sizeof(*b));if(err==ESP_OK)err=nvs_commit(h);nvs_close(h);nvs_flash_deinit();
 ESP_LOGI(TAG,"Leaderboard saved=%d generation=%lu first=%lu",err==ESP_OK,(unsigned long)b->generation,(unsigned long)b->rows[0].chips);return err==ESP_OK;
}

bool cp_store_muted(void){nvs_handle_t h;uint8_t value=0;if(!open_store(&h,NVS_READONLY))return false;
 esp_err_t err=nvs_get_u8(h,"muted",&value);nvs_close(h);nvs_flash_deinit();return err==ESP_OK&&value==1;
}
bool cp_store_set_muted(bool muted){nvs_handle_t h;if(!open_store(&h,NVS_READWRITE))return false;
 esp_err_t err=nvs_set_u8(h,"muted",muted?1:0);if(err==ESP_OK)err=nvs_commit(h);nvs_close(h);nvs_flash_deinit();return err==ESP_OK;
}
