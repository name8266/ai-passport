#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../firmware/main/hub_ai.h"
#include "../firmware/main/hub_app_catalog.h"
#include "../firmware/main/hub_ai_prompt.h"
int main(void) {
    assert(!strcmp(hub_catalog_display("com.tencent.xin"),"微信"));
    assert(!strcmp(hub_catalog_category("com.tencent.xin"),"社交"));
    assert(!strcmp(hub_catalog_display("com.bytedance.feishu"),"飞书"));
    assert(!strcmp(hub_catalog_category("com.bytedance.feishu"),"工作"));
    assert(!strcmp(hub_catalog_category("com.alipay.iphoneclient"),"金融"));
    assert(!strcmp(hub_catalog_display("org.example.OtherApp"),"OtherApp"));
    assert(!strcmp(hub_catalog_category("org.example.OtherApp"),"其他"));
    assert(!strcmp(hub_catalog_display("Unresolved"),"未识别应用"));
    uint32_t midnight=20000u*86400u;
    assert(hub_ai_day_tag(0)==0);
    assert(hub_ai_day_tag(midnight)==hub_ai_day_tag(midnight+15u*3600u));
    assert(hub_ai_day_tag(midnight)!=hub_ai_day_tag(midnight+16u*3600u));
    assert(strstr(HUB_AI_SYSTEM_PROMPT,"此前尚未清除的摘要"));
    assert(strstr(HUB_AI_SYSTEM_PROMPT,"不得编造"));
    assert(strstr(HUB_AI_SYSTEM_PROMPT,"open_items"));
    assert(strstr(HUB_AI_SYSTEM_PROMPT,"JSON"));
    assert(HUB_AI_TRIGGER_COUNT==3);
    assert(!hub_ai_batch_ready(0,false));
    assert(!hub_ai_batch_ready(2,false));
    assert(hub_ai_batch_ready(3,false));
    assert(!hub_ai_batch_ready(0,true));
    assert(hub_ai_batch_ready(1,true));
    assert(HUB_AI_TASK_LIMIT==5);
    puts("Catalog, fixed timezone, 3-message trigger and structured digest prompt: PASS");
    return 0;
}
