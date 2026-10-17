#pragma once
#include <stdint.h>
#include "pusher_records.h"
#define CP_FX_COIN 1u
#define CP_FX_PRIZE 2u
#define CP_FX_JACKPOT 4u
#define CP_FX_RESET 8u
#define CP_FX_EVENT 16u
#define CP_FX_SPIN 32u
#define CP_FX_HOLE 64u
void cp_fx_start(void);
void cp_fx_notify(uint32_t effects);

int cp_fx_battery(void);

void cp_fx_store(const cp_records_t *board);
uint32_t cp_fx_saved_generation(void);
bool cp_fx_save_failed(void);

void cp_fx_mute(bool muted);
bool cp_fx_muted(void);
