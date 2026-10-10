#include "hub_ui.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

const hub_ui_palette_t *hub_ui_palette(bool dark_mode) {
    static const hub_ui_palette_t light={
        .background=0xf2f2f7,.surface=0xffffff,.surface_alt=0xfafaff,
        .primary=0x1c1c1e,.secondary=0x8e8e93,.accent=0x007aff,
        .selected=0xe5f0ff,.success=0x34c759,.danger=0xff3b30,
        .separator=0xd1d1d6
    };
    static const hub_ui_palette_t dark_colors={
        .background=0x000000,.surface=0x1c1c1e,.surface_alt=0x2c2c2e,
        .primary=0xf5f5f7,.secondary=0x98989d,.accent=0x0a84ff,
        .selected=0x173454,.success=0x30d158,.danger=0xff453a,
        .separator=0x38383a
    };
    return dark_mode?&dark_colors:&light;
}

bool hub_ui_font_check(const lv_font_t *font) {
    static const uint32_t fixed[]={
#include "hub_ui_fixed_glyphs.inc"
    };
    for(size_t i=0;i<sizeof(fixed)/sizeof(fixed[0]);i++) {
        lv_font_glyph_dsc_t d={0};
        if(!lv_font_get_glyph_dsc(font,&d,fixed[i],0) || d.is_placeholder) return false;
    }
    return true;
}
void hub_ui_set_text(lv_obj_t *label,const char *text) {
    if(!text) text="";
    if(!strcmp(lv_label_get_text(label),text)) return;
    size_t len=strlen(text),i=0,out=0;
    char *clean=lv_malloc(len+1);
    if(!clean) {lv_label_set_text(label,"内存不足");return;}
    const lv_font_t *font=lv_obj_get_style_text_font(label,LV_PART_MAIN);
    while(i<len) {
        size_t start=i;uint32_t cp=(unsigned char)text[i++];unsigned extra=0;
        if(cp>=0xc2 && cp<=0xdf) {extra=1;cp&=0x1f;}
        else if(cp>=0xe0 && cp<=0xef) {extra=2;cp&=0x0f;}
        else if(cp>=0xf0 && cp<=0xf4) {extra=3;cp&=7;}
        else if(cp>=0x80) cp=0xffffffff;
        bool valid=cp!=0xffffffff && i+extra<=len;
        for(unsigned n=0;valid && n<extra;n++) {
            unsigned char ch=text[i];
            if((ch&0xc0)!=0x80) {valid=false;break;}
            cp=(cp<<6)|(ch&0x3f);i++;
        }
        if((extra==1 && cp<0x80) || (extra==2 && cp<0x800) ||
           (extra==3 && cp<0x10000) || cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff)) valid=false;
        lv_font_glyph_dsc_t d={0};
        bool supported=valid && (cp=='\n' || cp=='\r' ||
            (lv_font_get_glyph_dsc(font,&d,cp,0) && !d.is_placeholder));
        if(supported) {memcpy(clean+out,text+start,i-start);out+=i-start;}
        else clean[out++]='?';
    }
    clean[out]=0;
    if(strcmp(lv_label_get_text(label),clean)) lv_label_set_text(label,clean);
    lv_free(clean);
}
void hub_ui_set_text_fmt(lv_obj_t *label,const char *fmt,...) {
    va_list args,copy;va_start(args,fmt);va_copy(copy,args);
    int len=vsnprintf(NULL,0,fmt,copy);va_end(copy);
    if(len<0) {va_end(args);return;}
    char *text=lv_malloc((size_t)len+1);
    if(text) {vsnprintf(text,(size_t)len+1,fmt,args);hub_ui_set_text(label,text);lv_free(text);}
    else lv_label_set_text(label,"内存不足");
    va_end(args);
}
