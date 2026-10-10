#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
typedef struct {int unused;} lv_font_t;
typedef struct {bool is_placeholder;} lv_font_glyph_dsc_t;
typedef struct {char text[512];} lv_obj_t;
#define LV_FONT_DECLARE(name) extern const lv_font_t name
#define LV_PART_MAIN 0
static const lv_font_t mock_font={0};
static inline bool lv_font_get_glyph_dsc(const lv_font_t *f,lv_font_glyph_dsc_t *d,uint32_t cp,uint32_t next) {
    (void)f;(void)next;d->is_placeholder=cp>0xffff;return true;
}
static inline const lv_font_t *lv_obj_get_style_text_font(lv_obj_t *o,int part) {(void)o;(void)part;return &mock_font;}
extern unsigned ui_test_allocations,ui_test_updates;
static inline const char *lv_label_get_text(lv_obj_t *o) {return o->text;}
static inline void *lv_malloc(size_t n) {ui_test_allocations++;return malloc(n);}
static inline void lv_free(void *p) {free(p);}
static inline void lv_label_set_text(lv_obj_t *o,const char *s) {ui_test_updates++;strncpy(o->text,s,sizeof(o->text)-1);}
