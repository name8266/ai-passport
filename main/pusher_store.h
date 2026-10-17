#pragma once
#include "pusher_records.h"
/* Load before renderer/audio allocations; writing belongs to the FX worker. */
bool cp_store_load(cp_records_t *board);
bool cp_store_write(const cp_records_t *board);

/* A separate NVS key leaves the leaderboard schema unchanged. */
bool cp_store_muted(void);
bool cp_store_set_muted(bool muted);
