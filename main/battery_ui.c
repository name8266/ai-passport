#include "battery_ui.h"
#include "bsp_pins.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

#define INK 0xEAF3EE
#define MUTED 0x94A79D
#define GREEN 0xB8E986
#define BG 0x101C18
#define PANEL 0x20322A
static lv_obj_t *s_title,*s_battery,*s_clock,*s_heading,*s_subheading,*s_value;
static lv_obj_t *s_card,*s_bar,*s_detail,*s_action[3],*s_hint,*s_message;
static const uint32_t status_colors[]={GREEN,0x8CC5E8,0xF3CF75,0xED9C7B,0x94A79D};
static lv_obj_t *label(lv_obj_t *parent,int x,int y,int w,const lv_font_t *font,uint32_t color) {
    lv_obj_t *o=lv_label_create(parent); lv_obj_set_pos(o,x,y); lv_obj_set_width(o,w);
    lv_label_set_long_mode(o,LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(o,font,0); lv_obj_set_style_text_color(o,lv_color_hex(color),0);
    return o;
}
static void show(lv_obj_t *o,bool yes) {
    if (yes) lv_obj_remove_flag(o,LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(o,LV_OBJ_FLAG_HIDDEN);
}
void battery_ui_init(void) {
    lv_obj_t *root=lv_obj_create(NULL); lv_obj_remove_style_all(root);
    lv_obj_set_size(root,BSP_LCD_W,BSP_LCD_H); lv_obj_set_style_bg_color(root,lv_color_hex(BG),0);
    lv_obj_set_style_bg_opa(root,LV_OPA_COVER,0); lv_obj_remove_flag(root,LV_OBJ_FLAG_SCROLLABLE);
    s_title=label(root,22,17,140,&lv_font_montserrat_14,GREEN);
    s_battery=label(root,173,17,48,&lv_font_montserrat_14,MUTED);
    lv_obj_set_style_text_align(s_battery,LV_TEXT_ALIGN_RIGHT,0);
    s_clock=label(root,22,40,196,&lv_font_montserrat_14,MUTED);
    s_heading=label(root,22,66,196,&lv_font_montserrat_20,INK);
    s_subheading=label(root,22,94,196,&lv_font_montserrat_14,MUTED);
    s_card=lv_obj_create(root); lv_obj_remove_style_all(s_card);
    lv_obj_set_pos(s_card,16,121); lv_obj_set_size(s_card,208,129);
    lv_obj_set_style_bg_color(s_card,lv_color_hex(PANEL),0); lv_obj_set_style_bg_opa(s_card,LV_OPA_COVER,0);
    lv_obj_set_style_radius(s_card,16,0); lv_obj_remove_flag(s_card,LV_OBJ_FLAG_SCROLLABLE);
    s_value=label(s_card,14,12,180,&lv_font_montserrat_28,INK);
    s_bar=lv_bar_create(s_card); lv_obj_set_pos(s_bar,14,52); lv_obj_set_size(s_bar,180,8);
    lv_bar_set_range(s_bar,0,100); lv_obj_set_style_bg_color(s_bar,lv_color_hex(0x3B5145),LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_bar,lv_color_hex(GREEN),LV_PART_INDICATOR);
    s_detail=label(s_card,14,71,180,&lv_font_montserrat_14,MUTED);
    lv_label_set_long_mode(s_detail,LV_LABEL_LONG_WRAP);
    for (int i=0;i<3;++i) {
        s_action[i]=label(s_card,14,12+i*35,180,&lv_font_montserrat_14,INK);
        lv_obj_set_style_pad_all(s_action[i],7,0); lv_obj_set_style_radius(s_action[i],7,0);
    }
    s_message=label(root,22,257,196,&lv_font_montserrat_14,GREEN);
    s_hint=label(root,22,283,196,&lv_font_montserrat_14,MUTED);
    lv_label_set_long_mode(s_hint,LV_LABEL_LONG_WRAP);
    lv_screen_load(root);
}
unsigned battery_quick_actions(unsigned status,bat_action_t actions[3]) {
    switch (status) {
        case BAT_READY: actions[0]=BAT_CHECKOUT; actions[1]=BAT_CHARGE; actions[2]=BAT_INSPECT; return 3;
        case BAT_IN_USE: actions[0]=BAT_RETURN; return 1;
        case BAT_CHARGING: actions[0]=BAT_FINISH; return 1;
        case BAT_SERVICE: actions[0]=BAT_RELEASE; actions[1]=BAT_CHARGE; return 2;
        default: return 0;
    }
}
/* Dynamic UTF-8 asset names are deliberately represented by their stable ASCII ID
 * on the device. Full names remain in the browser; no glyphs are silently dropped. */
void battery_ui_render(const bat_db_t *db,const battery_ui_state_t *u) {
    lv_label_set_text(s_title,"BATTERY / DESK");
    if (u->device_soc<0) lv_label_set_text(s_battery,"--");
    else lv_label_set_text_fmt(s_battery,"%d%%",u->device_soc);
    if (u->epoch) {
        time_t local=(time_t)(u->epoch+u->timezone*60); struct tm tm;
        gmtime_r(&local,&tm); char text[48]; strftime(text,sizeof(text),"%m/%d   %H:%M  /  PHONE TIME",&tm);
        lv_label_set_text(s_clock,text);
    } else lv_label_set_text(s_clock,"TIME / Connect your phone");
    lv_label_set_text(s_message,u->message[0] ? u->message : (!u->writable ? "Storage unavailable" : ""));
    show(s_value,true); show(s_bar,true); show(s_detail,true);
    for (int i=0;i<3;++i) show(s_action[i],false);
    if (u->view==BAT_VIEW_WEB) {
        lv_label_set_text(s_heading,"Phone workspace");
        lv_label_set_text(s_subheading,u->web_running ? "Join Wi-Fi, then open URL" : "Private local workspace");
        show(s_bar,false); lv_obj_set_style_text_font(s_value,&lv_font_montserrat_14,0);
        if (u->web_running) {
            lv_label_set_text(s_value,u->ssid);
            lv_label_set_text_fmt(s_detail,"KEY  %s\n192.168.4.1\nTime sync on page open",u->password);
            lv_label_set_text(s_hint,"OK: stop Wi-Fi\nHold OK: back");
        } else {
            lv_label_set_text(s_value,"Your phone. Your assets.");
            lv_label_set_text(s_detail,"No cloud required.\nStart Wi-Fi to edit assets\nand sync the clock.");
            lv_label_set_text(s_hint,"OK: start Wi-Fi\nHold OK: back");
        }
        return;
    }
    lv_obj_set_style_text_font(s_value,&lv_font_montserrat_28,0);
    if (u->view==BAT_VIEW_HOME) {
        bat_summary_t summary; bat_summary(db,&summary);
        lv_label_set_text(s_heading,"Ready for today.");
        lv_obj_set_style_bg_color(s_bar,lv_color_hex(GREEN),LV_PART_INDICATOR);
        lv_label_set_text_fmt(s_subheading,"%u assets  /  %u need attention",db->count,summary.attention);
        lv_label_set_text_fmt(s_value,"%u ready",summary.statuses[BAT_READY]);
        lv_bar_set_value(s_bar,db->count ? summary.statuses[BAT_READY]*100/db->count : 0,LV_ANIM_OFF);
        lv_label_set_text_fmt(s_detail,"%u in use  /  %u charging\n%s",summary.statuses[BAT_IN_USE],
                              summary.statuses[BAT_CHARGING],db->count ? "OK to browse your collection" : "Add your first asset on phone");
        lv_label_set_text(s_hint,"OK: assets  /  DOWN: web\nHold OK: phone workspace");
        return;
    }
    if (!db->count) {
        lv_label_set_text(s_heading,"A fresh collection"); lv_label_set_text(s_subheading,"Make room for what powers you");
        lv_label_set_text(s_value,"0 assets"); lv_bar_set_value(s_bar,0,LV_ANIM_OFF);
        lv_label_set_text(s_detail,"Add battery details from\nyour phone workspace.");
        lv_label_set_text(s_hint,"OK: phone workspace\nHold OK: overview"); return;
    }
    unsigned index=u->selected<db->count ? u->selected : 0;
    const bat_asset_t *a=&db->assets[index];
    lv_label_set_text_fmt(s_heading,"BAT-%03lu",(unsigned long)a->id);
    lv_label_set_text_fmt(s_subheading,"%u / %u   %s",index+1,db->count,bat_status_name(a->status));
    if (u->view==BAT_VIEW_ACTIONS) {
        show(s_value,false); show(s_bar,false); show(s_detail,false);
        bat_action_t actions[3]; unsigned count=battery_quick_actions(a->status,actions);
        for (unsigned i=0;i<count;++i) {
            show(s_action[i],true); lv_label_set_text_fmt(s_action[i],"%s %s",i==u->action_index ? ">" : " ",bat_action_name(actions[i]));
            lv_obj_set_style_bg_opa(s_action[i],i==u->action_index ? LV_OPA_COVER : LV_OPA_TRANSP,0);
            lv_obj_set_style_bg_color(s_action[i],lv_color_hex(0x3B5145),0);
        }
        lv_label_set_text(s_hint,u->confirm ? "OK: confirm & save\nUP/DOWN: cancel  Hold: back" : "UP/DOWN: choose  OK: select\nHold OK: back");
        if (u->confirm) lv_label_set_text(s_message,"Confirm this operation?");
    } else {
        lv_label_set_text_fmt(s_value,"%u%%",a->soc); lv_bar_set_value(s_bar,a->soc,LV_ANIM_OFF);
        lv_obj_set_style_bg_color(s_bar,lv_color_hex(status_colors[a->status]),LV_PART_INDICATOR);
        lv_label_set_text_fmt(s_detail,"%u mAh  /  Health %u%%\n%u cycles  /  Details on phone",a->capacity_mah,a->health,a->cycles);
        lv_label_set_text(s_hint,"UP/DOWN: browse  OK: action\nHold OK: overview");
    }
}
