#include "brake_vehicle.h"
#include "brake_assets.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

enum { PEARL=224, BODY_LIT, BODY_SIDE, BODY_DARK, CHROME, GLASS,
       GLASS_LIT, TIRE, ALLOY, LAMP_OFF, LAMP_ON, CLOTH, CLOTH_DARK,
       ROOF, SMOKE, SPARK };
typedef struct {int x,y;} point_t;
static float clamp01(float x){return x<0?0:x>1?1:x;}
bh_vehicle_pose_t bh_vehicle_pose(const bh_view_t *v) {
    const bh_state_t *s=&v->car;bh_vehicle_pose_t p={0};
    p.tent_opening=clamp01(s->tent*.01f);p.roof_lift=p.tent_opening*1.15f;
    p.brake_lights=!s->brake_failed && s->pedal>1;
    p.pitch=p.brake_lights?-.045f*clamp01(s->pedal/1600):0;
    if(v->page==BH_CRASH && s->end==BH_COLLISION) {
        p.crush=(.22f+.65f*clamp01(s->speed/45))*clamp01(v->animation/10.0f);
        if(s->obstacle==1) {
            p.fall=clamp01(v->animation/60.0f);p.pitch=-p.fall*.65f;
            p.crush*=clamp01((v->animation-35.0f)/15.0f);
        }
    }
    return p;
}
static void pixel(uint8_t *p,int x,int y,uint8_t color){
    if(x>=0&&x<BH_W&&y>=72&&y<248)p[y*BH_W+x]=color;
}
static void span(uint8_t *p,int x,int y,int width,uint8_t color){
    if(y<72||y>=248)return;
    if(x<0){width+=x;x=0;}
    if(x+width>BH_W)width=BH_W-x;
    if(width>0)memset(p+y*BH_W+x,color,(size_t)width);
}
static void line(uint8_t *p,point_t a,point_t b,uint8_t color){
    int dx=abs(b.x-a.x),dy=-abs(b.y-a.y),sx=a.x<b.x?1:-1,sy=a.y<b.y?1:-1,err=dx+dy;
    for(;;){
        pixel(p,a.x,a.y,color);if(a.x==b.x&&a.y==b.y)break;
        int e=err*2;if(e>=dy){err+=dy;a.x+=sx;}if(e<=dx){err+=dx;a.y+=sy;}
    }
}
static void disc(uint8_t *p,int x,int y,int r,uint8_t color){
    for(int yy=-r;yy<=r;++yy)for(int xx=-r;xx<=r;++xx)
        if(xx*xx+yy*yy<=r*r)pixel(p,x+xx,y+yy,color);
}
static void body(uint8_t *p,int x,int y,int w,int h,bh_vehicle_pose_t pose){
    const bh_sprite_t *s=&bh_vehicle_body;
    int map[136];
    for(int xx=0;xx<w;++xx)map[xx]=xx*s->width/w;
    const unsigned stride=(s->width+7u)/8u;
    for(int yy=0;yy<h;++yy){
        int sy=yy*s->height/h;
        int dent=pose.crush>.05f&&yy>h/2&&yy<h*4/5?(int)(pose.crush*3):0;
        for(int xx=dent;xx<w-dent;++xx){
            int sx=map[xx];uint8_t color=s->pixels[sy*s->width+sx];
            if(!color)continue;
            if(pose.brake_lights&&(bh_vehicle_brake_mask[sy*stride+sx/8]&(1u<<(sx%8))))
                color=bh_palette[color*4+2]>75?LAMP_ON:RED;
            pixel(p,x+xx,y+yy,color);
        }
    }
}
static void tent(uint8_t *p,int x,int y,int w,int h,float opening){
    /* Zero yaw and roll are invariants. The car, tent hinge, lid and lamps all
     * share the same centered image coordinates; opening changes height only. */
    int center=x+w/2,front=y+h*8/100,base=y+h*22/100;
    int lift=(int)(opening*w*.29f+.5f),rear=base-lift;
    int front_half=w*25/100,rear_half=w*31/100;
    if(lift>0){
        for(int yy=rear;yy<base;++yy){
            int inset=(base-yy)/10;
            int half=rear_half-inset;
            for(int xx=-half;xx<=half;++xx){
                uint8_t color=((xx+yy*3)%17==0||abs(xx)>half-2)?CLOTH_DARK:CLOTH;
                pixel(p,center+xx,yy,color);
            }
        }
        if(lift>13){
            int top=rear+lift/4,bottom=base-3,half=w/10;
            for(int yy=top;yy<bottom;++yy)span(p,center-half,yy,half*2,GLASS);
            line(p,(point_t){center-half-1,top-1},(point_t){center+half,top-1},CLOTH_DARK);
            line(p,(point_t){center,top},(point_t){center,bottom},CLOTH_DARK);
        }
        for(int sign=-1;sign<=1;sign+=2){
            line(p,(point_t){center+sign*(rear_half-3),base},(point_t){center+sign*(rear_half-3),rear+2},CHROME);
            line(p,(point_t){center+sign*front_half,front+2},(point_t){center+sign*rear_half,rear+1},BODY_DARK);
        }
    }
    /* The lid samples the rendered roof's actual paint/reflection texture.
     * When it rotates past the viewing direction, its dark underside appears.
     * Only one division per scanline; no floating point in the pixel loops. */
    int lo=front<rear?front:rear,hi=front>rear?front:rear;
    for(int yy=lo;yy<=hi;++yy){
        int den=rear-front,t=den?(yy-front)*1024/den:0;
        int half=front_half+(rear_half-front_half)*t/1024;
        int sy=2+t*18/1024;if(sy>=bh_vehicle_body.height)sy=bh_vehicle_body.height-1;
        int step=(bh_vehicle_body.width<<10)/(half*4+1);
        int u=(bh_vehicle_body.width/2<<10)-half*step;
        for(int xx=-half;xx<=half;++xx){
            int sx=u>>10;u+=step;
            uint8_t c=rear<front?ROOF:bh_vehicle_body.pixels[sy*bh_vehicle_body.width+sx];
            pixel(p,center+xx,yy,c?c:ROOF);
        }
    }
    line(p,(point_t){center-rear_half,rear},(point_t){center+rear_half,rear},CHROME);
    line(p,(point_t){center-front_half,front},(point_t){center+front_half,front},BODY_DARK);
    line(p,(point_t){center-rear_half,rear},(point_t){center-front_half,front},CHROME);
    line(p,(point_t){center+rear_half,rear},(point_t){center+front_half,front},CHROME);
}
void bh_vehicle_draw(uint8_t *p,const bh_view_t *v){
    bh_vehicle_pose_t pose=bh_vehicle_pose(v);
    int w=v->page==BH_HOME?104:136;
    w-=(int)(w*pose.fall*.3f);
    int h=w*bh_vehicle_body.height/bh_vehicle_body.width;
    h-=(int)(pose.crush*10);
    if(pose.brake_lights)h-=2;
    int x=(BH_W-w)/2,bottom=v->page==BH_HOME?172:246;
    bottom+=(int)(pose.fall*65);int y=bottom-h;
    for(int yy=-5;yy<=4;++yy){
        int half=w*40/100-abs(yy)*4;
        span(p,120-half,bottom-3+yy,half*2,TIRE);
    }
    body(p,x,y,w,h,pose);
    tent(p,x,y,w,h,pose.tent_opening);
    if(pose.crush>.05f){
        /* Cracked rear glazing and retained crease lines remain attached to
         * the body, while debris and dust have their own bounded animation. */
        point_t crack={120,y+h*38/100};
        for(int i=0;i<7;++i){
            point_t end={x+w*(12+i*12)/100,y+h*(26+(i%3)*9)/100};
            line(p,crack,end,CHROME);
        }
        for(int i=0;i<3;++i){
            int yy=y+h*(65+i*9)/100;
            line(p,(point_t){x+w/5,yy},(point_t){x+w*4/5,yy+2},BODY_DARK);
        }
        for(int i=0;i<18;++i){
            int age=(int)v->animation,sign=(i&1)?1:-1;
            int xx=120+sign*(w/4+(age*(i%4+1))/3);
            int yy=bottom-8-(age<15?age:30-age)*(i%3+1)/3;
            pixel(p,xx,yy,i%3?CHROME:SPARK);
        }
        for(int i=0;i<4;++i){
            int yy=y+5-(int)v->animation/3+i*4;
            disc(p,120+(i-2)*8,yy,3+(int)v->animation/18%5,SMOKE);
        }
    }
}
