#pragma once
#include "hub_alert.h"
bool hub_sound_start(void);
hub_sound_config_t hub_sound_get(void);
void hub_sound_set(hub_sound_config_t config);
void hub_sound_notify(uint8_t event,uint8_t flags);
void hub_sound_preview(void);
bool hub_sound_error(void);
/* Single AI worker holds this through TLS cleanup. Sound reminders coalesce
 * while held and play afterwards; no I2S DMA allocation during TLS. */
bool hub_sound_network_begin(void);
void hub_sound_network_end(void);
