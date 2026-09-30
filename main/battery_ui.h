#pragma once
#include "battery_care.h"
#include <stdbool.h>

typedef enum { BAT_VIEW_HOME, BAT_VIEW_ASSET, BAT_VIEW_ACTIONS, BAT_VIEW_WEB, BAT_VIEW_REMINDERS } bat_view_t;
typedef struct {
    bat_view_t view;
    unsigned selected, action_index;
    bool confirm, web_running, writable;
    int device_soc, timezone;
    int64_t epoch;
    care_info_t care;
    care_reminder_t selected_reminder;
    unsigned pet_boop;
    bool speaker_available;
    char ssid[32], password[13], message[64];
} battery_ui_state_t;
/* Called only while holding BSP LVGL lock. Single screen and fixed reusable widgets. */
void battery_ui_init(void);
void battery_ui_render(const bat_db_t *db,const battery_ui_state_t *state);
unsigned battery_quick_actions(unsigned status,bat_action_t actions[3]);
