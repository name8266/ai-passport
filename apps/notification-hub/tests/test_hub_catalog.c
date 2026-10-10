#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../firmware/main/hub_ai.h"
#include "../firmware/main/hub_ai_state.h"
#include "../firmware/main/hub_archive_gc.h"
#include "../firmware/main/hub_app_catalog.h"
#include "../firmware/main/hub_ai_prompt.h"
#include "../firmware/main/hub_control_event.h"
#include "../firmware/main/hub_ai_filter.h"

static void test_protected_pending(void) {
    hub_ai_digest_t prior={0},proposal={0},out={0};
    strcpy(prior.summary,"先前重要事项");
    prior.task_count=2;
    strcpy(prior.tasks[0].task,"确认会议时间");
    strcpy(prior.tasks[0].source,"飞书");
    strcpy(prior.tasks[1].task,"领取快递");
    strcpy(prior.tasks[1].source,"菜鸟");
    uint32_t stable=hub_ai_task_id(&prior.tasks[0]);
    assert(stable==hub_ai_task_id(&prior.tasks[0]));
    assert(stable!=hub_ai_task_id(&prior.tasks[1]));
    strcpy(proposal.summary,"新的三条消息已合并");
    /* The model may omit old items; firmware must retain them. */
    assert(hub_ai_merge_pending(&prior,&proposal,&out)==HUB_AI_MERGE_OK);
    assert(out.task_count==2 && !strcmp(out.tasks[0].task,"确认会议时间"));
    /* Repeated candidate is deduplicated by stable source + task. */
    proposal.task_count=1;proposal.tasks[0]=prior.tasks[1];
    assert(hub_ai_merge_pending(&prior,&proposal,&out)==HUB_AI_MERGE_OK);
    assert(out.task_count==2);
    strcpy(proposal.tasks[0].task,"联系物业");
    strcpy(proposal.tasks[0].source,"微信");
    assert(hub_ai_merge_pending(&prior,&proposal,&out)==HUB_AI_MERGE_OK);
    assert(out.task_count==3 && !strcmp(out.tasks[2].task,"联系物业"));
    /* An unacknowledged full table cannot evict a previous commitment. */
    prior.task_count=HUB_AI_TASK_LIMIT;
    for(int i=2;i<HUB_AI_TASK_LIMIT;i++) {
        snprintf(prior.tasks[i].task,sizeof(prior.tasks[i].task),"旧待办%d",i);
        strcpy(prior.tasks[i].source,"飞书");
    }
    assert(hub_ai_merge_pending(&prior,&proposal,&out)==HUB_AI_MERGE_FULL);
    assert(!strcmp(prior.tasks[0].task,"确认会议时间"));
    /* An explicit ACK is the only way to discard old tasks. */
    prior.cleared=true;
    assert(hub_ai_merge_pending(&prior,&proposal,&out)==HUB_AI_MERGE_OK);
    assert(out.task_count==1 && !strcmp(out.tasks[0].task,"联系物业"));
    /* A filtered batch must never reuse a previously cleared task array. */
    out.task_count=HUB_AI_TASK_LIMIT;
    strcpy(out.tasks[0].task,"旧待办不得复活");
    hub_ai_clear_output(&out);
    assert(out.task_count==0 && out.summary[0]==0 && out.tasks[0].task[0]==0);
    /* Per-task completion must never erase an unrelated pending item. */
    prior.cleared=false;
    prior.task_count=2;
    memset(prior.tasks,0,sizeof(prior.tasks));
    strcpy(prior.tasks[0].task,"准备演示");
    strcpy(prior.tasks[0].source,"飞书");
    strcpy(prior.tasks[0].due,"明日");
    strcpy(prior.tasks[1].task,"检查设备");
    strcpy(prior.tasks[1].source,"备忘录");
    uint32_t id=hub_ai_task_id(&prior.tasks[0]);
    assert(!hub_ai_complete_pending(&prior,id+1));
    assert(prior.task_count==2);
    assert(hub_ai_complete_pending(&prior,id));
    assert(prior.task_count==1 && !strcmp(prior.tasks[0].task,"检查设备"));
    assert(prior.tasks[1].task[0]==0);
    assert(!hub_ai_complete_pending(&prior,id));
    assert(!hub_ai_complete_pending(&prior,0));
    /* Fingerprint collisions (including identical legacy rows) fail closed. */
    prior.task_count=2;
    prior.tasks[1]=prior.tasks[0];
    assert(!hub_ai_complete_pending(&prior,hub_ai_task_id(&prior.tasks[0])));
    assert(prior.task_count==2);
    /* A later known deadline improves the existing item without duplication. */
    prior.task_count=1;
    memset(&proposal,0,sizeof(proposal));
    strcpy(proposal.summary,"新摘要");
    strcpy(proposal.tasks[0].task,prior.tasks[0].task);
    strcpy(proposal.tasks[0].source,prior.tasks[0].source);
    strcpy(proposal.tasks[0].due,"周三17点");
    proposal.task_count=1;
    assert(hub_ai_merge_pending(&prior,&proposal,&out)==HUB_AI_MERGE_OK);
    assert(out.task_count==1 && !strcmp(out.tasks[0].due,"周三17点"));
    proposal.tasks[0].due[0]=0;
    assert(hub_ai_merge_pending(&out,&proposal,&prior)==HUB_AI_MERGE_OK);
    assert(!strcmp(prior.tasks[0].due,"周三17点"));
    /* No truncation of an overlong summary may pass validation. */
    memset(proposal.summary,'a',HUB_AI_SUMMARY_MAX_UTF8_BYTES+1);
    proposal.summary[HUB_AI_SUMMARY_MAX_UTF8_BYTES+1]=0;
    assert(hub_ai_merge_pending(NULL,&proposal,&out)==HUB_AI_MERGE_INVALID);
}

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
    assert(strstr(HUB_AI_SYSTEM_PROMPT,"此前未清除摘要"));
    assert(strstr(HUB_AI_SYSTEM_PROMPT,"不可信"));
    assert(strstr(HUB_AI_SYSTEM_PROMPT,"open_items"));
    assert(strstr(HUB_AI_SYSTEM_PROMPT,"JSON"));
    /* Sensitive content is rejected before a remote model sees the payload. */
    assert(hub_ai_sensitive("您的验证码","123456"));
    assert(hub_ai_sensitive("Your VERIFICATION CODE","843123"));
    assert(hub_ai_sensitive("One-Time Passcode","843123"));
    assert(hub_ai_sensitive("Reset PASSWORD","link"));
    assert(hub_ai_sensitive("Bank Card number","ending 34"));
    assert(hub_ai_sensitive("安全校验","请使用OTP"));
    assert(hub_ai_sensitive(NULL,"PIN 1234"));
    assert(!hub_ai_sensitive("Shipping notice","pickup at gate"));
    assert(!hub_ai_sensitive("Routine meeting","Schedule for Monday"));
    assert(HUB_AI_TRIGGER_COUNT==3);
    assert(!hub_ai_batch_ready(0,false));
    assert(!hub_ai_batch_ready(2,false));
    assert(hub_ai_batch_ready(3,false));
    assert(!hub_ai_batch_ready(0,true));
    assert(hub_ai_batch_ready(1,true));
    assert(HUB_AI_TASK_LIMIT==5);
    archive_control_event_t clear=hub_control_make(6);
    archive_control_event_t wire={0};
    assert(sizeof(clear)>1);
    memcpy(&wire,&clear,sizeof(wire));
    assert(wire.kind==6 && !wire.fingerprint && !wire.revision && !wire.id);
    clear=hub_control_make(9);
    clear.fingerprint=123;clear.revision=32;clear.id=7;
    memcpy(&wire,&clear,sizeof(wire));
    assert(wire.kind==9 && wire.fingerprint==123 && wire.revision==32 && wire.id==7);
    assert(!strstr(HUB_AI_SYSTEM_PROMPT,"示例：{"));

    assert(!hub_archive_gc_due(0,false));
    assert(!hub_archive_gc_due(63,false));
    assert(hub_archive_gc_due(64,false));
    assert(!hub_archive_gc_due(0,true));
    assert(hub_archive_gc_due(3,true));
    test_protected_pending();
    puts("Catalog, 3-message trigger, prompt and deterministic task retention: PASS");
    return 0;
}
