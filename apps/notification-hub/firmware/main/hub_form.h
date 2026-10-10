#pragma once
#include "hub_ai.h"
/* Parse the complete browser form; reject malformed or oversized fields. */
bool hub_form_parse(const char *body, const char *nonce,
                    hub_ai_settings_t *settings, hub_ai_connection_t *wifi);
bool hub_form_confirm_archive_clear(const char *body,const char *nonce);
bool hub_form_validate_csrf(const char *body,const char *nonce);
/* Strict decimal IDs and CSRF for completing a single digest item. */
bool hub_form_parse_task_complete(const char *body,const char *nonce,
                                  uint32_t *fingerprint,uint32_t *revision);
bool hub_form_parse_login(const char *body,char *username,size_t username_size,
                          char *password,size_t password_size);
/* 0..2: up/down/OK; 3..5: their long presses; 6: sound preview. */
bool hub_form_parse_control(const char *body,const char *nonce,unsigned *action);
