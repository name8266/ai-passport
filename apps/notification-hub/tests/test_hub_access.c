#include "hub_access.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    assert(hub_access_needs_migration(0,false)); /* Fresh install */
    assert(hub_access_needs_migration(0,true));  /* beta2 generated password */
    assert(hub_access_needs_migration(1,true));  /* beta4 pre-release credential state */
    assert(!hub_access_needs_migration(HUB_ACCESS_CREDENTIAL_VERSION,true));
    assert(hub_access_needs_migration(HUB_ACCESS_CREDENTIAL_VERSION,false));
    assert(!hub_access_needs_migration(HUB_ACCESS_CREDENTIAL_VERSION+1,true));
    assert(sizeof(HUB_ACCESS_DEFAULT_PASSWORD) <= 17);
    puts("Admin password first-boot and upgrade migration: PASS");
}
