#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool hub_web_auth_credentials_match(const char *username,const char *password,
                                    const char *expected_username,
                                    const char *expected_password);
bool hub_web_auth_cookie_matches(const char *cookie_header,const char *token);
