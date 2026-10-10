#pragma once
#include <stdbool.h>
#include <stddef.h>
typedef struct {char *data;size_t used,cap;bool overflow;} hub_response_t;
/* No allocation until the first body fragment, after the TLS handshake. */
bool hub_response_append(hub_response_t *response,const void *data,size_t length);
void hub_response_release(hub_response_t *response);
