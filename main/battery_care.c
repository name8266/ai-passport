#include "battery_care.h"

care_reason_t care_due(const care_asset_t *a,int64_t now,int64_t *at) {
    *at=0;
    if (a->asset.status==BAT_RETIRED) return CARE_NONE;
    care_reason_t reason=CARE_NONE;
    if (a->asset.soc<=20 && a->asset.status!=BAT_CHARGING) reason=CARE_LOW;
    if (a->asset.status==BAT_CHARGING) { *at=a->charge_until; reason=CARE_CHARGE_DUE; }
    if (a->remind_at && (reason==CARE_NONE || (*at && a->remind_at<*at))) { *at=a->remind_at; reason=CARE_SCHEDULED; }
    int64_t stale=a->updated_at ? a->updated_at+7*86400LL : 0;
    if (stale && (reason==CARE_NONE || (*at && stale<*at))) { *at=stale; reason=CARE_STALE; }
    (void)now; return reason;
}
unsigned care_reward(care_pet_t *p,care_asset_t *a,bat_action_t action,bool changed,int64_t now,int tz) {
    if (!now) return 0;
    unsigned amount=action==BAT_FINISH ? 10 : action==BAT_RELEASE ? 8 : action==BAT_EDIT && changed ? 5 : action==BAT_RETURN ? 2 : 0;
    if (!amount) return 0;
    int32_t day=(int32_t)((now+tz*60)/86400);
    if (a->reward_day!=day) { a->reward_day=day; a->reward_mask=0; }
    if (p->reward_day!=day) { p->reward_day=day; p->daily_xp=0; }
    uint32_t mask=1u<<(unsigned)action;
    if ((a->reward_mask&mask) || p->daily_xp>=CARE_DAILY_XP) return 0;
    if (amount>(unsigned)CARE_DAILY_XP-p->daily_xp) amount=(unsigned)CARE_DAILY_XP-p->daily_xp;
    a->reward_mask|=mask; p->daily_xp+=amount; p->xp+=amount;
    if (p->last_day!=day) { p->streak=p->last_day==day-1 ? p->streak+1 : 1; p->last_day=day; }
    return amount;
}
bool care_quiet(const care_pet_t *p,int64_t now,int tz) {
    if (p->muted || !p->volume || !now || now<p->snooze_until) return true;
    unsigned hour=(unsigned)(((now+tz*60)/3600)%24);
    if (p->quiet_start==p->quiet_end) return false;
    return p->quiet_start<p->quiet_end ? hour>=p->quiet_start && hour<p->quiet_end : hour>=p->quiet_start || hour<p->quiet_end;
}
unsigned care_stage(const care_pet_t *p) { unsigned stage=p->xp/60; return stage>3 ? 3 : stage; }
const char *care_reason_name(unsigned r) {
    static const char *names[]={"No reminder","Low charge","Check charging","Scheduled care","Update status"};
    return r<=CARE_STALE ? names[r] : "Unknown";
}
