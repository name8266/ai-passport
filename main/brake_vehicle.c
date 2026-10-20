#include "brake_vehicle.h"
#include "brake_assets.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

enum { PEARL=224, BODY_LIT, BODY_SIDE, BODY_DARK, CHROME, GLASS,
       GLASS_LIT, TIRE, ALLOY, LAMP_OFF, LAMP_ON, CLOTH, CLOTH_DARK,
       ROOF, SMOKE, SPARK };
typedef struct { float x,y,z; } vertex_t;
typedef struct { int x,y; } point_t;
typedef struct { point_t p[4]; float depth; unsigned count; uint8_t color; } face_t;
/* One renderer owns this fixed scratch storage; no per-frame heap allocation. */
static face_t faces[256];
static unsigned face_count;
static bh_vehicle_pose_t pose;
static int camera_shake;
static int screen_offset;

static float clamp01(float x) { return x<0?0:x>1?1:x; }
bh_vehicle_pose_t bh_vehicle_pose(const bh_view_t *v) {
    const bh_state_t *s=&v->car;
    bh_vehicle_pose_t p={0};
    p.tent_opening=clamp01(s->tent*.01f);
    p.roof_lift=p.tent_opening*1.15f;
    p.brake_lights=!s->brake_failed && s->pedal>1;
    p.pitch=p.brake_lights?-.045f*clamp01(s->pedal/1600):0;
    if(v->page==BH_CRASH && s->end==BH_COLLISION) {
        float time=clamp01(v->animation/10.0f);
        p.crush=(.22f+.65f*clamp01(s->speed/45))*time;
        if(s->obstacle==1) {
            p.fall=clamp01(v->animation/60.0f);
            p.pitch=-p.fall*.65f;
            p.crush*=clamp01((v->animation-35.0f)/15.0f);
        }
    }
    return p;
}
static point_t project(vertex_t v,float *depth) {
    /* Rear-left chase camera, looking slightly down the road. Camera basis is
     * constant; all projection is per vertex, never per framebuffer pixel. */
    if(v.z>.6f) {
        float bend=(v.z-.6f)/1.9f;
        v.z-=pose.crush*bend;
        v.y+=pose.crush*bend*(v.y>1.25f?.25f:-.12f);
        v.x+=pose.crush*bend*.12f;
    }
    v.y+=v.z*pose.pitch-pose.fall*2;
    float x=.943f*v.x-.333f*v.z;
    float z=.333f*v.x+.943f*v.z;
    float y=.94f*v.y+.342f*z;
    z=-.342f*v.y+.94f*z+9.7f+pose.fall*2;
    if(depth)*depth=z;
    float k=245.0f/z;
    return (point_t){120+(int)(x*k)+camera_shake,223+screen_offset-(int)(y*k)};
}
static void face(const vertex_t *p,unsigned n,uint8_t color) {
    if(face_count>=sizeof(faces)/sizeof(faces[0]))return;
    face_t *f=&faces[face_count++];f->count=n;f->color=color;f->depth=0;
    for(unsigned i=0;i<n;++i) {float z;f->p[i]=project(p[i],&z);f->depth+=z;}
    f->depth/=n;
}
static void quad(vertex_t a,vertex_t b,vertex_t c,vertex_t d,uint8_t color) {
    const vertex_t p[]={a,b,c,d};face(p,4,color);
}
static void triangle(uint8_t *p,point_t a,point_t b,point_t c,uint8_t color) {
    if(a.y>b.y){point_t t=a;a=b;b=t;}
    if(a.y>c.y){point_t t=a;a=c;c=t;}
    if(b.y>c.y){point_t t=b;b=c;c=t;}
    if(a.y==c.y)return;
    int lo=a.y<72?72:a.y,hi=c.y>247?247:c.y;
    for(int y=lo;y<=hi;++y) {
        point_t d=y<b.y? a:b, e=y<b.y?b:c;
        int x1=a.x+(c.x-a.x)*(y-a.y)/(c.y-a.y);
        int x2=d.y==e.y?d.x:d.x+(e.x-d.x)*(y-d.y)/(e.y-d.y);
        if(x1>x2){int t=x1;x1=x2;x2=t;}
        if(x1<0)x1=0;
        if(x2>=BH_W)x2=BH_W-1;
        if(x1<=x2)memset(p+y*BH_W+x1,color,(size_t)(x2-x1+1));
    }
}
static void line(uint8_t *p,point_t a,point_t b,uint8_t color) {
    int dx=abs(b.x-a.x),dy=-abs(b.y-a.y),sx=a.x<b.x?1:-1,sy=a.y<b.y?1:-1,err=dx+dy;
    for(;;) {
        if(a.x>=0&&a.x<BH_W&&a.y>=72&&a.y<248)p[a.y*BH_W+a.x]=color;
        if(a.x==b.x&&a.y==b.y)break;
        int e=err*2;if(e>=dy){err+=dy;a.x+=sx;}if(e<=dx){err+=dx;a.y+=sy;}
    }
}
static void wheel(float side,float z,unsigned animation,float speed) {
    static const float cx[12]={1,.866f,.5f,0,-.5f,-.866f,-1,-.866f,-.5f,0,.5f,.866f};
    static const float cy[12]={0,.5f,.866f,1,.866f,.5f,0,-.5f,-.866f,-1,-.866f,-.5f};
    vertex_t center={side*1.065f,.46f,z};
    for(unsigned j=0;j<12;++j) {
        unsigned k=(j+1)%12;
        vertex_t a={center.x,.46f+cy[j]*.43f,z+cx[j]*.43f};
        vertex_t b={center.x,.46f+cy[k]*.43f,z+cx[k]*.43f};
        const vertex_t t[]={center,a,b};face(t,3,TIRE);
        a.x=b.x=center.x+side*.015f;
        a.y=.46f+cy[j]*.30f;a.z=z+cx[j]*.30f;
        b.y=.46f+cy[k]*.30f;b.z=z+cx[k]*.30f;
        vertex_t rim_center={a.x,center.y,center.z};
        const vertex_t rim[]={rim_center,a,b};
        face(rim,3,((j+(speed>1?animation/2:0))%3)?ALLOY:BODY_DARK);
    }
}
void bh_vehicle_draw(uint8_t *pixels,const bh_view_t *v) {
    pose=bh_vehicle_pose(v);face_count=0;
    screen_offset=v->page==BH_HOME?-75:0;
    camera_shake=(v->page==BH_CRASH&&v->animation<10)?((v->animation&1)?2:-2):0;
    /* Grounded contact shadow, behind the mesh. */
    for(int y=218;y<245;++y) {
        int half=43-(abs(y-232)*2);
        if(half>0)memset(pixels+(y+screen_offset)*BH_W+119-half,TIRE,(size_t)(half*2));
    }
    const float zz[]={-2.45f,-2.12f,1.35f,2.45f};
    const float ww[]={.91f,1.03f,1.03f,.91f};
    const float top[]={1.92f,2.12f,2.12f,1.48f};
    for(int j=0;j<3;++j) {
        float z=zz[j],next=zz[j+1],w=ww[j],wn=ww[j+1];
        quad((vertex_t){-w,.48f,z},(vertex_t){-wn,.48f,next},(vertex_t){-wn,1.25f,next},(vertex_t){-w,1.25f,z},BODY_LIT);
        quad((vertex_t){w,.48f,z},(vertex_t){w,1.25f,z},(vertex_t){wn,1.25f,next},(vertex_t){wn,.48f,next},BODY_SIDE);
        for(int sign=-1;sign<=1;sign+=2) {
            quad((vertex_t){sign*w,1.25f,z},(vertex_t){sign*wn,1.25f,next},(vertex_t){sign*(wn-.1f),top[j+1],next},(vertex_t){sign*(w-.1f),top[j],z},BODY_DARK);
            if(j<2) {
                quad((vertex_t){sign*(w+.003f),1.34f,z+.12f},(vertex_t){sign*(wn+.003f),1.34f,next-.12f},(vertex_t){sign*(wn-.11f),top[j+1]-.1f,next-.12f},(vertex_t){sign*(w-.11f),top[j]-.1f,z+.12f},sign<0?GLASS_LIT:GLASS);
            }
        }
        quad((vertex_t){-w+.1f,top[j],z},(vertex_t){w-.1f,top[j],z},(vertex_t){wn-.1f,top[j+1],next},(vertex_t){-wn+.1f,top[j+1],next},ROOF);
    }
    /* Upright rear MPV body, wraparound black glass and two-tone pearl paint. */
    quad((vertex_t){-.91f,.48f,-2.46f},(vertex_t){.91f,.48f,-2.46f},(vertex_t){.91f,1.30f,-2.46f},(vertex_t){-.91f,1.30f,-2.46f},PEARL);
    quad((vertex_t){-.91f,1.30f,-2.46f},(vertex_t){.91f,1.30f,-2.46f},(vertex_t){.81f,1.92f,-2.46f},(vertex_t){-.81f,1.92f,-2.46f},GLASS);
    quad((vertex_t){-.77f,1.39f,-2.47f},(vertex_t){.76f,1.39f,-2.47f},(vertex_t){.69f,1.80f,-2.47f},(vertex_t){-.69f,1.80f,-2.47f},GLASS_LIT);
    quad((vertex_t){-.89f,.53f,-2.48f},(vertex_t){.89f,.53f,-2.48f},(vertex_t){.89f,.64f,-2.48f},(vertex_t){-.89f,.64f,-2.48f},CHROME);
    quad((vertex_t){-.25f,.72f,-2.48f},(vertex_t){.25f,.72f,-2.48f},(vertex_t){.25f,.89f,-2.48f},(vertex_t){-.25f,.89f,-2.48f},TIRE);
    uint8_t lamp=pose.brake_lights?LAMP_ON:LAMP_OFF;
    for(int sign=-1;sign<=1;sign+=2) {
        float x=sign*.70f;
        quad((vertex_t){x-.15f,1.14f,-2.49f},(vertex_t){x+.15f,1.14f,-2.49f},(vertex_t){x+.15f,1.29f,-2.49f},(vertex_t){x-.15f,1.29f,-2.49f},lamp);
        quad((vertex_t){sign*1.045f,1.21f,-1.35f},(vertex_t){sign*1.045f,1.21f,-.68f},(vertex_t){sign*1.045f,1.25f,-.68f},(vertex_t){sign*1.045f,1.25f,-1.35f},CHROME);
        wheel((float)sign,-1.55f,v->animation,v->car.speed);
        wheel((float)sign,1.62f,v->animation,v->car.speed);
    }
    quad((vertex_t){-.35f,1.82f,-2.475f},(vertex_t){.35f,1.82f,-2.475f},(vertex_t){.35f,1.86f,-2.475f},(vertex_t){-.35f,1.86f,-2.475f},lamp);
    /* Front fascia, tall lamp signatures and chamfered hood. */
    quad((vertex_t){-.91f,.5f,2.45f},(vertex_t){-.91f,1.48f,2.45f},(vertex_t){.91f,1.48f,2.45f},(vertex_t){.91f,.5f,2.45f},PEARL);
    /* Front-hinged hard-shell tent: lift and fabric geometry use the exact
     * same continuous opening as the HUD/physics, including passive closing. */
    float rear=2.20f+pose.roof_lift,front=2.20f;
    quad((vertex_t){-.73f,2.14f,-1.75f},(vertex_t){.73f,2.14f,-1.75f},(vertex_t){.73f,rear,-1.75f},(vertex_t){-.73f,rear,-1.75f},CLOTH_DARK);
    for(int sign=-1;sign<=1;sign+=2) {
        quad((vertex_t){sign*.73f,2.14f,-1.75f},(vertex_t){sign*.73f,2.14f,1.17f},(vertex_t){sign*.73f,front,1.17f},(vertex_t){sign*.73f,rear,-1.75f},sign<0?CLOTH:CLOTH_DARK);
    }
    quad((vertex_t){-.77f,rear,-1.8f},(vertex_t){.77f,rear,-1.8f},(vertex_t){.77f,front,1.22f},(vertex_t){-.77f,front,1.22f},ROOF);
    quad((vertex_t){-.77f,rear,-1.8f},(vertex_t){.77f,rear,-1.8f},(vertex_t){.77f,rear+.07f,-1.8f},(vertex_t){-.77f,rear+.07f,-1.8f},CHROME);
    for(unsigned i=1;i<face_count;++i) {
        face_t f=faces[i];unsigned j=i;
        while(j && faces[j-1].depth<f.depth){faces[j]=faces[j-1];--j;}faces[j]=f;
    }
    for(unsigned i=0;i<face_count;++i) {
        face_t *f=&faces[i];triangle(pixels,f->p[0],f->p[1],f->p[2],f->color);
        if(f->count==4)triangle(pixels,f->p[0],f->p[2],f->p[3],f->color);
    }
    /* Door shut lines, roof supports, and wind flutter remain projected with
     * the same body transform rather than floating screen-space indicators. */
    for(int sign=-1;sign<=1;sign+=2) {
        line(pixels,project((vertex_t){sign*1.04f,.65f,-.55f},NULL),project((vertex_t){sign*1.04f,1.3f,-.55f},NULL),BODY_DARK);
        if(pose.tent_opening>.01f)line(pixels,project((vertex_t){sign*.73f,2.2f,-1.68f},NULL),project((vertex_t){sign*.73f,rear,-1.68f},NULL),CHROME);
    }
    if(pose.crush>.05f) {
        /* Retained dents plus cracked glass, scattered fragments, sparks and
         * rising dust. Intensity depends on game impact speed and elapsed time. */
        const vertex_t cracks[]={{-.62f,1.62f,-2.50f},{.12f,1.52f,-2.50f},{.65f,1.73f,-2.50f},{-.10f,1.80f,-2.50f},{.22f,1.35f,-2.50f}};
        for(unsigned i=0;i<4;++i)line(pixels,project(cracks[i],NULL),project(cracks[i+1],NULL),CHROME);
        for(unsigned i=0;i<18;++i) {
            float t=v->animation/30.0f,side=(i&1)?1:-1;
            vertex_t a={side*(.6f+(i%4)*t),.3f+fmaxf(0,1.7f*t-2*t*t)+(i%3)*.09f,1.4f-(i%5)*t};
            point_t q=project(a,NULL);
            if(q.x>0&&q.x<239&&q.y>=72&&q.y<247) {
                pixels[q.y*BH_W+q.x]=i%3?CHROME:SPARK;
                pixels[q.y*BH_W+q.x+1]=i%3?CHROME:SPARK;
            }
        }
        for(unsigned i=0;i<4;++i) {
            vertex_t a={.35f+i*.17f,1.8f+v->animation*.014f+(i%2)*.16f,1.0f};
            point_t q=project(a,NULL);int radius=2+(int)(v->animation/18)%5;
            for(int yy=-radius;yy<=radius;++yy)for(int xx=-radius;xx<=radius;++xx)
                if(xx*xx+yy*yy<=radius*radius && q.x+xx>=0&&q.x+xx<240&&q.y+yy>=72&&q.y+yy<248)
                    pixels[(q.y+yy)*BH_W+q.x+xx]=SMOKE;
        }
    }
}
