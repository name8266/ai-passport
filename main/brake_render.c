#include "brake_render.h"
#include "brake_assets.h"
#include "brake_vehicle.h"
#include <stdio.h>
#include <string.h>

static int clip_top,clip_bottom=BH_H;

static void rect(uint8_t *p,int x,int y,int w,int h,uint8_t c) {
    if(x<0){w+=x;x=0;} if(y<0){h+=y;y=0;}
    if(y<clip_top){h-=clip_top-y;y=clip_top;}
    if(x+w>BH_W)w=BH_W-x;
    if(y+h>BH_H)h=BH_H-y;
    if(y+h>clip_bottom)h=clip_bottom-y;
    if(w<=0||h<=0)return;
    for(int j=0;j<h;++j)memset(p+(y+j)*BH_W+x,c,(size_t)w);
}
static void sprite(uint8_t *p,const bh_sprite_t *s,int x,int y,int w,int h,bool keyed) {
    if(w<1||h<1)return;
    int x0=x<0?0:x, y0=y<0?0:y, x1=x+w>BH_W?BH_W:x+w, y1=y+h>BH_H?BH_H:y+h;
    if(y0<clip_top)y0=clip_top;
    if(y1>clip_bottom)y1=clip_bottom;
    int map[BH_W];
    for(int xx=x0;xx<x1;++xx)map[xx]=(xx-x)*s->width/w;
    for(int yy=y0;yy<y1;++yy) {
        int sy=(yy-y)*s->height/h;
        for(int xx=x0;xx<x1;++xx) {
            uint8_t c=s->pixels[sy*s->width+map[xx]];
            if(!keyed||c)p[yy*BH_W+xx]=c;
        }
    }
}
static const bh_glyph_t *glyph(uint32_t c) {
    unsigned lo=0,hi=bh_glyph_count;
    while(lo<hi){unsigned mid=(lo+hi)/2;if(bh_glyphs[mid].codepoint<c)lo=mid+1;else hi=mid;}
    return lo<bh_glyph_count&&bh_glyphs[lo].codepoint==c?&bh_glyphs[lo]:NULL;
}
bool bh_font_has(uint32_t c) { return glyph(c)!=NULL; }
static uint32_t utf8(const unsigned char **s) {
    uint32_t c=*(*s)++;
    if(c<128)return c;
    unsigned n=c<224?1:c<240?2:3;c&=(1u<<(6-n))-1;
    for(unsigned i=0;i<n;++i)c=(c<<6)|(*(*s)++&63);
    return c;
}
static void text(uint8_t *p,int x,int y,const char *s,uint8_t color) {
    const unsigned char *t=(const unsigned char*)s;
    while(*t) {
        const bh_glyph_t *g=glyph(utf8(&t));
        if(!g)g=glyph('?');
        if(!g)return;
        for(int yy=0;yy<14;++yy)for(int xx=0;xx<g->width;++xx)
            if(g->bitmap[yy*2+xx/8]&(0x80>>(xx%8)))rect(p,x+xx,y+yy,1,1,color);
        x+=g->width+1;
    }
}
static void number(uint8_t *p,int x,int y,int value,int scale,uint8_t color) {
    static const uint8_t masks[]={63,6,91,79,102,109,125,7,127,111};
    char buf[12];snprintf(buf,sizeof(buf),"%d",value);
    for(const char *t=buf;*t;++t) {
        unsigned m=masks[*t-'0'];int a=scale,b=5*scale;
        if(m&1)rect(p,x+a,y,b,a,color);
        if(m&2)rect(p,x+b+a,y+a,a,b,color);
        if(m&4)rect(p,x+b+a,y+b+2*a,a,b,color);
        if(m&8)rect(p,x+a,y+2*b+2*a,b,a,color);
        if(m&16)rect(p,x,y+b+2*a,a,b,color);
        if(m&32)rect(p,x,y+a,a,b,color);
        if(m&64)rect(p,x+a,y+b+a,b,a,color);
        x+=9*scale;
    }
}
static void header(uint8_t *p,const bh_view_t *v,const char *title) {
    text(p,18,12,title,AMBER);
    char b[16]; if(v->battery>=0)snprintf(b,sizeof(b),"%d%%",v->battery);else strcpy(b,"--");
    text(p,191,13,b,MUTED);rect(p,16,34,208,1,STEEL);
}
static void button(uint8_t *p,int y,const char *label,bool active) {
    rect(p,18,y,204,30,active?GOLD:PANEL);
    if(active)rect(p,18,y,3,30,AMBER);
    text(p,33,y+8,label,active?WHITE:MUTED);
}
static void hud(uint8_t *p,int y,const char *label,int value,int max,uint8_t color) {
    char b[48];snprintf(b,sizeof(b),"%s %d%s",label,value,max==1600?" N":"%");
    text(p,18,y,b,color);rect(p,18,y+16,204,3,PANEL);
    if(value>max)value=max;
    if(value<0)value=0;
    rect(p,18,y+16,204*value/max,3,color);
}
static void scene(uint8_t *p,const bh_view_t *v) {
    clip_top=72;clip_bottom=248;
    const bh_state_t *s=&v->car;
    int dive=0;
    int shake=0;
    int impact_shake=0;
    if(v->page==BH_CRASH && s->obstacle!=1) {
        int force=s->obstacle==2?6:3;
        impact_shake=v->animation<12?(v->animation&1?force:-force):0;
        shake+=impact_shake;
    }
    int horizon=100+dive;
    sprite(p,&bh_horizon,0,72,240,42,false);
    sprite(p,&bh_mountains,0,78,240,36,true);
    const int pos=(int)(s->position*1000);
    /* Reciprocal lookup indexed by screen row; initialized once, no per-pixel
     * floating point, allocation, texture decompression, or road reset. */
    static int depth[150];
    if(!depth[1])for(int i=1;i<150;++i)depth[i]=150000/i;
    for(int y=horizon+1;y<248;++y) {
        int dy=y-horizon,z=depth[dy],half=dy*11/10;
        int world=pos+z;
        uint8_t grass=((world/4000)&1)?GRASS:SKY;
        rect(p,0,y,240,1,grass);
        int left=120-half+shake,right=120+half+shake;
        /* Flat road scanlines remove the old per-pixel texture divisions.
         * World-space lane stripes and guardrails still convey exact travel. */
        rect(p,left,y,right-left+1,1,((world/4500)&1)?ROAD:STEEL);
        rect(p,left,y,2,1,LINE);rect(p,right-1,y,2,1,LINE);
        if(world%12000<5500) {
            rect(p,120-half/3+shake,y,dy>60?2:1,1,WHITE);
            rect(p,120+half/3+shake,y,dy>60?2:1,1,WHITE);
        }
        // Alternating guardrail sleepers move at the same world displacement.
        uint8_t rail=bh_guardrail.pixels[(11+(world/2500)%6)*96+(world/80)%96];
        if(!rail)rail=STEEL;
        rect(p,left-8,y,5,1,rail);
        rect(p,right+3,y,5,1,rail);
    }
    for(int i=1;i<=13;++i) {
        int z=i*16000-(pos%16000);
        if(z<1400)continue;
        int yy=horizon+150000/z,hw=165000/z;
        int height=580000/z;
        for(int sign=-1;sign<=1;sign+=2) {
            int x=120+sign*(hw+hw/3)+shake;
            int width=height*2/7;if(width<1)width=1;
            sprite(p,&bh_lamp,x-width/2,yy-height,width,height,true);
        }
    }
    for(int i=1;i<=12;++i) {
        int z=i*24000-((pos+8000)%24000);
        if(z<1600)continue;
        int yy=horizon+150000/z,height=680000/z,width=height*8/13;
        if(width<1)width=1;
        int side=330000/z;
        sprite(p,&bh_tree,120-side-width/2+shake,yy-height,width,height,true);
        sprite(p,&bh_tree,120+side-width/2+shake,yy-height,width,height,true);
    }
    int distance=(int)(s->remaining*1000);if(distance<1600)distance=1600;
    int ow=600000/distance;if(ow<2)ow=2;if(ow>270)ow=270;
    int bottom=horizon+150000/distance;
    const bh_sprite_t *obs[]={&bh_wall,&bh_cliff,&bh_truck,&bh_construction};
    int oh=s->obstacle==2?ow*6/5:ow*2/3;
    sprite(p,obs[s->obstacle%4],120-ow/2+shake,bottom-oh,ow,oh,true);
    bh_vehicle_draw(p,v);
    if(v->page==BH_CRASH) {
        if(s->obstacle==1) {
            int fall=(int)v->animation*4;if(fall>120)fall=120;
            rect(p,0,72,240,fall,BLACK);
        } else {
            if(s->obstacle==2 && v->animation%8<4) {
                rect(p,35,160,20,5,RED);rect(p,185,160,20,5,RED);
            }
            if(s->obstacle==3) {
                int fall=(int)v->animation*3;
                sprite(p,&bh_construction,25+fall,125+fall,110,75,true);
            }
        }
        if(v->animation%6<2)rect(p,0,72,240,3,RED);
    }
    clip_top=0;clip_bottom=BH_H;
}
void bh_render(uint8_t frame[BH_FRAME_BYTES],const bh_view_t *v) {
    clip_top=0;clip_bottom=BH_H;
    memcpy(frame,bh_palette,BH_PALETTE_BYTES);
    uint8_t *p=frame+BH_PALETTE_BYTES;
    memset(p,INK,BH_W*BH_H);
    char b[96];const bh_state_t *s=&v->car;
    if(v->page==BH_HOME) {
        header(p,v,"极限刹停王");rect(p,18,43,204,132,PANEL);
        text(p,33,53,"尊界 V800 / 第三人称",MUTED);bh_vehicle_draw(p,v);
        text(p,29,177,"注意力度，别踩断。",AMBER);
        button(p,200,"开始挑战",v->selection==0);
        button(p,235,"本机排行榜",v->selection==1);
        button(p,270,"玩法说明",v->selection==2);
        text(p,34,306,"UP/DOWN 选择  OK 确定",MUTED);
    } else if(v->page==BH_COUNTDOWN||v->page==BH_DRIVE||v->page==BH_CRASH) {
        header(p,v,s->brake_failed?"制动失效":"尊界 V800");
        number(p,18,40,(int)(s->speed*3.6f+.5f),2,WHITE);text(p,75,55,"km/h",MUTED);
        snprintf(b,sizeof(b),"峰值 %.0f",s->peak_speed*3.6f);text(p,116,40,b,AMBER);
        snprintf(b,sizeof(b),"剩余 %.1fm",s->remaining);text(p,116,57,b,WHITE);
        scene(p,v);
        if(v->page==BH_COUNTDOWN) {
            rect(p,73,113,94,47,INK);
            if(v->countdown>0)number(p,94,120,v->countdown,2,AMBER);else text(p,107,130,"GO!",GREEN);
            text(p,22,225,"松开按键，准备开始",WHITE);
        }
        if(s->brake_failed){rect(p,16,97,208,20,DARKRED);text(p,33,100,"刹车断裂！用帐篷减速",WHITE);}
        else if(s->pedal>=1400){rect(p,16,97,208,20,DARKRED);text(p,37,100,"注意力度！接近极限",AMBER);}
        hud(p,253,"车顶帐篷开度",(int)s->tent,100,TEAL);
        if(s->brake_failed){text(p,18,275,"刹车失效！帐篷仍可减速",RED);rect(p,18,291,204,3,RED);}
        else hud(p,275,"刹车力度",(int)s->pedal,1600,s->pedal>=1400?RED:AMBER);
        hud(p,297,"油门",(int)s->throttle,100,GREEN);
    } else if(v->page==BH_RESULT) {
        bool ok=s->end==BH_STOPPED;
        header(p,v,ok?"挑战成功":"挑战失败");
        text(p,27,48,ok?"漂亮！极限刹停":"没刹住，再来一次",ok?GREEN:RED);
        number(p,62,75,(int)(s->peak_speed*3.6f+.5f),3,WHITE);text(p,90,118,"峰值 km/h",MUTED);
        snprintf(b,sizeof(b),"开始制动  %.1f km/h",s->brake_start_speed*3.6f);text(p,22,146,b,WHITE);
        snprintf(b,sizeof(b),"停车剩余  %.2f m",s->remaining);text(p,22,168,b,WHITE);
        snprintf(b,sizeof(b),"最大踏板  %.0f N",s->max_pedal);text(p,22,190,b,s->brake_failed?RED:AMBER);
        snprintf(b,sizeof(b),"帐篷最大  %.0f%%",s->max_tent);text(p,22,212,b,TEAL);
        const char *reason[]={"混凝土墙","悬崖断路","货车尾部","施工围挡"};
        if(!ok)text(p,22,234,s->end==BH_COLLISION?reason[s->obstacle]:s->end==BH_IDLE?"未起步":"挑战超时",RED);
        else if(v->rank>=0) {
            snprintf(b,sizeof(b),"TOP %d  %s",v->rank+1,v->saved?"已保存":"未持久化");text(p,22,234,b,GREEN);
        } else text(p,22,234,s->peak_speed*3.6f<100?"峰值不足100，不入榜":"未进入本机前十",MUTED);
        button(p,260,"再来一局",v->selection==0);button(p,295,"返回首页",v->selection==1);
    } else if(v->page==BH_BOARD) {
        header(p,v,"本机 TOP 10");
        text(p,22,47,"峰值优先 / 余距越小越好",MUTED);
        if(!v->scores.count)text(p,36,112,"还没有合格成绩",WHITE);
        if(v->scores.count) {
            snprintf(b,sizeof(b),"个人纪录 %.2f km/h",v->scores.rows[0].peak_mms*.0036f);text(p,22,70,b,AMBER);
        }
        for(int j=0;j<5;++j) {
            int i=v->board_page*5+j;if(i>=v->scores.count)break;
            const bh_record_t *r=&v->scores.rows[i];
            rect(p,18,100+j*34,204,30,PANEL);
            snprintf(b,sizeof(b),"%02d %s  %.2f",i+1,r->name,r->peak_mms*.0036f);text(p,24,102+j*34,b,i==0?AMBER:WHITE);
            snprintf(b,sizeof(b),"余距 %.2fm",r->remaining_mm*.001f);text(p,89,116+j*34,b,MUTED);
        }
        text(p,24,282,v->storage_ok?"离线记录 / 无虚构日期":"存储不可用 / 仅本次有效",MUTED);
        text(p,24,305,"UP/DOWN 翻页  OK 返回",WHITE);
    } else {
        header(p,v,"玩法说明");
        const char *lines[]={"UP 帐篷 / DOWN 刹车","OK 油门 / 不支持组合键","按住增加，松开自动回落","快速切键，控制量独立回落","帐篷每秒 +40% / -20%","油门每秒 +65% / -45%","刹车每秒 +800 / -1200 N","超过1600N永久断裂","断裂后仍可用帐篷减速","500米直路，零百后刹停","峰值至少100km/h才入榜","虚构游戏，不预测真实车辆"};
        for(unsigned i=0;i<sizeof(lines)/sizeof(lines[0]);++i)text(p,18,45+(int)i*20,lines[i],i==7?RED:WHITE);
        text(p,36,299,"OK 返回 / 赛中不设退出",AMBER);
    }
    // Same rounded physical aperture in desktop captures; important text inset.
    for(int y=0;y<30;++y)for(int x=0;x<30;++x)if((29-x)*(29-x)+(29-y)*(29-y)>29*29) {
        p[y*BH_W+x]=BLACK;p[y*BH_W+239-x]=BLACK;
        p[(319-y)*BH_W+x]=BLACK;p[(319-y)*BH_W+239-x]=BLACK;
    }
}
