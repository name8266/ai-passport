#pragma once
#include "esp_err.h"
/* Called only by the serialized sound worker. */
esp_err_t stock_sound_play(void);
