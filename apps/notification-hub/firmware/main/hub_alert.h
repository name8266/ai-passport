#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef struct {bool enabled;uint8_t tone,volume;} hub_sound_config_t;
#define HUB_TONE_COUNT 3
#define HUB_SOUND_RATE 16000
hub_sound_config_t hub_sound_defaults(void);
bool hub_sound_valid(hub_sound_config_t config);
bool hub_alert_eligible(hub_sound_config_t config,uint8_t event,uint8_t flags,uint64_t now,uint64_t last,bool has_last);
size_t hub_tone_samples(uint8_t tone);
int16_t hub_tone_sample(uint8_t tone,size_t index);
