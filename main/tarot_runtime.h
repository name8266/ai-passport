#pragma once
#include <stdbool.h>
#include <stdint.h>
#define TAROT_DIM_AFTER_US (30LL*1000*1000)
#define TAROT_OFF_AFTER_US (120LL*1000*1000)
#define TAROT_BATTERY_REFRESH_US (60LL*1000*1000)
typedef enum{TAROT_IDLE_ACTIVE=0,TAROT_IDLE_DIMMED,TAROT_IDLE_OFF}tarot_idle_state_t;typedef struct{int64_t last_input_us;int64_t next_battery_refresh_us;tarot_idle_state_t idle_state;bool suppress_until_next_press;}tarot_runtime_t;void tarot_runtime_init(tarot_runtime_t*r,int64_t now);tarot_idle_state_t tarot_runtime_update_idle(tarot_runtime_t*r,int64_t now);bool tarot_runtime_note_input(tarot_runtime_t*r,int64_t now,bool press,bool*woke);bool tarot_runtime_battery_refresh_due(tarot_runtime_t*r,int64_t now);
