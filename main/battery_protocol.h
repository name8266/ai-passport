#pragma once
#include <stdbool.h>
#include <stddef.h>
/* Conservative pre-parse limit for bounded flat JSON mutation payloads. */
bool battery_payload_safe(const char *data,size_t length);
