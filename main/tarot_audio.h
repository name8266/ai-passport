#pragma once
#include <stdbool.h>
#include <stddef.h>

bool tarot_audio_init(void);
void tarot_audio_set_enabled(bool enabled);
void tarot_audio_play_reveal(void);
void tarot_audio_play_confirm(void);
void tarot_audio_suspend(void);
void tarot_audio_resume(void);
size_t tarot_audio_stack_high_water(void);
