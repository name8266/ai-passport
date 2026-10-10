#include <assert.h>
#include <stdio.h>
#include "hub_ui.h"
unsigned ui_test_allocations,ui_test_updates;
int main(void) {
    const hub_ui_palette_t *light=hub_ui_palette(false),*dark=hub_ui_palette(true);
    assert(light->background==0xf2f2f7 && light->surface==0xffffff);
    assert(dark->background==0x000000 && dark->surface==0x1c1c1e);
    assert(light->primary!=light->background && dark->primary!=dark->background);
    assert(light->accent!=light->surface && dark->accent!=dark->surface);
    lv_obj_t label={0};
    hub_ui_set_text(&label,"中文\nHello");assert(!strcmp(label.text,"中文\nHello"));
    unsigned allocations=ui_test_allocations,updates=ui_test_updates;
    for(unsigned i=0;i<1000;i++) hub_ui_set_text(&label,"中文\nHello");
    assert(ui_test_allocations==allocations && ui_test_updates==updates);
    hub_ui_set_text(&label,"消息😀结束");assert(!strcmp(label.text,"消息?结束"));
    updates=ui_test_updates;
    hub_ui_set_text(&label,"消息😀结束");assert(ui_test_updates==updates);
    hub_ui_set_text(&label,"A\xc0\xaf" "B");assert(!strcmp(label.text,"A??B"));
    hub_ui_set_text(&label,"\xed\xa0\x80");assert(!strcmp(label.text,"?"));
    hub_ui_set_text(&label,"\xe4\xb8");assert(!strcmp(label.text,"??"));
    hub_ui_set_text_fmt(&label,"第 %d 条：%s",7,"中文");assert(!strcmp(label.text,"第 7 条：中文"));
    assert(hub_ui_font_check(&mock_font));
    puts("Chinese text preservation, missing glyph fallback and malformed UTF-8: PASS");
}
