#pragma once
#include "battery_model.h"
#define CARE_REMINDERS 8
#define CARE_DAILY_XP 40

typedef enum { CARE_NONE, CARE_LOW, CARE_CHARGE_DUE, CARE_SCHEDULED, CARE_STALE } care_reason_t;
typedef struct {
    bat_asset_t asset;
    int64_t updated_at, remind_at, charge_until, snooze_until;
    int32_t reward_day;
    uint32_t reward_mask;
    uint16_t charge_minutes,reserved;
    uint32_t checksum;
} care_asset_t;
typedef struct {
    uint32_t xp, streak;
    int32_t last_day, reward_day;
    uint16_t daily_xp;
    uint8_t volume, quiet_start, quiet_end, muted;
    int64_t snooze_until;
} care_pet_t;
typedef struct { uint32_t id; int64_t due_at; uint8_t reason; } care_reminder_t;
typedef struct {
    uint32_t total, counts[BAT_STATUS_COUNT], due_count, attention;
    uint32_t revision, next_cursor, page_after;
    care_pet_t pet;
    care_reminder_t reminders[CARE_REMINDERS];
    uint8_t reminder_count;
    uint64_t storage_used, storage_total;
} care_info_t;

care_reason_t care_due(const care_asset_t *a,int64_t now,int64_t *at);
unsigned care_reward(care_pet_t *pet,care_asset_t *a,bat_action_t action,bool meaningful,int64_t now,int tz);
bool care_quiet(const care_pet_t *pet,int64_t now,int tz);
unsigned care_stage(const care_pet_t *pet);
const char *care_reason_name(unsigned reason);
