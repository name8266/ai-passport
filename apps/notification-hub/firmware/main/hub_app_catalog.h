#pragma once
/* Fixed, allocation-free app name and high-level category catalog.
 * Bundle IDs remain the archive identity so renames never mix histories.
 * Unknown bundle IDs remain browsable using their final identifier segment.
 */
#include <string.h>
#include <stddef.h>
typedef struct { const char *id,*name,*category; } hub_app_entry_t;
static inline const hub_app_entry_t *hub_catalog_find(const char *id) {
    static const hub_app_entry_t catalog[]={
        {"Unresolved","未识别应用","未识别"},
        {"com.tencent.xin","微信","社交"},
        {"com.tencent.mqq","QQ","社交"},
        {"net.whatsapp.WhatsApp","WhatsApp","社交"},
        {"ph.telegra.Telegraph","Telegram","社交"},
        {"org.telegram.Telegram","Telegram","社交"},
        {"com.facebook.Messenger","Messenger","社交"},
        {"com.apple.MobileSMS","短信","社交"},
        {"com.apple.mobilephone","电话","社交"},
        {"com.bytedance.feishu","飞书","工作"},
        {"com.larksuite.suite","飞书","工作"},
        {"com.alibaba.DingTalk","钉钉","工作"},
        {"com.laiwang.DingTalk","钉钉","工作"},
        {"com.tencent.WeWork","企业微信","工作"},
        {"com.microsoft.skype.teams","Teams","工作"},
        {"com.microsoft.Office.Outlook","Outlook","工作"},
        {"com.google.Gmail","Gmail","工作"},
        {"com.apple.mobilemail","邮件","工作"},
        {"com.apple.mobilecal","日历","工作"},
        {"com.apple.reminders","提醒事项","工作"},
        {"com.apple.mobilenotes","备忘录","工作"},
        {"com.alipay.iphoneclient","支付宝","金融"},
        {"com.tencent.WeChat.Pay","微信支付","金融"},
        {"group.za.bank","ZA Bank","金融"},
        {"com.icbc.iphoneclient","工商银行","金融"},
        {"com.ccb.ccbApp","建设银行","金融"},
        {"com.cmbchina.MPBBank","招商银行","金融"},
        {"com.taobao.taobao4iphone","淘宝","购物"},
        {"com.360buy.jdmobile","京东","购物"},
        {"com.xunmeng.pinduoduo","拼多多","购物"},
        {"com.xiaomi.mihome","米家","生活"},
        {"com.meituan.imeituan","美团","生活"},
        {"com.sankuai.meituan.takeoutnew","美团外卖","生活"},
        {"com.didi.DiDi","滴滴出行","出行"},
        {"com.autonavi.amap","高德地图","出行"},
        {"com.baidu.map","百度地图","出行"},
        {"com.apple.Maps","地图","出行"},
        {"com.ss.iphone.ugc.Aweme","抖音","娱乐"},
        {"tv.danmaku.bilianime","哔哩哔哩","娱乐"},
        {"com.sina.weibo","微博","娱乐"},
        {"com.apple.Preferences","系统设置","系统"},
        {"com.apple.AppStore","App Store","系统"},
    };
    if(!id || !*id)return &catalog[0];
    for(size_t i=0;i<sizeof(catalog)/sizeof(catalog[0]);i++)
        if(strcmp(id,catalog[i].id)==0)return &catalog[i];
    return NULL;
}
static inline const char *hub_catalog_display(const char *id) {
    const hub_app_entry_t *entry=hub_catalog_find(id);
    if(entry)return entry->name;
    if(!id || !*id)return "未识别应用";
    const char *last=strrchr(id,'.');
    return last && last[1]?last+1:id;
}
static inline const char *hub_catalog_category(const char *id) {
    const hub_app_entry_t *entry=hub_catalog_find(id);
    if(entry)return entry->category;
    if(!id)return "其他";
    if(strncmp(id,"com.apple.",10)==0)return "系统";
    if(strncmp(id,"com.microsoft.",14)==0 ||
       strncmp(id,"com.google.",11)==0)return "工作";
    return "其他";
}
