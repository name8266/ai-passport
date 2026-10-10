#pragma once

#include <stdbool.h>
#include <stdint.h>

#define HUB_ACCESS_CREDENTIAL_VERSION 2u
#define HUB_ACCESS_DEFAULT_PASSWORD "admin"

/* First install and pre-beta4 credentials migrate to the public beta default.
 * Beta5 also normalizes beta4's one-time migration state; later versions keep
 * an initialized credential. */
bool hub_access_needs_migration(uint8_t version, bool password_present);
