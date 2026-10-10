#include "hub_access.h"

bool hub_access_needs_migration(uint8_t version, bool password_present) {
    return version < HUB_ACCESS_CREDENTIAL_VERSION || !password_present;
}
