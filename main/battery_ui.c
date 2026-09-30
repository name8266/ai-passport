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
static lv_obj_t *s_cat,*s_eyes[2],*s_collar,*s_hat;
static lv_obj_t *s_title,*s_battery,*s_clock,*s_heading,*s_subheading,*s_value;
static lv_obj_t *s_card,*s_bar,*s_detail,*s_action[3],*s_hint,*s_message;
static const uint32_t status_colors[]={GREEN,0x8CC5E8,0xF3CF75,0xED9C7B,0x94A79D};
static lv_obj_t *label(lv_obj_t *parent,int x,int y,int w,const lv_font_t *font,uint32_t color) {
    lv_obj_t *o=lv_label_create(parent); lv_obj_set_pos(o,x,y); lv_obj_set_width(o,w);
    lv_label_set_long_mode(o,LV_LABEL_LONG_DOT);
    lv_obj_set_height(o,font->line_height);
    lv_obj_set_style_text_font(o,font,0); lv_obj_set_style_text_color(o,lv_color_hex(color),0);
    return o;
}
static void value_font(const lv_font_t *font) {
    lv_obj_set_style_text_font(s_value,font,0);lv_obj_set_height(s_value,font->line_height);
}
static void show(lv_obj_t *o,bool yes) {
    if (yes) lv_obj_remove_flag(o,LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(o,LV_OBJ_FLAG_HIDDEN);
}
static lv_obj_t *shape(lv_obj_t *parent,int x,int y,int w,int h,int radius,uint32_t color) {
    lv_obj_t *o=lv_obj_create(parent);lv_obj_remove_style_all(o);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);
    lv_obj_set_style_radius(o,radius,0);lv_obj_set_style_bg_color(o,lv_color_hex(color),0);
    lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE);return o;
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
    lv_obj_set_height(s_detail,54);
    for (int i=0;i<3;++i) {
        s_action[i]=label(s_card,14,12+i*35,180,&lv_font_montserrat_14,INK);
        lv_obj_set_height(s_action[i],lv_font_montserrat_14.line_height+14);
        lv_obj_set_style_pad_all(s_action[i],7,0); lv_obj_set_style_radius(s_action[i],7,0);
    }
    s_message=label(root,22,257,196,&lv_font_montserrat_14,GREEN);
    s_hint=label(root,22,283,196,&lv_font_montserrat_14,MUTED);
    lv_label_set_long_mode(s_hint,LV_LABEL_LONG_WRAP);
    lv_obj_set_height(s_hint,36);
    s_cat=lv_obj_create(root);lv_obj_remove_style_all(s_cat);lv_obj_set_pos(s_cat,54,110);lv_obj_set_size(s_cat,132,90);
    lv_obj_remove_flag(s_cat,LV_OBJ_FLAG_SCROLLABLE);
    shape(s_cat,15,2,30,44,10,0x8292A5);shape(s_cat,88,2,30,44,10,0x8292A5);
    shape(s_cat,21,9,16,25,8,0xB2AAB7);shape(s_cat,96,9,16,25,8,0xB2AAB7);
    shape(s_cat,12,16,110,71,35,0x94A5B8);
    for(int i=0;i<2;++i) { shape(s_cat,35+i*49,39,15,16,8,0xE8B45B);s_eyes[i]=shape(s_cat,41+i*49,41,4,12,2,0x20323E); }
    shape(s_cat,61,61,11,6,3,0xDDACB8);shape(s_cat,51,72,11,2,1,0x526475);shape(s_cat,73,72,11,2,1,0x526475);
    s_collar=shape(s_cat,42,83,51,5,2,GREEN);s_hat=shape(s_cat,48,4,37,7,2,0xE8B45B);
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
    show(s_cat,u->view==BAT_VIEW_HOME);
    lv_label_set_text(s_title,"BATTERY / DESK");
    if (u->device_soc<0) lv_label_set_text(s_battery,"--");
    else lv_label_set_text_fmt(s_battery,"%d%%",u->device_soc);
    if (u->epoch) {
        time_t local=(time_t)(u->epoch+u->timezone*60); struct tm tm;
        gmtime_r(&local,&tm); char text[48]; strftime(text,sizeof(text),"%m/%d  %H:%M / PHONE",&tm);
        lv_label_set_text(s_clock,text);
    } else lv_label_set_text(s_clock,"TIME / Connect phone");
    lv_label_set_text(s_message,u->message[0] ? u->message : (!u->writable ? "Storage unavailable" : ""));
    lv_obj_set_pos(s_card,16,121);lv_obj_set_height(s_card,129);
    lv_obj_set_pos(s_value,14,12);lv_obj_set_pos(s_bar,14,52);
    show(s_value,true); show(s_bar,true); show(s_detail,true);
    for (int i=0;i<3;++i) show(s_action[i],false);
    if (u->view==BAT_VIEW_WEB) {
        lv_label_set_text(s_heading,"Phone workspace");
        lv_label_set_text(s_subheading,u->web_running ? "Join Wi-Fi, then open URL" : "Private local workspace");
        show(s_bar,false); value_font(&lv_font_montserrat_14);
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
    value_font(&lv_font_montserrat_28);
    if(u->view==BAT_VIEW_REMINDERS) {
        show(s_bar,false);value_font(&lv_font_montserrat_20);
        lv_label_set_text(s_heading,"A gentle reminder");
        lv_label_set_text_fmt(s_subheading,"%lu waiting / %s",(unsigned long)u->care.due_count,
                              u->speaker_available ? "sound ready" : "speaker unavailable");
        if(u->selected_reminder.id) {
            const care_reminder_t *r=&u->selected_reminder;
            lv_label_set_text_fmt(s_value,"BAT-%03lu",(unsigned long)r->id);
            lv_label_set_text_fmt(s_detail,"%s\nCheck and update on phone",care_reason_name(r->reason));
        } else { lv_label_set_text(s_value,"All cared for");lv_label_set_text(s_detail,"Blue is happy to see you.\nYour next check will appear here."); }
        lv_label_set_text(s_hint,"UP/DOWN: browse\nOK: snooze  Hold: home");return;
    }
    if (u->view==BAT_VIEW_HOME) {
        lv_label_set_text(s_heading,"Blue / your buddy");
        lv_label_set_text_fmt(s_subheading,"%lu assets  /  %lu reminders",(unsigned long)u->care.total,(unsigned long)u->care.due_count);
        lv_obj_set_pos(s_card,16,199);lv_obj_set_height(s_card,52);
        value_font(&lv_font_montserrat_14);lv_obj_set_pos(s_value,14,8);
        lv_label_set_text_fmt(s_value,"Level %u  /  %lu care points",care_stage(&u->care.pet)+1,(unsigned long)u->care.pet.xp);
        lv_obj_set_pos(s_bar,14,33);lv_bar_set_value(s_bar,care_stage(&u->care.pet)==3 ? 100 : u->care.pet.xp%60*100/60,LV_ANIM_OFF);
        show(s_detail,false);show(s_collar,care_stage(&u->care.pet)>0);show(s_hat,care_stage(&u->care.pet)>1);
        for(int i=0;i<2;++i)lv_obj_set_height(s_eyes[i],u->pet_boop ? 3 : 12);
        lv_obj_set_y(s_cat,u->pet_boop ? 108 : 110);
        lv_label_set_text(s_hint,"UP: care  DOWN: assets\nOK: pet  Hold: web");return;
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
        lv_label_set_text(s_hint,u->confirm ? "OK: confirm & save\nUP/DOWN: cancel  Hold: back" : "UP/DOWN: choose\nOK: select  Hold: back");
        if (u->confirm) lv_label_set_text(s_message,"Confirm this operation?");
    } else {
        lv_label_set_text_fmt(s_value,"%u%%",a->soc); lv_bar_set_value(s_bar,a->soc,LV_ANIM_OFF);
        lv_obj_set_style_bg_color(s_bar,lv_color_hex(status_colors[a->status]),LV_PART_INDICATOR);
        lv_label_set_text_fmt(s_detail,"%u mAh  /  Health %u%%\n%u cycles  /  Details on phone",a->capacity_mah,a->health,a->cycles);
        lv_label_set_text(s_hint,"UP/DOWN: browse\nOK: action  Hold: home");
    }
}
