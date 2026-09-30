#pragma once
#include <stdbool.h>
/* Queue only. A single worker owns all blocking BSP audio operations. */
bool battery_sound_init(void);
void battery_sound_notify(bool reward,unsigned volume);
bool battery_sound_available(void);
