#pragma once

#include <stdbool.h>

bool tarot_audio_init(void);
void tarot_audio_set_enabled(bool enabled);
void tarot_audio_play_reveal(void);
void tarot_audio_play_confirm(void);
