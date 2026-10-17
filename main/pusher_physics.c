#include "pusher_physics.h"
#include <stddef.h>
#include <string.h>
#ifdef CP_PHYSICS_AUDIT
#include <stdio.h>
#include <stdlib.h>
#endif
#ifdef ESP_PLATFORM
#include "esp_attr.h"
#include "esp_cpu.h"
#define CP_HOT IRAM_ATTR __attribute__((noinline))
#else
#define CP_HOT
#endif

#define PI 3.14159265358979323846f
#define GRAVITY 380.0f
#define SOLVER_PASSES 4
#define COIN_R 8.0f
#define DT16 ((65536+CP_HZ/2)/CP_HZ)
/* Static scratch belongs to the single simulation owner, never on task stack. */
/* Q16.16 contact arithmetic avoids software floating-point inside the hot
 * iterative solver on the C3. Collision geometry/orientation remain 3D. */
typedef struct { int32_t x,y,z; } fv3;
typedef struct {
    fv3 p,sv,sw;
    int32_t xx,yy,zz,xy,xz,yz,im;
    uint8_t kind;bool valid;
} fixed_body_t;
typedef struct {int16_t x,y,z;} packed_v3;
typedef struct {
    packed_v3 n,ra;
    int16_t tangent_a,tangent_b;
    uint8_t tangent_bits,surface,slot,reserved;
    int32_t f1,f2,normal,bias;
    int16_t a,b;
} contact_t;
/* Collision geometry uses Q8 millimetres and Q12 unit axes. Products fit
 * signed 32 bits at this cabinet's scale, avoiding the C3 software FPU. */
typedef struct {int16_t x,y,z;} sv3;
typedef struct {int32_t x,y,z;} gv3;
typedef struct {sv3 axis[3],half,bound;gv3 p;int32_t q[4];int16_t sphere;uint8_t n,vertices;} geometry_t;
static uint16_t s_broad_order[CP_MAX_COINS];
static geometry_t s_geometry[CP_MAX_COINS];
static inline __attribute__((always_inline)) gv3 gv(int32_t x,int32_t y,int32_t z){return (gv3){x,y,z};}
static inline __attribute__((always_inline)) gv3 ga(gv3 a,gv3 b){return gv(a.x+b.x,a.y+b.y,a.z+b.z);}
static inline __attribute__((always_inline)) gv3 gs(gv3 a,gv3 b){return gv(a.x-b.x,a.y-b.y,a.z-b.z);}
static inline __attribute__((always_inline)) gv3 gload(sv3 v){return gv(v.x,v.y,v.z);}
static inline __attribute__((always_inline)) sv3 gstore(gv3 v){return (sv3){(int16_t)v.x,(int16_t)v.y,(int16_t)v.z};}
static inline __attribute__((always_inline)) int32_t gdot(gv3 a,gv3 b){return (a.x*b.x+a.y*b.y+a.z*b.z)>>12;}
static inline __attribute__((always_inline)) gv3 gm(gv3 a,int32_t t){return gv((a.x*t)>>12,(a.y*t)>>12,(a.z*t)>>12);}
static inline __attribute__((always_inline)) int32_t giabs(int32_t v){return v<0?-v:v;}
static inline __attribute__((always_inline)) gv3 gtransform(const geometry_t *g,int32_t x,int32_t y,int32_t z){return ga(gm(gload(g->axis[0]),x),ga(gm(gload(g->axis[1]),y),gm(gload(g->axis[2]),z)));}
static void geometry_shape(geometry_t *g,unsigned kind){
    static const int16_t radius[6]={2076,2076,3300,3260,2864,2076};g->sphere=kind<6?radius[kind]:0;
    g->n=cp_kind_prize(kind)?3:5;g->vertices=cp_kind_prize(kind)?4:8;
}
/* Vertices are derived from the authoritative basis on demand. No second
 * 48-byte vertex array is retained for every sleeping body. */
static inline __attribute__((always_inline)) gv3 geometry_ring(const geometry_t *g,unsigned k){
    static const int16_t rx[8]={4096,2896,0,-2896,-4096,-2896,0,2896},ry[8]={0,2896,4096,2896,0,-2896,-4096,-2896};
    return g->n==3?gtransform(g,k&1?g->half.x:-g->half.x,k&2?g->half.y:-g->half.y,0):gtransform(g,(g->half.x*rx[k])>>12,(g->half.y*ry[k])>>12,0);
}
static void geometry_from_body(geometry_t *g,const cp_coin_t *b){
    g->q[0]=(int32_t)(b->q.w*32768);g->q[1]=(int32_t)(b->q.x*32768);g->q[2]=(int32_t)(b->q.y*32768);g->q[3]=(int32_t)(b->q.z*32768);
    for(unsigned k=0;k<3;++k){cp_v3 axis=cp_body_axis(b,k);g->axis[k]=(sv3){(int16_t)(axis.x*4096),(int16_t)(axis.y*4096),(int16_t)(axis.z*4096)};}
    cp_v3 half=cp_body_half(b);g->half=(sv3){(int16_t)(half.x*256),(int16_t)(half.y*256),(int16_t)(half.z*256)};
    g->bound=(sv3){(int16_t)(b->bound.x*256),(int16_t)(b->bound.y*256),(int16_t)(b->bound.z*256)};
    geometry_shape(g,b->kind);
}
static void geometry_refresh(unsigned i,const cp_coin_t *b){geometry_from_body(&s_geometry[i],b);}
static inline __attribute__((always_inline)) int32_t gextent(const geometry_t *g,gv3 n){
    int32_t x=gdot(n,gload(g->axis[0])),y=gdot(n,gload(g->axis[1])),z=giabs(gdot(n,gload(g->axis[2])));
    if(g->n==3)return (g->half.x*giabs(x)+g->half.y*giabs(y)+g->half.z*z)>>12;
    int32_t radial=giabs(x)>giabs(y)?giabs(x):giabs(y),diagonal=((giabs(x+y)>giabs(x-y)?giabs(x+y):giabs(x-y))*2896)>>12;
    if(diagonal>radial)radial=diagonal;
    return (g->half.x*radial+g->half.z*z)>>12;
}
static CP_HOT gv3 gsupport(const geometry_t *g,gv3 n){
    int32_t x=gdot(n,gload(g->axis[0])),y=gdot(n,gload(g->axis[1])),z=gdot(n,gload(g->axis[2]));
    if(g->n==3)return ga(g->p,gtransform(g,x>=0?g->half.x:-g->half.x,y>=0?g->half.y:-g->half.y,z>=0?g->half.z:-g->half.z));
    unsigned best=0;int32_t value=INT32_MIN;gv3 winner=gv(0,0,0);
    for(unsigned k=0;k<8;++k){gv3 ring=geometry_ring(g,k);int32_t v=gdot(n,ring);if(v>value){value=v;best=k;winner=ring;}}
    (void)best;return ga(g->p,ga(winner,gm(gload(g->axis[2]),z>=0?g->half.z:-g->half.z)));
}
static union {contact_t contacts[CP_MAX_CONTACTS];unsigned char bytes[sizeof(contact_t)*CP_MAX_CONTACTS];} s_workspace;
#define s_contacts s_workspace.contacts
static unsigned s_frame_depth_bytes;
void *cp_frame_depth_scratch(unsigned size){
    if(size>sizeof(s_workspace.bytes)||size%4)return NULL;
    s_frame_depth_bytes=size;return s_workspace.bytes+sizeof(s_workspace.bytes)-size;
}
void *cp_frame_scratch(unsigned size){return size<=sizeof(s_workspace.bytes)-s_frame_depth_bytes?s_workspace.bytes:NULL;}
/* Persistent narrow-phase normals. Only slow, nearly unchanged pairs reuse
 * them, for at most three ticks; moving/rotating impacts always run full SAT. */
typedef struct {uint32_t key;uint16_t step;sv3 normal,delta;} pair_cache_t;
_Static_assert(CP_MAX_COINS<=512,"9-bit ordered body indices");
static pair_cache_t s_pair_cache[512];
static fixed_body_t s_fixed[CP_MAX_COINS];
static unsigned s_count;
static uint32_t s_factory_cycles;
static inline __attribute__((always_inline)) uint32_t cpu_cycles(void){
#ifdef ESP_PLATFORM
    return esp_cpu_get_cycle_count();
#else
    return 0;
#endif
}
static int32_t s_penetration,s_front,s_surface_speed;
static uint32_t (*s_clock)(void);
void cp_set_clock(uint32_t (*clock_us)(void)){s_clock=clock_us;}
static uint32_t clock_now(void){return s_clock?s_clock():0;}
typedef struct { uint32_t key,step; uint16_t normal; packed_v3 friction; int8_t nx,ny,nz; } warm_t;
static warm_t s_warm[512];
static inline __attribute__((always_inline)) uint16_t cache_normal(int32_t value){uint32_t q=(uint32_t)value>>2;return (uint16_t)(q>65535?65535:q);}
static inline __attribute__((always_inline)) int16_t cache_friction(int32_t value){int32_t q=value/4;return (int16_t)(q<-32768?-32768:q>32767?32767:q);}
static inline __attribute__((always_inline)) packed_v3 cache_vector(fv3 v){return (packed_v3){cache_friction(v.x),cache_friction(v.y),cache_friction(v.z)};}
static unsigned warm_index(uint32_t key) { return (key*2654435761u)>>23; }
static inline __attribute__((always_inline)) int32_t fx(float v) {return (int32_t)(cp_clamp(v,-30000,30000)*65536);}
static inline __attribute__((always_inline)) int32_t fm(int32_t a,int32_t b) {return (int32_t)(((int64_t)a*b)>>16);}
static inline __attribute__((always_inline)) fv3 fv(int32_t x,int32_t y,int32_t z) {return (fv3){x,y,z};}
static inline __attribute__((always_inline)) fv3 from_v(cp_v3 a) {return fv(fx(a.x),fx(a.y),fx(a.z));}
static inline __attribute__((always_inline)) fv3 fa(fv3 a,fv3 b) {return fv(a.x+b.x,a.y+b.y,a.z+b.z);}
static inline __attribute__((always_inline)) fv3 fs(fv3 a,fv3 b) {return fv(a.x-b.x,a.y-b.y,a.z-b.z);}
static inline __attribute__((always_inline)) fv3 cross(fv3 a,fv3 b) {return fv((int32_t)(((int64_t)a.y*b.z-(int64_t)a.z*b.y)>>16),
    (int32_t)(((int64_t)a.z*b.x-(int64_t)a.x*b.z)>>16),(int32_t)(((int64_t)a.x*b.y-(int64_t)a.y*b.x)>>16));}
static uint32_t root32(uint32_t n) {
    if(!n)return 0;
    uint32_t x=1u<<((32-__builtin_clz(n)+1)/2);
    for(unsigned k=0;k<6;++k){uint32_t next=(x+n/x)/2;if(next>=x)break;x=next;}
    return x;
}
static fv3 unit(fv3 a) {
    uint32_t x=(uint32_t)(a.x<0?-a.x:a.x),y=(uint32_t)(a.y<0?-a.y:a.y),z=(uint32_t)(a.z<0?-a.z:a.z);
    uint32_t xx=x>>2,yy=y>>2,zz=z>>2,d=root32(xx*xx+yy*yy+zz*zz);
    if(!d)return fv(65536,0,0);
    int32_t nx=(int32_t)((x*16384u)/d),ny=(int32_t)((y*16384u)/d),nz=(int32_t)((z*16384u)/d);
    return fv(a.x<0?-nx:nx,a.y<0?-ny:ny,a.z<0?-nz:nz);
}
/* Exact comparison with the public pose detects test/tool edits without
 * retaining another 52 bytes of float snapshots per body. */
static bool pose_position(const fixed_body_t *f,const cp_coin_t *b){
    return cp_fixed_float(f->p.x,16)==b->p.x&&cp_fixed_float(f->p.y,16)==b->p.y&&cp_fixed_float(f->p.z,16)==b->p.z;
}
static bool pose_rotation(const geometry_t *g,const cp_coin_t *b){
    return cp_fixed_float(g->q[0],15)==b->q.w&&cp_fixed_float(g->q[1],15)==b->q.x&&cp_fixed_float(g->q[2],15)==b->q.y&&cp_fixed_float(g->q[3],15)==b->q.z;
}
static void tensor_refresh(unsigned i,const cp_coin_t *b){fixed_body_t *f=&s_fixed[i];const geometry_t *g=&s_geometry[i];
    cp_v3 inertia=cp_body_inertia(b);int32_t ix=fx(inertia.x),iy=fx(inertia.y),iz=fx(inertia.z);
    f->im=fx(cp_body_inv_mass(b));
    gv3 x=gload(g->axis[0]),y=gload(g->axis[1]),z=gload(g->axis[2]);
#define TENSOR(U,V) ((int32_t)(((int64_t)ix*x.U*x.V+(int64_t)iy*y.U*y.V+(int64_t)iz*z.U*z.V)>>24))
    f->xx=TENSOR(x,x);f->yy=TENSOR(y,y);f->zz=TENSOR(z,z);f->xy=TENSOR(x,y);f->xz=TENSOR(x,z);f->yz=TENSOR(y,z);
#undef TENSOR
}
static __attribute__((noinline)) void fixed_body(unsigned i,const cp_coin_t *b,bool gravity){
    fixed_body_t *f=&s_fixed[i];geometry_t *g=&s_geometry[i];bool fresh=!f->valid||f->kind!=b->kind;
    if(fresh||!pose_position(f,b))f->p=from_v(b->p);
    if(b->sleeping){f->sv=f->sw=fv(0,0,0);}
    else{
        f->sv=fv((int32_t)(b->v.x*256),(int32_t)(b->v.y*256),(int32_t)(b->v.z*256));
        f->sw=fv((int32_t)(b->w.x*4096),(int32_t)(b->w.y*4096),(int32_t)(b->w.z*4096));
        if(gravity)f->sv.z-=(380*256+CP_HZ/2)/CP_HZ;
    }
    g->p=gv(f->p.x>>8,f->p.y>>8,f->p.z>>8);
    if(fresh||!pose_rotation(g,b)){geometry_refresh(i,b);tensor_refresh(i,b);}
    f->kind=b->kind;f->valid=true;
}

/* Q8 linear velocity/impulses, Q12 angular velocity, Q14 Jacobians.
 * Contact iteration is exclusively 32-bit arithmetic; position stays Q16. */
static inline __attribute__((always_inline)) packed_v3 pack_axis(fv3 v){return (packed_v3){(int16_t)(v.x>>2),(int16_t)(v.y>>2),(int16_t)(v.z>>2)};}
static inline __attribute__((always_inline)) fv3 raw(packed_v3 v){return fv(v.x,v.y,v.z);}
static inline __attribute__((always_inline)) fv3 cross14(fv3 arm,fv3 n){return fv((arm.y*n.z-arm.z*n.y)>>14,(arm.z*n.x-arm.x*n.z)>>14,(arm.x*n.y-arm.y*n.x)>>14);}
static inline __attribute__((always_inline)) fv3 angular(const fixed_body_t *b,fv3 arm){
    int32_t xx=b->xx>>2,yy=b->yy>>2,zz=b->zz>>2,xy=b->xy>>2,xz=b->xz>>2,yz=b->yz>>2;
    return fv((xx*arm.x+xy*arm.y+xz*arm.z)>>8,(xy*arm.x+yy*arm.y+yz*arm.z)>>8,(xz*arm.x+yz*arm.y+zz*arm.z)>>8);
}
static inline __attribute__((always_inline)) int32_t mass_dot(fv3 r,fv3 a){return (r.x*a.x+r.y*a.y+r.z*a.z)>>8;}
static inline __attribute__((always_inline)) fv3 arm_velocity(fv3 w,packed_v3 r){return fv((w.y*r.z-w.z*r.y)>>12,(w.z*r.x-w.x*r.z)>>12,(w.x*r.y-w.y*r.x)>>12);}
static packed_v3 s_surfaces[32];static unsigned s_surface_count;
static inline __attribute__((always_inline)) packed_v3 contact_rb(const fixed_body_t *a,const fixed_body_t *b,const contact_t *c){
    return b?(packed_v3){(int16_t)((a->p.x>>8)+c->ra.x-(b->p.x>>8)),(int16_t)((a->p.y>>8)+c->ra.y-(b->p.y>>8)),(int16_t)((a->p.z>>8)+c->ra.z-(b->p.z>>8))}:(packed_v3){0,0,0};
}
/* The constructed first tangent has one exact zero component. Store its two
 * nonzero Q16 components as Q14 plus their low bits; derive the second by
 * exactly the same Q16 cross product as the original contact factory. */
static inline __attribute__((always_inline)) void contact_basis(const contact_t *c,packed_v3 *t1,packed_v3 *t2){
    int32_t x=(int32_t)c->tangent_a*4+(c->tangent_bits&3),y=(int32_t)c->tangent_b*4+((c->tangent_bits>>2)&3);
    fv3 full=giabs(c->n.z)>2867*4?fv(x,0,y):fv(x,y,0);
    *t1=pack_axis(full);*t2=pack_axis(cross(fv(c->n.x*4,c->n.y*4,c->n.z*4),full));
}
static inline __attribute__((always_inline)) uint32_t contact_key(const contact_t *c){unsigned a=(unsigned)c->a;int b=c->b;
    return a<256&&b<255?(a<<16)|((unsigned)(b+1)<<8)|c->slot:0x40000000u|(a<<18)|((unsigned)(b+1)<<8)|c->slot;
}
static inline __attribute__((always_inline)) fv3 relative(const fixed_body_t *a,const fixed_body_t *b,const contact_t *c){
    return fs(b?fa(b->sv,arm_velocity(b->sw,contact_rb(a,b,c))):raw(s_surfaces[c->surface]),fa(a->sv,arm_velocity(a->sw,c->ra)));
}
static inline __attribute__((always_inline)) int32_t velocity_dot(fv3 v,packed_v3 n){return (v.x*(n.x>>2)+v.y*(n.y>>2)+v.z*(n.z>>2))>>12;}
static inline __attribute__((always_inline)) fv3 linear_j(packed_v3 n,int32_t j){return fv((n.x*j)>>14,(n.y*j)>>14,(n.z*j)>>14);}
static inline __attribute__((always_inline)) fv3 angular_j(packed_v3 n,int32_t j){return fv((n.x*j)>>10,(n.y*j)>>10,(n.z*j)>>10);}
typedef struct {packed_v3 t1,t2,an,at1,at2,bn,bt1,bt2;int16_t in,in1,in2,incross;int32_t kn1,kn2;} jacobian_t;
static int32_t reciprocal(int32_t v) {if(v<164)v=164;return (int32_t)((1u<<26)/(uint32_t)v);}
static inline __attribute__((always_inline)) packed_v3 jac_pack(fv3 v){return (packed_v3){(int16_t)v.x,(int16_t)v.y,(int16_t)v.z};}
/* Recreate immutable coefficients from the current rigid pose. Impulses remain
 * resident; recomputation is exact and removes 72 bytes/contact from RAM. */
static CP_HOT void jacobian(const contact_t *c,jacobian_t *j,bool normal_only){
    const fixed_body_t *a=&s_fixed[c->a],*b=c->b>=0?&s_fixed[c->b]:NULL;
    fv3 ra=raw(c->ra),rb=raw(contact_rb(a,b,c)),ran=cross14(ra,raw(c->n)),rbn=cross14(rb,raw(c->n));
    fv3 an=angular(a,ran),bn=b?angular(b,rbn):fv(0,0,0);j->an=jac_pack(an);j->bn=jac_pack(bn);
    int32_t mass=(a->im>>2)+(b?b->im>>2:0);j->in=reciprocal(mass+mass_dot(ran,an)+mass_dot(rbn,bn));
    if(normal_only)return;
    contact_basis(c,&j->t1,&j->t2);
    fv3 rat1=cross14(ra,raw(j->t1)),rat2=cross14(ra,raw(j->t2)),rbt1=cross14(rb,raw(j->t1)),rbt2=cross14(rb,raw(j->t2));
    fv3 at1=angular(a,rat1),at2=angular(a,rat2),bt1=b?angular(b,rbt1):fv(0,0,0),bt2=b?angular(b,rbt2):fv(0,0,0);
    j->at1=jac_pack(at1);j->at2=jac_pack(at2);j->bt1=jac_pack(bt1);j->bt2=jac_pack(bt2);
    int32_t k1=mass+mass_dot(rat1,at1)+mass_dot(rbt1,bt1),k2=mass+mass_dot(rat2,at2)+mass_dot(rbt2,bt2),k12=mass_dot(rat1,at2)+mass_dot(rbt1,bt2);
    int32_t det=(int32_t)(((int64_t)k1*k2-(int64_t)k12*k12)>>14);if(det<164)det=164;
    int32_t invdet=(int32_t)((1u<<28)/(uint32_t)det);j->in1=(int32_t)(((int64_t)k2*invdet)>>16);j->in2=(int32_t)(((int64_t)k1*invdet)>>16);j->incross=-(int32_t)(((int64_t)k12*invdet)>>16);
    fv3 nn=raw(c->n),tt1=raw(j->t1),tt2=raw(j->t2);
    j->kn1=mass_dot(ran,at1)+mass_dot(rbn,bt1)+(int32_t)((int64_t)mass*(nn.x*tt1.x+nn.y*tt1.y+nn.z*tt1.z)>>28);
    j->kn2=mass_dot(ran,at2)+mass_dot(rbn,bt2)+(int32_t)((int64_t)mass*(nn.x*tt2.x+nn.y*tt2.y+nn.z*tt2.z)>>28);
}
static CP_HOT void impulse(fixed_body_t *a,fixed_body_t *b,const contact_t *c,const jacobian_t *jac,int32_t jn,int32_t jt1,int32_t jt2){
    if(!(jn|jt1|jt2))return;
    fv3 j=linear_j(c->n,jn),da=angular_j(jac->an,jn),db=angular_j(jac->bn,jn);
    if(jt1|jt2){j=fa(j,fa(linear_j(jac->t1,jt1),linear_j(jac->t2,jt2)));
        da=fa(da,fa(angular_j(jac->at1,jt1),angular_j(jac->at2,jt2)));if(b)db=fa(db,fa(angular_j(jac->bt1,jt1),angular_j(jac->bt2,jt2)));}
    int32_t im=a->im>>4;a->sv=fs(a->sv,fv((j.x*im)>>12,(j.y*im)>>12,(j.z*im)>>12));a->sw=fs(a->sw,da);
    if(b){im=b->im>>4;b->sv=fa(b->sv,fv((j.x*im)>>12,(j.y*im)>>12,(j.z*im)>>12));b->sw=fa(b->sw,db);}
}
static const float ring_x[8]={1,.70710678f,0,-.70710678f,-1,-.70710678f,0,.70710678f};
static const float ring_y[8]={0,.70710678f,1,.70710678f,0,-.70710678f,-1,-.70710678f};

static uint32_t random_u32(cp_game_t *g)
{
    uint32_t x=g->rng; x^=x<<13; x^=x>>17; x^=x<<5;
    return g->rng=x?x:0x6d2b79f5u;
}
static float random_float(cp_game_t *g) { return (random_u32(g)&65535)/65535.0f; }
static cp_q4 normalize(cp_q4 q)
{
    float d=q.w*q.w+q.x*q.x+q.y*q.y+q.z*q.z;
    if(!isfinite(d)||d<1e-12f) return (cp_q4){1,0,0,0};
    float f=1.0f/sqrtf(d); return (cp_q4){q.w*f,q.x*f,q.y*f,q.z*f};
}
static void integrate_orientation(unsigned i,cp_coin_t *b){
    geometry_t *g=&s_geometry[i];const fixed_body_t *f=&s_fixed[i];
    int32_t w=g->q[0],x=g->q[1],y=g->q[2],z=g->q[3];
    int32_t ax=fm(f->sw.x*16,DT16),ay=fm(f->sw.y*16,DT16),az=fm(f->sw.z*16,DT16);
    int32_t nw=w-(int32_t)(((int64_t)ax*x+(int64_t)ay*y+(int64_t)az*z)>>17);
    int32_t nx=x+(int32_t)(((int64_t)ax*w+(int64_t)ay*z-(int64_t)az*y)>>17);
    int32_t ny=y+(int32_t)((-(int64_t)ax*z+(int64_t)ay*w+(int64_t)az*x)>>17);
    int32_t nz=z+(int32_t)(((int64_t)ax*y-(int64_t)ay*x+(int64_t)az*w)>>17);
    uint32_t len=root32((uint32_t)((int64_t)nw*nw+(int64_t)nx*nx+(int64_t)ny*ny+(int64_t)nz*nz));
    w=nw*32768/(int32_t)len;x=nx*32768/(int32_t)len;y=ny*32768/(int32_t)len;z=nz*32768/(int32_t)len;
    b->q=(cp_q4){cp_fixed_float(w,15),cp_fixed_float(x,15),cp_fixed_float(y,15),cp_fixed_float(z,15)};
    gv3 axis[3]={gv(4096-((y*y+z*z)>>17),(x*y+w*z)>>17,(x*z-w*y)>>17),
        gv((x*y-w*z)>>17,4096-((x*x+z*z)>>17),(y*z+w*x)>>17),
        gv((x*z+w*y)>>17,(y*z-w*x)>>17,4096-((x*x+y*y)>>17))};
    for(unsigned k=0;k<3;++k){g->axis[k]=gstore(axis[k]);}g->q[0]=w;g->q[1]=x;g->q[2]=y;g->q[3]=z;
    geometry_shape(g,b->kind);g->bound=(sv3){(int16_t)gextent(g,gv(4096,0,0)),(int16_t)gextent(g,gv(0,4096,0)),(int16_t)gextent(g,gv(0,0,4096))};
    b->bound=cp_v(cp_fixed_float(g->bound.x,8),cp_fixed_float(g->bound.y,8),cp_fixed_float(g->bound.z,8));
    tensor_refresh(i,b);
}
static bool box(const cp_coin_t *b) { return cp_kind_prize(b->kind); }
static float extent(const cp_coin_t *b,cp_v3 n)
{
    float x=cp_dot(n,cp_body_axis(b,0)),y=cp_dot(n,cp_body_axis(b,1)),z=cp_dot(n,cp_body_axis(b,2));
    if(box(b)) return cp_body_half(b).x*fabsf(x)+cp_body_half(b).y*fabsf(y)+cp_body_half(b).z*fabsf(z);
    float radial=fmaxf(fmaxf(fabsf(x),fabsf(y)),
                      .70710678f*fmaxf(fabsf(x+y),fabsf(x-y)));
    return cp_body_half(b).x*radial+cp_body_half(b).z*fabsf(z);
}
cp_v3 cp_body_axis(const cp_coin_t *b,unsigned k){cp_q4 q=b->q;
    return k==0?cp_v(1-2*(q.y*q.y+q.z*q.z),2*(q.x*q.y+q.w*q.z),2*(q.x*q.z-q.w*q.y)):
        k==1?cp_v(2*(q.x*q.y-q.w*q.z),1-2*(q.x*q.x+q.z*q.z),2*(q.y*q.z+q.w*q.x)):
        cp_v(2*(q.x*q.z+q.w*q.y),2*(q.y*q.z-q.w*q.x),1-2*(q.x*q.x+q.y*q.y));
}
cp_v3 cp_body_half(const cp_coin_t *b){return b->kind==CP_PRIZE_RED?cp_v(9,7,6):b->kind==CP_PRIZE_BLUE?cp_v(7,7,8):b->kind==CP_PRIZE_GREEN?cp_v(8,6,5):cp_v(COIN_R,COIN_R,1.3f);}
float cp_body_inv_mass(const cp_coin_t *b){return box(b)?1/2.2f:b->kind==CP_CHIP?.5f:1;}
cp_v3 cp_body_inertia(const cp_coin_t *b){
    cp_v3 half=cp_body_half(b);float mass=box(b)?2.2f:b->kind==CP_CHIP?2:1;
    if(box(b))return cp_v(3/(mass*(half.y*half.y+half.z*half.z)),3/(mass*(half.x*half.x+half.z*half.z)),3/(mass*(half.x*half.x+half.y*half.y)));
    return cp_v(12/(mass*(3*COIN_R*COIN_R+6.76f)),12/(mass*(3*COIN_R*COIN_R+6.76f)),2/(mass*COIN_R*COIN_R));
}
void cp_body_refresh(cp_coin_t *b){
    b->q=normalize(b->q);
    b->bound=cp_v(extent(b,cp_v(1,0,0)),extent(b,cp_v(0,1,0)),extent(b,cp_v(0,0,1)));
}
cp_v3 cp_support(const cp_coin_t *b,cp_v3 d)
{
    float dx=cp_dot(d,cp_body_axis(b,0)),dy=cp_dot(d,cp_body_axis(b,1)),dz=cp_dot(d,cp_body_axis(b,2));
    cp_v3 local;
    if(box(b)) local=cp_v(dx>=0?cp_body_half(b).x:-cp_body_half(b).x,dy>=0?cp_body_half(b).y:-cp_body_half(b).y,dz>=0?cp_body_half(b).z:-cp_body_half(b).z);
    else {
        unsigned best=0; float v=-1e30f;
        for(unsigned i=0;i<8;++i) { float t=dx*ring_x[i]+dy*ring_y[i]; if(t>v){v=t;best=i;} }
        local=cp_v(cp_body_half(b).x*ring_x[best],cp_body_half(b).x*ring_y[best],dz>=0?cp_body_half(b).z:-cp_body_half(b).z);
    }
    return cp_add(b->p,cp_add(cp_mul(cp_body_axis(b,0),local.x),
        cp_add(cp_mul(cp_body_axis(b,1),local.y),cp_mul(cp_body_axis(b,2),local.z))));
}
unsigned cp_coin_count(const cp_game_t *g)
{
    unsigned n=0; for(unsigned i=0;i<CP_MAX_COINS;++i) n+=g->coins[i].active; return n;
}
unsigned cp_tower_count(const cp_game_t *g)
{
    unsigned n=0; for(unsigned i=0;i<CP_MAX_COINS;++i)
        if(g->coins[i].active && !box(&g->coins[i]) && g->coins[i].p.z>9 && g->coins[i].p.y>92) ++n;
    return n;
}
cp_coin_t *cp_spawn(cp_game_t *g,cp_coin_kind_t kind,cp_v3 p,cp_q4 q)
{
    for(unsigned i=0;i<CP_MAX_COINS;++i) if(!g->coins[i].active) {
        cp_coin_t *b=&g->coins[i];s_fixed[i].valid=false; memset(b,0,sizeof(*b));
        b->active=true; b->kind=kind; b->p=p; b->q=q; cp_body_refresh(b); return b;
    }
    return NULL;
}
void cp_init(cp_game_t *g,uint32_t seed,uint32_t bank,uint32_t best,uint32_t round)
{
    for(unsigned i=0;i<CP_MAX_COINS;++i)s_broad_order[i]=(uint16_t)i;
    memset(s_pair_cache,0,sizeof(s_pair_cache));memset(s_warm,0,sizeof(s_warm));memset(s_fixed,0,sizeof(s_fixed));
    memset(g,0,sizeof(*g)); g->rng=seed?seed:12345;g->bank=bank;g->best=best;g->round=round?round:1;
    g->pusher_y=8;g->features=true;g->arcade.clock=12*CP_HZ;g->arcade.tower_clock=18*CP_HZ;
    /* Loose lower deck, direct upper deck, two independent six-layer coin towers. */
    static const float loose_x[4]={24,104,184,104},loose_y[4]={100,100,100,CP_FRONT_EDGE-20};
    for(unsigned k=0;k<4;++k)cp_spawn(g,CP_COIN_GOLD,cp_v(loose_x[k],loose_y[k],1.3f),(cp_q4){1,0,0,0});
    for(unsigned col=0;col<5;++col) cp_spawn(g,CP_COIN_GOLD,cp_v(32+col*36,CP_FRONT_EDGE-1,1.3f),(cp_q4){1,0,0,0});
    for(unsigned row=0;row<1;++row) for(unsigned col=0;col<4;++col)
        cp_spawn(g,CP_COIN_GOLD,cp_v(31+col*48,26+row*22,19.3f),(cp_q4){1,0,0,0});
    for(unsigned tower=0;tower<2;++tower) for(unsigned level=0;level<6;++level)
        for(unsigned k=0;k<3;++k) {
            float angle=k*2*PI/3;
            cp_spawn(g,CP_COIN_GOLD,cp_v(63+tower*83+10*cosf(angle),111+10*sinf(angle),1.3f+level*2.6f),
                (cp_q4){1,0,0,0});
        }
    cp_spawn(g,CP_PRIZE_RED,cp_v(63,111,21.6f),(cp_q4){1,0,0,0});
    cp_spawn(g,CP_PRIZE_BLUE,cp_v(146,111,23.6f),(cp_q4){1,0,0,0});
    cp_spawn(g,CP_PRIZE_GREEN,cp_v(104,CP_FRONT_EDGE-18,5.0f),(cp_q4){1,0,0,0});
    for(unsigned i=0;i<CP_MAX_COINS;++i) {
        cp_coin_t *b=&g->coins[i];if(!b->active||b->p.y<99) continue;
        b->sleeping=true;b->support_count=1;b->support[0]=-1;
        if(b->p.z>2&&b->kind<CP_PRIZE_RED) b->support[0]=(int16_t)(i-3);
        if(b->kind==CP_PRIZE_RED||b->kind==CP_PRIZE_BLUE) {
            b->support_count=0;
            for(unsigned j=0;j<CP_MAX_COINS&&b->support_count<3;++j) {
                cp_coin_t *c=&g->coins[j];
                if(c->active&&c->kind==CP_COIN_GOLD&&c->p.z>13&&hypotf(c->p.x-b->p.x,c->p.y-b->p.y)<12)
                    b->support[b->support_count++]=(int16_t)j;
            }
        }
    }
}
bool cp_house_coin(cp_game_t *g,int x,int y){
 if(cp_coin_count(g)>=CP_MAX_COINS-12)return false;
 for(unsigned i=0;i<CP_MAX_COINS;++i){const cp_coin_t *b=&g->coins[i];if(b->active&&fabsf(b->p.x-x)<b->bound.x+8&&fabsf(b->p.y-y)<b->bound.y+8&&fabsf(b->p.z-19.3f)<b->bound.z+1.3f)return false;}
 return cp_spawn(g,CP_COIN_GOLD,cp_v(x,y,19.3f),(cp_q4){1,0,0,0})!=NULL;
}
static bool inlet_drop(cp_game_t *g,bool paid)
{
    if(g->paused||(paid&&!g->bank)) return false;
    unsigned prizes=cp_prize_count(g),reserve=g->features&&prizes<3?3-prizes:0;
    if(cp_coin_count(g)>=CP_MAX_COINS-reserve){++g->capacity_waits;return false;}
    /* Avoid a second coin intersecting one still in the physical coin chute. */
    for(unsigned i=0;i<CP_MAX_COINS;++i) {
        const cp_coin_t *b=&g->coins[i];
        if(b->active&&b->feeder==1&&b->p.z>142&&fabsf(b->p.x-CP_ENTRY_X)<15) return false;
    }
    float a=(random_float(g)-.5f)*.08f;
    cp_coin_t *b=cp_spawn(g,CP_COIN_GOLD,cp_v(CP_ENTRY_X,CP_ENTRY_Y,CP_ENTRY_Z),(cp_q4){.70710678f*cosf(a),.70710678f*cosf(a),.70710678f*sinf(a),-.70710678f*sinf(a)});
    if(!b) return false;
    b->feeder=1;b->paid=(uint8_t)paid;b->v=cp_v((random_float(g)-.5f)*.2f,0,-8);b->w=cp_v(0,(random_float(g)-.5f)*.6f,0);
    if(paid){--g->bank;++g->drops;}else ++g->bonus_drops;
    return true;
}
bool cp_special_inlet(cp_game_t *g,unsigned kind){
    if(kind<1||kind>CP_SPECIAL_COUNT)return false;
    if(!inlet_drop(g,false))return false;
    for(unsigned i=0;i<CP_MAX_COINS;++i){cp_coin_t *b=&g->coins[i];
        if(b->active&&b->feeder==1&&!b->special&&b->p.z==CP_ENTRY_Z){
            b->special=(uint8_t)kind;b->paid=1;
            if(kind==CP_SP_METEOR){b->kind=CP_CHIP;cp_body_refresh(b);s_fixed[i].valid=false;}
            g->arcade.play_ticks=8*CP_HZ;return true;
        }
    }
    return false;
}
void cp_body_kick(cp_game_t *g,unsigned i,int vx,int vy,int vz){
    if(i>=CP_MAX_COINS||!g->coins[i].active)return;
    cp_coin_t *b=&g->coins[i];b->sleeping=false;b->quiet=0;
    /* Called only between complete solver ticks, before fixed_body refresh. */
    b->v.x+=cp_fixed_float(vx,8);b->v.y+=cp_fixed_float(vy,8);b->v.z+=cp_fixed_float(vz,8);
}
void cp_reward_event(cp_game_t *g,cp_event_t event){
    if(!g->features||event<CP_EVENT_RUSH||event>CP_EVENT_MAX)return;
    /* Keep the jackpot celebration visible when another prize lands in the
     * same step. Its physical effect still applies below. */
    if(event==CP_EVENT_JACKPOT||g->event_kind!=CP_EVENT_JACKPOT||!g->event_ticks){
        g->event_kind=(uint8_t)event;g->event_ticks=4*CP_HZ;
    }
    ++g->events;g->arcade.last_result=(uint8_t)event;
    if(event==CP_EVENT_BLACKHOLE)g->arcade.hole_ticks=(uint16_t)cp_rogue_duration(g,g->arcade.hole_ticks,8*CP_HZ);
    if(event==CP_EVENT_DOUBLE){g->arcade.double_ticks=(uint16_t)cp_rogue_duration(g,g->arcade.double_ticks,12*CP_HZ);
        unsigned marked=0;for(unsigned i=0;i<CP_MAX_COINS;++i){cp_coin_t *b=&g->coins[i];
            if(b->active&&!b->feeder&&b->kind==CP_COIN_GOLD&&b->p.z>=0&&b->p.y>82&&(!g->rogue.active||marked<12)){b->feeder_mask|=CP_BODY_CLONE;++marked;}}}
    if(event==CP_EVENT_TOWERS){unsigned n=g->arcade.tower_queue+2;g->arcade.tower_queue=(uint8_t)(n>4?4:n);}
    if(event==CP_EVENT_SHAKE)g->arcade.shake_ticks=(uint16_t)cp_rogue_duration(g,g->arcade.shake_ticks,5*CP_HZ);
    if(event==CP_EVENT_FEVER)g->arcade.fever_ticks=(uint16_t)cp_rogue_duration(g,g->arcade.fever_ticks,10*CP_HZ);
    if(event==CP_EVENT_RUSH||event==CP_EVENT_JACKPOT)g->rush_ticks=(uint16_t)cp_rogue_duration(g,g->rush_ticks,10*CP_HZ);
    if(event==CP_EVENT_SAVE||event==CP_EVENT_JACKPOT)g->guard_ticks=(uint16_t)cp_rogue_duration(g,g->guard_ticks,15*CP_HZ);
    if(event==CP_EVENT_RAIN||event==CP_EVENT_JACKPOT){
        unsigned queue=g->bonus_queue+(event==CP_EVENT_JACKPOT?12:8);
        g->bonus_queue=(uint16_t)(queue>4096?4096:queue);
    }
    if(event==CP_EVENT_JACKPOT){++g->jackpots;if(g->rogue.active)cp_rogue_jackpot(g);else{g->bank+=50;g->score+=50;}
        g->collection_mask=0;if(g->score>g->best)g->best=g->score;}
    cp_rogue_event(g);
}
bool cp_drop(cp_game_t *g){
    if(g->tactics.active&&g->tactics.queue_count)return cp_tactics_cast(g,cp_tactics_auto_kind(g));
    if(!inlet_drop(g,true))return false;
    if(g->features){g->arcade.play_ticks=8*CP_HZ;
        if(g->arcade.double_ticks&&g->bonus_queue<4096)++g->bonus_queue;
    }
    cp_rogue_paid(g);
    return true;
}
static cp_event_t arcade_draw(cp_game_t *g){
    cp_arcade_t *a=&g->arcade;
    if(random_u32(g)%((g->rogue.active?32-3*g->rogue.relic[CP_R_LUCK]:16))==0)return CP_EVENT_JACKPOT;
    const unsigned mask=(1u<<CP_EVENT_RUSH)|(1u<<CP_EVENT_RAIN)|(1u<<CP_EVENT_SAVE)|
        (1u<<CP_EVENT_BLACKHOLE)|(1u<<CP_EVENT_DOUBLE)|(1u<<CP_EVENT_TOWERS)|(1u<<CP_EVENT_SHAKE)|(1u<<CP_EVENT_FEVER);
    if(!a->bag)a->bag=(uint16_t)mask;
    unsigned count=(unsigned)__builtin_popcount(a->bag),pick=random_u32(g)%count;
    for(unsigned event=1;event<=CP_EVENT_MAX;++event)if(a->bag&(1u<<event)){
        if(pick--==0){a->bag&=(uint16_t)~(1u<<event);return (cp_event_t)event;}}
    return CP_EVENT_RAIN;
}
static void arcade_spin(cp_game_t *g){
    cp_arcade_t *a=&g->arcade;if(a->spin_ticks||a->cooldown)return;
    a->result=(uint8_t)arcade_draw(g);a->spin_ticks=45;++a->spins;
}
void cp_arcade_gate(cp_game_t *g,unsigned lane){
    if(!g->features||g->paused||lane>2||(g->rogue.active&&!g->bank&&!g->tactics.queue_count))return;
    if(cp_rogue_lane(g,lane)){++g->arcade.gate_hits;arcade_spin(g);}
}
int cp_tower_pusher_mm(const cp_game_t *g){
    int travel=(CP_TOWER_TICKS-g->arcade.tower_ticks)*50/CP_TOWER_TICKS;
    return g->arcade.tower_side?216-travel:-8+travel;
}
static void tower_tick(cp_game_t *g,bool playing){
    cp_arcade_t *a=&g->arcade;
    if(a->tower_ticks){--a->tower_ticks;return;}
    if(a->tower_clock&&playing)--a->tower_clock;
    if(!a->tower_clock&&playing&&!a->tower_queue){a->tower_queue=1;a->tower_clock=24*CP_HZ;}
    if(!a->tower_queue||a->hole_ticks||cp_coin_count(g)>64)return;
    unsigned side=a->next_side;int x=side?208:0;
    for(unsigned i=0;i<CP_MAX_COINS;++i){const cp_coin_t *b=&g->coins[i];
        if(b->active&&fabsf(b->p.x-x)<18&&b->p.y>80&&b->p.y<130&&b->p.z<24)return;}
    a->tower_side=(uint8_t)side;a->next_side^=1;a->tower_ticks=CP_TOWER_TICKS;
    for(unsigned level=0;level<6;++level)for(unsigned row=0;row<2;++row){
        cp_coin_t *b=cp_spawn(g,CP_COIN_GOLD,cp_v((float)x,96+row*18,1.3f+level*2.6f),(cp_q4){1,0,0,0});
        if(b)++a->tower_coins;
    }
    if(g->tactics.active&&g->tactics.stage==CP_ACT_COUNT-1&&!g->tactics.crown_spawned){
        cp_coin_t *crown=cp_spawn(g,CP_PRIZE_RED,cp_v((float)x,114,21.6f),(cp_q4){1,0,0,0});
        if(crown){crown->trait=CP_TRAIT_CROWN;g->tactics.crown_spawned=true;}
    }
    --a->tower_queue;++a->towers;a->tower_clock=24*CP_HZ;
}
static void clone_tick(cp_game_t *g){
    cp_arcade_t *a=&g->arcade;
    if(a->clone_clock){--a->clone_clock;return;}
    if(cp_coin_count(g)>82)return;
    for(unsigned i=0;i<CP_MAX_COINS;++i){cp_coin_t *b=&g->coins[i];
        if(!b->active||!(b->feeder_mask&CP_BODY_CLONE))continue;
        cp_v3 p=cp_add(b->p,cp_v(0,0,24));bool clear=true;
        for(unsigned j=0;j<CP_MAX_COINS;++j){const cp_coin_t *c=&g->coins[j];
            if(c->active&&fabsf(c->p.x-p.x)<17&&fabsf(c->p.y-p.y)<17&&fabsf(c->p.z-p.z)<5){clear=false;break;}}
        if(!clear)continue;
        cp_coin_t *clone=cp_spawn(g,CP_COIN_GOLD,p,b->q);if(!clone)return;
        b->feeder_mask&=(uint16_t)~CP_BODY_CLONE;clone->v=cp_v(0,0,-4);clone->w=cp_v(0,.6f,0);
        ++a->clones;a->clone_clock=5;return;
    }
}
static void arcade_tick(cp_game_t *g){
    cp_arcade_t *a=&g->arcade;bool playing=g->rogue.active?(a->play_ticks&&(g->bank||g->tactics.queue_count)):(a->play_ticks||g->autoplay||g->bonus_queue);
    if(a->play_ticks)--a->play_ticks;
    a->lane=(uint8_t)((g->steps/(2*CP_HZ))%3);
    if(a->cooldown)--a->cooldown;
    if(a->reveal_ticks)--a->reveal_ticks;
    if(a->spin_ticks){if(!--a->spin_ticks){cp_reward_event(g,(cp_event_t)a->result);a->reveal_ticks=30;a->cooldown=(g->rogue.active?10:5)*CP_HZ;a->clock=(uint16_t)((12+random_u32(g)%7)*CP_HZ);cp_rogue_reel(g);}}
    else if(playing&&a->clock){if(!--a->clock)arcade_spin(g);}
    else if(playing&&!a->cooldown)arcade_spin(g);
    if(a->hole_ticks&&! --a->hole_ticks){unsigned n=a->tower_queue+2;a->tower_queue=(uint8_t)(n>4?4:n);}
    if(a->double_ticks)--a->double_ticks;
    if(a->shake_ticks)--a->shake_ticks;
    if(a->fever_ticks)--a->fever_ticks;
    clone_tick(g);tower_tick(g,playing);
}
static __attribute__((noinline)) void gameplay_tick(cp_game_t *g){
    if(!g->features)return;
    arcade_tick(g);
    unsigned count=cp_coin_count(g);
    if(!g->circulation&&count>=(g->rogue.active?264u:68u)){g->circulation=true;++g->circulation_cycles;
        for(unsigned i=0;i<CP_MAX_COINS;++i){cp_coin_t *b=&g->coins[i];
            if(b->active&&!b->feeder&&b->p.y>90){b->sleeping=false;b->quiet=0;}}
    }else if(g->circulation&&count<=(g->rogue.active?224u:52u))g->circulation=false;
    if(g->rush_ticks)--g->rush_ticks;
    if(g->guard_ticks){--g->guard_ticks;
        if(!g->guard_ticks)for(unsigned i=0;i<CP_MAX_COINS;++i){cp_coin_t *b=&g->coins[i];
            if(b->active&&b->p.y>CP_SIDE_EDGE&&(b->p.x<24||b->p.x>184)){b->sleeping=false;b->quiet=0;}}
    }
    if(g->event_ticks)--g->event_ticks;
    if(g->bonus_clock)--g->bonus_clock;
    if(g->bonus_queue&&!g->bonus_clock){
        if(inlet_drop(g,false))--g->bonus_queue;
        g->bonus_clock=CP_HZ*3/4;
    }
    if(g->rogue.active&&((!g->bank&&!g->tactics.queue_count)||!g->arcade.play_ticks))return;
    for(unsigned kind=0;kind<3;++kind){bool present=false;
        for(unsigned i=0;i<CP_MAX_COINS;++i)if(g->coins[i].active&&g->coins[i].kind==CP_PRIZE_RED+kind){present=true;break;}
        if(present){g->restock_ticks[kind]=0;continue;}
        if(g->restock_ticks[kind]<6*CP_HZ){++g->restock_ticks[kind];continue;}
        /* An overhead dispenser releases a real box, only into clear space.
         * Full/obstructed cabinets wait; no existing body is deleted or moved. */
        int x=46+(int)kind*58;bool clear=cp_coin_count(g)<CP_MAX_COINS;
        for(unsigned i=0;i<CP_MAX_COINS&&clear;++i){const cp_coin_t *b=&g->coins[i];
            if(b->active&&fabsf(b->p.x-x)<22&&fabsf(b->p.y-91)<20&&b->p.z>26)clear=false;}
        if(clear&&cp_spawn(g,(cp_coin_kind_t)(CP_PRIZE_RED+kind),cp_v(x,91,48),(cp_q4){1,0,0,0})){
            ++g->restocks;g->restock_ticks[kind]=0;
        }
    }
}
static inline __attribute__((always_inline)) gv3 local_axis(const geometry_t *g,unsigned i,bool edge){
    if(g->n==3)return i==0?gv(4096,0,0):i==1?gv(0,4096,0):gv(0,0,4096);
    if(!i)return gv(0,0,4096);
    static const int16_t cs[4]={3784,1567,-1567,-3784},sn[4]={1567,3784,3784,1567};
    return edge?gv(-sn[i-1],cs[i-1],0):gv(cs[i-1],sn[i-1],0);
}
static inline __attribute__((always_inline)) int32_t local_extent(const geometry_t *g,gv3 n){
    int32_t x=giabs(n.x),y=giabs(n.y),z=giabs(n.z);
    if(g->n==3)return (g->half.x*x+g->half.y*y+g->half.z*z)>>12;
    int32_t r=x>y?x:y,diag=((giabs(n.x+n.y)>giabs(n.x-n.y)?giabs(n.x+n.y):giabs(n.x-n.y))*2896)>>12;
    if(diag>r){r=diag;}return (g->half.x*r+g->half.z*z)>>12;
}
static inline __attribute__((always_inline)) gv3 matrix_axis(const int32_t m[3][3],gv3 n,bool transpose){
    if(transpose)return gv((m[0][0]*n.x+m[1][0]*n.y+m[2][0]*n.z)>>12,(m[0][1]*n.x+m[1][1]*n.y+m[2][1]*n.z)>>12,(m[0][2]*n.x+m[1][2]*n.y+m[2][2]*n.z)>>12);
    return gv((m[0][0]*n.x+m[0][1]*n.y+m[0][2]*n.z)>>12,(m[1][0]*n.x+m[1][1]*n.y+m[1][2]*n.z)>>12,(m[2][0]*n.x+m[2][1]*n.y+m[2][2]*n.z)>>12);
}
/* Edge crosses are formed before Q12 truncation. Near-parallel edges must
 * not turn (1,3,1) into a length-1.66 solver normal through integer sqrt. */
static inline __attribute__((always_inline)) int32_t axis_ratio(int32_t value,uint32_t denominator,uint32_t reciprocal32){
    uint32_t magnitude=(uint32_t)giabs(value);if(magnitude==denominator)return value<0?-4096:4096;
    /* Reciprocal estimate is a lower bound. The residual fits 32 bits for
     * Q12 edge products; one hardware division restores the exact quotient. */
    uint32_t estimate,remainder;
    if(denominator<=4096||denominator>33554432u){
        estimate=(uint32_t)(((uint64_t)magnitude*reciprocal32)>>20);
        remainder=(uint32_t)(((uint64_t)magnitude<<12)-(uint64_t)estimate*denominator);
        estimate+=remainder/denominator;
    }else{
        /* One reciprocal Newton step in Q44. Across Q12 edge products its
         * error is below one quotient unit; a residual comparison is exact. */
        uint32_t error=0u-denominator*reciprocal32;
        uint32_t reciprocal44=(reciprocal32<<12)+(uint32_t)(((uint64_t)reciprocal32*error)>>20);
        estimate=(uint32_t)(((uint64_t)magnitude*reciprocal44)>>32);
        remainder=(magnitude<<12)-estimate*denominator;
        if(remainder>=denominator)++estimate;
    }
    int32_t exact=(int32_t)estimate;return value<0?-exact:exact;
}
static CP_HOT __attribute__((noinline)) gv3 edge_cross(gv3 a,gv3 b){
    int32_t x=a.y*b.z-a.z*b.y,y=a.z*b.x-a.x*b.z,z=a.x*b.y-a.y*b.x;
    int32_t hi=giabs(x);if(giabs(y)>hi)hi=giabs(y);if(giabs(z)>hi)hi=giabs(z);
    if(!hi)return gv(0,0,0);
    uint32_t reciprocal32=UINT32_MAX/(uint32_t)hi;
    return gv(axis_ratio(x,(uint32_t)hi,reciprocal32),axis_ratio(y,(uint32_t)hi,reciprocal32),axis_ratio(z,(uint32_t)hi,reciprocal32));
}
static inline __attribute__((always_inline)) bool local_separates(const geometry_t *a,const geometry_t *b,gv3 d,gv3 na,gv3 nb,int32_t known_a,int32_t known_b,bool coplanar,int32_t *best,gv3 *normal){
    bool face=known_a>=0||known_b>=0;
    uint32_t len2=(uint32_t)(na.x*na.x+na.y*na.y+na.z*na.z);if(len2<4)return false;
    int32_t depth=(known_a>=0?known_a:local_extent(a,na))+(known_b>=0?known_b:local_extent(b,nb))-giabs(gdot(d,na));
    if(depth< -24){*normal=gtransform(a,na.x,na.y,na.z);return true;}
    if(face&&depth>=*best)return false;
    if(!face&&depth>=0&&*best!=INT32_MAX){if(*best<=0)return false;int32_t upper=giabs(na.x)+giabs(na.y)+giabs(na.z);
        if(depth*4096>=*best*upper)return false;
        if((int64_t)depth*depth*16777216>=(int64_t)*best**best*len2)return false;}
    gv3 world=gtransform(a,na.x,na.y,na.z); /* axes, not positions */
    if(coplanar&&(uint32_t)(world.z*world.z)>len2*4/5)return false;
    uint32_t len=face?4096:root32(len2);int32_t distance=face?depth:depth*4096/(int32_t)len;
    if(distance<*best){*best=distance;int32_t sign=gdot(d,na)>=0?4096:-4096;uint32_t world_len=face?4096:root32((uint32_t)(world.x*world.x+world.y*world.y+world.z*world.z));if(!world_len)world_len=4096;*normal=gv(world.x*sign/(int32_t)world_len,world.y*sign/(int32_t)world_len,world.z*sign/(int32_t)world_len);}
    return false;
}
static int32_t face_extent(const geometry_t *g,unsigned i){return g->n==3?(i==0?g->half.x:i==1?g->half.y:g->half.z):i==0?g->half.z:(g->half.x*3784)>>12;}
/* A shared point on the center segment proves a real intersection. For a
 * shallow cap contact this permits the face normal without enumerating edge
 * crosses; edge/rim contacts keep the complete SAT. */
static CP_HOT int32_t segment_score(const geometry_t *g,gv3 d){
    int32_t score=0;
    for(unsigned k=0;k<g->n;++k){int32_t distance=giabs(gdot(d,local_axis(g,k,false))),extent=face_extent(g,k)+(g->n==5?8:6);
        int32_t q=distance*4096/(extent>0?extent:1);if(q>score)score=q;}
    return score;
}
static CP_HOT bool overlap(const geometry_t *a,const geometry_t *b,gv3 *n,int32_t *depth){
    gv3 delta=gs(b->p,a->p);
    if(giabs(delta.x)>=a->bound.x+b->bound.x+24||giabs(delta.y)>=a->bound.y+b->bound.y+24||giabs(delta.z)>=a->bound.z+b->bound.z+24)return false;
    int32_t radius=a->sphere+b->sphere;if(radius>0&&delta.x*delta.x+delta.y*delta.y+delta.z*delta.z>radius*radius)return false;
    /* Rotate the complete SAT into body A's frame. The relative matrix and
     * projected edge directions are shared by all face/cross-product axes. */
    int32_t m[3][3];for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)m[i][j]=gdot(gload(a->axis[i]),gload(b->axis[j]));
    gv3 d=gv(gdot(delta,gload(a->axis[0])),gdot(delta,gload(a->axis[1])),gdot(delta,gload(a->axis[2])));
    bool coplanar=a->n==5&&b->n==5&&giabs(a->axis[2].z)>4055&&giabs(b->axis[2].z)>4055&&giabs(delta.z)<294;*depth=INT32_MAX;
    for(unsigned i=0;i<a->n;++i){gv3 axis=local_axis(a,i,false),other=matrix_axis(m,axis,true);
        if(local_separates(a,b,d,axis,other,face_extent(a,i),-1,coplanar,depth,n))return false;}
    for(unsigned i=0;i<b->n;++i){gv3 other=local_axis(b,i,false),axis=matrix_axis(m,other,false);
        if(local_separates(a,b,d,axis,other,-1,face_extent(b,i),coplanar,depth,n))return false;}
    if(a->n==5&&b->n==5){
        if(giabs(m[2][2])>4093)return true;
        if(*depth>=0&&*depth<128&&giabs(gdot(*n,gload(a->axis[2])))>3932&&giabs(gdot(*n,gload(b->axis[2])))>3932){
            gv3 other=matrix_axis(m,d,true);int32_t sa=segment_score(a,d),sb=segment_score(b,other);
            if(sa<=4096||sb<=4096||(1u<<24)/(unsigned)sa+(1u<<24)/(unsigned)sb>=4098)return true;
        }
    }
    gv3 eb[5],own_a[5],own_b[5];for(unsigned i=0;i<a->n;++i)own_a[i]=local_axis(a,i,true);
    for(unsigned j=0;j<b->n;++j){own_b[j]=local_axis(b,j,true);eb[j]=matrix_axis(m,own_b[j],false);}
    for(unsigned i=0;i<a->n;++i)for(unsigned j=0;j<b->n;++j){gv3 na=edge_cross(own_a[i],eb[j]);
        /* Derive B from the same axis, preserving its scale. */
        gv3 nb=matrix_axis(m,na,true);
        if(local_separates(a,b,d,na,nb,-1,-1,coplanar,depth,n))return false;}
    return true;
}
bool cp_placement_clear(const cp_game_t *g,cp_coin_kind_t kind,cp_v3 position){
    cp_coin_t candidate={.kind=(uint8_t)kind,.p=position,.q={1,0,0,0}};cp_body_refresh(&candidate);
    geometry_t a;geometry_from_body(&a,&candidate);a.p=gv((int32_t)(position.x*256),(int32_t)(position.y*256),(int32_t)(position.z*256));
    for(unsigned i=0;i<CP_MAX_COINS;++i){const cp_coin_t *b=&g->coins[i];if(!b->active)continue;
        if(fabsf(b->p.x-position.x)>b->bound.x+candidate.bound.x+.1f||fabsf(b->p.y-position.y)>b->bound.y+candidate.bound.y+.1f||fabsf(b->p.z-position.z)>b->bound.z+candidate.bound.z+.1f)continue;
        geometry_t other;geometry_from_body(&other,b);other.p=gv((int32_t)(b->p.x*256),(int32_t)(b->p.y*256),(int32_t)(b->p.z*256));
        gv3 n;int32_t depth;if(overlap(&a,&other,&n,&depth)&&depth>5)return false;
    }
    return true;
}
static bool shared_segment(const geometry_t *a,const geometry_t *b,gv3 d){
    gv3 da=gv(gdot(d,gload(a->axis[0])),gdot(d,gload(a->axis[1])),gdot(d,gload(a->axis[2])));
    gv3 db=gv(gdot(d,gload(b->axis[0])),gdot(d,gload(b->axis[1])),gdot(d,gload(b->axis[2])));
    int32_t sa=segment_score(a,da),sb=segment_score(b,db);
    return sa<=4096||sb<=4096||(1u<<24)/(unsigned)sa+(1u<<24)/(unsigned)sb>=4098;
}
static bool pair_overlap(cp_game_t *g,unsigned i,unsigned j,gv3 *n,int32_t *depth){
    geometry_t *a=&s_geometry[i],*b=&s_geometry[j];gv3 d=gs(b->p,a->p);
    uint32_t key=i<112&&j<112?i*112+j+1:0x10000000u|(i<<9)|j;pair_cache_t *c=&s_pair_cache[((uint32_t)key*2654435761u)>>23];
    /* Any separating direction is a valid rejection certificate. Recheck it
     * against the current complete convex extents, with a conservative margin.
     * This never reuses a penetration depth or contact response. */
    if(c->key==(key|0x80000000u)){
        gv3 axis=gload(c->normal);int32_t gap=gextent(a,axis)+gextent(b,axis)-giabs(gdot(d,axis));
        if(gap< -256){++g->cache_hits;return false;}
    }
    unsigned age=(uint16_t)(g->steps-c->step);
    fv3 wa=s_fixed[i].sw,wb=s_fixed[j].sw;
    int32_t norm2=c->normal.x*c->normal.x+c->normal.y*c->normal.y+c->normal.z*c->normal.z;
    if(c->key==key&&norm2>16500000&&norm2<17000000&&
       giabs(gdot(gload(c->normal),gload(a->axis[2])))>4090&&
       giabs(gdot(gload(c->normal),gload(b->axis[2])))>4090&&age>0&&age<=3&&
       giabs(d.x-c->delta.x)+giabs(d.y-c->delta.y)+giabs(d.z-c->delta.z)<192&&
       giabs(wa.x)+giabs(wa.y)+giabs(wa.z)+giabs(wb.x)+giabs(wb.y)+giabs(wb.z)<8192){
        *n=gload(c->normal);*depth=gextent(a,*n)+gextent(b,*n)-gdot(d,*n);
        if(*depth>=-24&&*depth<=128&&shared_segment(a,b,d)){++g->cache_hits;return true;}
    }
    ++g->sat_calls;
    *n=gv(0,0,0);
    if(!overlap(a,b,n,depth)){
        if(n->x||n->y||n->z)*c=(pair_cache_t){(key|0x80000000u),(uint16_t)g->steps,gstore(*n),gstore(d)};
        else c->key=0;
        return false;
    }
    *c=(pair_cache_t){key,(uint16_t)g->steps,gstore(*n),gstore(d)};return true;
}
static CP_HOT void contact_motion(cp_game_t *g,int a,int b,gv3 p,gv3 n,int32_t depth,packed_v3 surface,unsigned slot)
{
    uint32_t start_cycles=cpu_cycles();
#ifdef CP_PHYSICS_AUDIT
    int64_t audit_norm=(int64_t)n.x*n.x+(int64_t)n.y*n.y+(int64_t)n.z*n.z;
    if(audit_norm<16400000||audit_norm>17200000){fprintf(stderr,"Bad normal %d,%d,%d step %u\n",n.x,n.y,n.z,g->steps);abort();}
#endif
    if(s_count>=CP_MAX_CONTACTS) { ++g->overflow_steps;return; }
    contact_t *c=&s_contacts[s_count++];
    /* All solver fields below are assigned before use. Avoid zeroing the
     * whole 112-byte record only to overwrite it for every contact. */
    c->n=(packed_v3){(int16_t)(n.x*4),(int16_t)(n.y*4),(int16_t)(n.z*4)};
    unsigned surface_id=0;while(surface_id<s_surface_count&&memcmp(&s_surfaces[surface_id],&surface,sizeof(surface)))++surface_id;
    if(surface_id==s_surface_count){if(s_surface_count>=32){--s_count;++g->overflow_steps;return;}s_surfaces[s_surface_count++]=surface;}
    c->surface=(uint8_t)surface_id;c->slot=(uint8_t)slot;c->a=(int16_t)a;c->b=(int16_t)b;c->normal=c->f1=c->f2=0;
    cp_coin_t *supported=n.z< -1638?&g->coins[a]:b>=0&&n.z>1638?&g->coins[b]:NULL;
    int support=n.z< -1638?b:a;
    if(supported){bool seen=false;for(unsigned k=0;k<supported->support_count;++k)if(supported->support[k]==support)seen=true;
        if(!seen&&supported->support_count<3)supported->support[supported->support_count++]=(int16_t)support;}
    fv3 normal=fv(n.x*16,n.y*16,n.z*16),t1,t2;
    if(n.x==0&&n.y==0){t1=fv(n.z>0?65536:-65536,0,0);t2=fv(0,65536,0);}
    else{t1=unit(giabs(n.z)>2867?fv(normal.z,0,-normal.x):fv(normal.y,-normal.x,0));t2=cross(normal,t1);}
    packed_v3 ct1=pack_axis(t1),ct2=pack_axis(t2);
    int32_t tangent_b=giabs(n.z)>2867?t1.z:t1.y;
    c->tangent_a=(int16_t)(t1.x>>2);c->tangent_b=(int16_t)(tangent_b>>2);c->tangent_bits=(uint8_t)((t1.x&3)|((tangent_b&3)<<2));
    fixed_body_t *ba=&s_fixed[a],*bb=b>=0?&s_fixed[b]:NULL;
    fv3 ra=fv(p.x-(ba->p.x>>8),p.y-(ba->p.y>>8),p.z-(ba->p.z>>8));
    c->ra=(packed_v3){(int16_t)ra.x,(int16_t)ra.y,(int16_t)ra.z};
    int32_t vn=velocity_dot(relative(ba,bb,c),c->n);
    c->bias=depth<0?depth*CP_HZ:depth>10?(depth-10)*9/2:0;if(c->bias>35*256)c->bias=35*256;
    if(depth>=0&&vn<-15*256)c->bias+=-(vn*(g->rogue.active&&!g->coins[a].feeder?g->rogue.bounce:20)>>8);
    uint32_t key=contact_key(c);
    warm_t *w=&s_warm[warm_index(key)];
    if(w->key==key&&w->step+1==g->steps&&
       (w->nx*n.x+w->ny*n.y+w->nz*n.z)>509805) {
        fv3 cached=fv(w->friction.x*4,w->friction.y*4,w->friction.z*4);c->normal=w->normal*4;c->f1=velocity_dot(cached,ct1);c->f2=velocity_dot(cached,ct2);
    }
    if(depth>s_penetration)s_penetration=depth;
    s_factory_cycles+=cpu_cycles()-start_cycles;
}
static inline __attribute__((always_inline)) void contact(cp_game_t *g,int a,int b,gv3 p,gv3 n,int32_t depth,int32_t speed,unsigned slot){
    contact_motion(g,a,b,p,n,depth,(packed_v3){0,(int16_t)speed,0},slot);
}
const cp_peg_t cp_feeder_pegs[CP_FEEDER_PEGS]={
    {53,102},{87,102},{121,102},{155,102},
    {36,81},{70,81},{104,81},{138,81},{172,81},
    {53,60},{87,60},{121,60},{155,60}
};
void cp_feeder_rod(float seconds,cp_rod_t *rod){
    float phase=seconds*(2*PI/2.4f),a=.32f*sinf(phase),speed=.32f*(2*PI/2.4f)*cosf(phase);
    *rod=(cp_rod_t){(int32_t)((CP_ROD_X+CP_ROD_LENGTH*cosf(a))*256),(int32_t)((CP_ROD_Z+CP_ROD_LENGTH*sinf(a))*256),
        (int32_t)(-CP_ROD_LENGTH*sinf(a)*speed*256),(int32_t)(CP_ROD_LENGTH*cosf(a)*speed*256)};
}
void cp_game_rod(const cp_game_t *g,float seconds,cp_rod_t *rod){
    unsigned n=g->rogue.active?g->rogue.relic[CP_R_ROD]:0;
    float scale=1+n*.25f;cp_feeder_rod(seconds*scale,rod);
    if(n){rod->vx=(int32_t)(rod->vx*scale);rod->vz=(int32_t)(rod->vz*scale);}
}
static cp_rod_t s_rod;
static inline __attribute__((always_inline)) gv3 capsule_normal(int32_t dx,int32_t dz,uint32_t distance){
    uint32_t length2=(uint32_t)(dx*dx+dz*dz);
    if(length2&&length2<65536){uint32_t precise=root32(length2<<16);return gv(dx*1048576/(int32_t)precise,0,dz*1048576/(int32_t)precise);}
    return gv((int32_t)((int64_t)dx*4096/distance),0,(int32_t)((int64_t)dz*4096/distance));
}
/* The guided coins remain rigid bodies. Capsules are extruded along Y;
 * their XZ section meets the upright disk inside a narrow physical channel. */
static CP_HOT void feeder_capsule(cp_game_t *g,unsigned i,int32_t ax,int32_t az,int32_t bx,int32_t bz,int32_t radius,unsigned slot,unsigned bit,bool moving){
    geometry_t *geo=&s_geometry[i];fixed_body_t *body=&s_fixed[i];
    int32_t ex=bx-ax,ez=bz-az,len2=ex*ex+ez*ez,t=0;
    if(len2)t=(int32_t)(((int64_t)(geo->p.x-ax)*ex+(int64_t)(geo->p.z-az)*ez)*4096/len2);
    if(t<0){t=0;}
    if(t>4096){t=4096;}
    int32_t x=ax+(ex*t>>12),z=az+(ez*t>>12),dx=x-geo->p.x,dz=z-geo->p.z;
    uint32_t distance=root32((uint32_t)(dx*dx+dz*dz));
    if(!distance){dx=1;dz=0;distance=1;}
    gv3 n=capsule_normal(dx,dz,distance);
    packed_v3 surface={moving?(int16_t)(s_rod.vx*t>>12):0,0,moving?(int16_t)(s_rod.vz*t>>12):0};
    int32_t depth=gextent(geo,n)+radius-(int32_t)distance;
    fv3 rel=fs(body->sv,raw(surface));int32_t closing=gdot(gv(rel.x,rel.y,rel.z),n);
    if(depth< -24&&(closing<=0||depth+closing/CP_HZ< -24))return;
    unsigned before=s_count;contact_motion(g,i,-1,gsupport(geo,n),n,depth,surface,slot);
    if(s_count>before){contact_t *c=&s_contacts[s_count-1];int32_t vn=velocity_dot(relative(body,NULL,c),c->n);
        if(depth>=0&&vn< -10*256)c->bias-=vn*(moving?12:70)/256;
        if(depth>=-24&&!(g->coins[i].feeder_mask&(1u<<bit))){g->coins[i].feeder_mask|=(uint16_t)(1u<<bit);if(moving)++g->rod_hits;else if(bit<CP_FEEDER_PEGS)++g->peg_hits;}}
}
static void feeder_terrain(cp_game_t *g,unsigned i){
    cp_coin_t *coin=&g->coins[i];geometry_t *geo=&s_geometry[i];gv3 p;
    if(geo->p.x-geo->bound.x<18*256){p=gsupport(geo,gv(-4096,0,0));contact(g,i,-1,p,gv(-4096,0,0),18*256-p.x,0,20);}
    if(geo->p.x+geo->bound.x>190*256){p=gsupport(geo,gv(4096,0,0));contact(g,i,-1,p,gv(4096,0,0),p.x-190*256,0,21);}
    if(coin->feeder==1){
        if(geo->p.z<55*256)coin->feeder=2;
        else{
            if(geo->p.y-geo->bound.y<23*256){p=gsupport(geo,gv(0,-4096,0));contact(g,i,-1,p,gv(0,-4096,0),23*256-p.y,0,22);}
            if(geo->p.y+geo->bound.y>29*256){p=gsupport(geo,gv(0,4096,0));contact(g,i,-1,p,gv(0,4096,0),p.y-29*256,0,23);}
            feeder_capsule(g,i,CP_ROD_X*256,CP_ROD_Z*256,s_rod.x,s_rod.z,3*256,24,15,true);
            for(unsigned peg=0;peg<CP_FEEDER_PEGS;++peg){int32_t x=cp_feeder_pegs[peg].x*256,z=cp_feeder_pegs[peg].z*256;
                if(giabs(geo->p.x-x)>24*256||giabs(geo->p.z-z)>24*256)continue;
                feeder_capsule(g,i,x,z,x,z,640,25+peg,peg,false);}
            return;
        }
    }
    if(coin->feeder==2){
        if(geo->p.y>72*256){unsigned lane=coin->p.x<78?0:coin->p.x>130?2:1;coin->feeder=0;++g->feeder_exits;++g->exit_bins[lane];if(!g->rogue.active||coin->paid)cp_arcade_gate(g,lane);return;}
        gv3 n=gv(0,-1242,-3903);p=gsupport(geo,n);
        int32_t depth=((42*256-p.z)*3903-(p.y-26*256)*1242)>>12;
        fv3 v=s_fixed[i].sv;int32_t closing=gdot(gv(v.x,v.y,v.z),n);
        if(depth>=-24||depth+closing/CP_HZ>=-24)contact(g,i,-1,p,n,depth,0,39);
        if(geo->p.y-geo->bound.y<22*256){p=gsupport(geo,gv(0,-4096,0));contact(g,i,-1,p,gv(0,-4096,0),22*256-p.y,0,40);}
    }
}
static gv3 s_floor_poly[2][16];
static unsigned clip_floor(unsigned count,unsigned src,unsigned axis,int32_t limit,bool minimum){
    gv3 *in=s_floor_poly[src],*out=s_floor_poly[src^1];unsigned n=0;
    for(unsigned i=0;i<count;++i){gv3 a=in[i],b=in[(i+1)%count];int32_t av=axis?a.y:a.x,bv=axis?b.y:b.x;
        bool ai=minimum?av>=limit:av<=limit,bi=minimum?bv>=limit:bv<=limit;
        if(ai)out[n++]=a;
        if(ai!=bi){int32_t t=(limit-av)*65536/(bv-av);gv3 q=gv(a.x+((b.x-a.x)*t>>16),a.y+((b.y-a.y)*t>>16),a.z+((b.z-a.z)*t>>16));
            if(axis)q.y=limit;else q.x=limit;out[n++]=q;}}
    return n;
}
static __attribute__((noinline)) void ground(cp_game_t *g,unsigned i,int height,int xmin,int xmax,int ymin,int ymax,int speed,unsigned base){
    geometry_t *b=&s_geometry[i];fixed_body_t *f=&s_fixed[i];int32_t angular_down=(giabs(f->sw.x)*b->bound.y+giabs(f->sw.y)*b->bound.x)/(4096*CP_HZ);
    if(b->p.z-b->bound.z+(f->sv.z<0?f->sv.z/CP_HZ:0)-angular_down>height+24||b->p.z+b->bound.z<height||b->p.z<height-1024||b->p.x+b->bound.x<xmin||b->p.x-b->bound.x>xmax||b->p.y+b->bound.y<ymin||b->p.y-b->bound.y>ymax)return;
    gv3 cap=gm(gload(b->axis[2]),b->half.z);if(cap.z>0)cap=gv(-cap.x,-cap.y,-cap.z);
    unsigned count=b->vertices,src=0;
    for(unsigned k=0;k<count;++k){unsigned index=count==4?k^(k>>1):k;s_floor_poly[0][k]=ga(b->p,ga(geometry_ring(b,index),cap));}
    if(b->p.x-b->bound.x<=xmin+2){count=clip_floor(count,src,0,xmin,true);src^=1;}
    if(b->p.x+b->bound.x>=xmax-2){count=clip_floor(count,src,0,xmax,false);src^=1;}
    if(b->p.y-b->bound.y<=ymin+2){count=clip_floor(count,src,1,ymin,true);src^=1;}
    if(b->p.y+b->bound.y>=ymax-2){count=clip_floor(count,src,1,ymax,false);src^=1;}
    gv3 best[4]={gv(0,0,INT32_MAX),gv(0,0,INT32_MAX),gv(0,0,INT32_MAX),gv(0,0,INT32_MAX)};int32_t score[4]={0};
    for(unsigned k=0;k<count;++k){gv3 v=s_floor_poly[src][k];if(v.x<xmin||v.x>xmax||v.y<ymin||v.y>ymax)continue;
        int32_t dx=v.x-b->p.x,dy=v.y-b->p.y;unsigned sector=(dx<0?1:0)+(dy<0?2:0);
        if(b->n==5){int32_t u=dx*2,v=-dx+((dy*7094)>>12),w=-dx-((dy*7094)>>12);sector=u>=v&&u>=w?0:v>=w?1:2;}
        int32_t outward=giabs(dx)+giabs(dy);
        if(v.z<best[sector].z||(v.z==best[sector].z&&outward>score[sector])){best[sector]=v;score[sector]=outward;}}
    for(unsigned sector=0;sector<(b->n==5?3u:4u);++sector){gv3 p=best[sector];if(p.z==INT32_MAX)continue;
        packed_v3 arm={(int16_t)(p.x-b->p.x),(int16_t)(p.y-b->p.y),(int16_t)(p.z-b->p.z)};int32_t vz=f->sv.z+arm_velocity(f->sw,arm).z;
        if(p.z<height+24||p.z+(vz<0?vz/CP_HZ:0)<=height+24)contact(g,i,-1,p,gv(0,0,-4096),height-p.z,speed,base+sector);}
}
/* Rare side-delivery geometry stays in Flash, outside the IRAM solver path. */
static __attribute__((noinline)) void tower_terrain(cp_game_t *g,unsigned i){
    geometry_t *b=&s_geometry[i];
        ground(g,i,0,g->arcade.tower_side?196*256:-10*256,g->arcade.tower_side?218*256:12*256,84*256,126*256,0,44);
        int32_t wall=cp_tower_pusher_mm(g)*256;
        gv3 n=gv(g->arcade.tower_side?4096:-4096,0,0),p=gsupport(b,n);
        int depth=g->arcade.tower_side?p.x-wall:wall-p.x;
        if(depth>=-24&&depth<24*256&&b->p.z<24*256&&b->p.z>-4*256)
            contact_motion(g,i,-1,p,n,depth,(packed_v3){g->arcade.tower_side?-4267:4267,0,0},48);
        /* A transparent front retainer and side guides travel with the tray.
         * They keep a tower upright during delivery, then release it completely. */
        if(b->p.z<24*256&&b->p.z>-4*256&&giabs(b->p.x-wall)<24*256){
            int32_t front=wall+(g->arcade.tower_side?-17:17)*256;
            bool inside=g->arcade.tower_side?b->p.x>front:b->p.x<front;
            int sign=(g->arcade.tower_side?-1:1)*(inside?1:-1);
            gv3 forward=gv(sign*4096,0,0);p=gsupport(b,forward);
            int gap=sign>0?p.x-front:front-p.x;
            if(gap>=-24&&gap<16*256)contact_motion(g,i,-1,p,forward,gap,(packed_v3){g->arcade.tower_side?-4267:4267,0,0},49);
            p=gsupport(b,gv(0,-4096,0));if(p.y<87*256)contact(g,i,-1,p,gv(0,-4096,0),87*256-p.y,0,50);
            p=gsupport(b,gv(0,4096,0));if(p.y>123*256)contact(g,i,-1,p,gv(0,4096,0),p.y-123*256,0,51);
        }
}
static __attribute__((noinline)) void terrain(cp_game_t *g,unsigned i){
    if(g->coins[i].feeder){feeder_terrain(g,i);return;}
    geometry_t *b=&s_geometry[i];int32_t front=s_front,speed=s_surface_speed,return_speed=g->circulation?(g->rogue.active?10:18)*256:0;
    ground(g,i,18*256,12*256,196*256,18*256,front,speed,0);
    bool hole=g->arcade.hole_ticks&&b->p.y>104*256&&b->p.y<140*256&&
        (int64_t)(b->p.x-104*256)*(b->p.x-104*256)+(int64_t)(b->p.y-122*256)*(b->p.y-122*256)<(int64_t)(18*256)*(18*256);
    if(hole)g->coins[i].feeder_mask|=CP_BODY_SIPHON;
    if(!hole){
    if(b->p.y+b->bound.y<=CP_SIDE_EDGE*256)ground(g,i,0,12*256,196*256,0,CP_SIDE_EDGE*256,return_speed,4);
    else{ground(g,i,0,(g->guard_ticks?12:18)*256,(g->guard_ticks?196:190)*256,0,(int32_t)(CP_FRONT_EDGE*256),return_speed,4);ground(g,i,0,12*256,18*256,0,CP_SIDE_EDGE*256,return_speed,8);ground(g,i,0,190*256,196*256,0,CP_SIDE_EDGE*256,return_speed,12);}
    }
    bool opening=g->arcade.tower_ticks&&b->p.y>84*256&&b->p.y<126*256;
    if(opening)tower_terrain(g,i);
    gv3 p;
    if(b->p.z>0&&b->p.y<(g->guard_ticks?CP_FRONT_EDGE:CP_SIDE_EDGE)*256){
        if(!(opening&&!g->arcade.tower_side)&&b->p.x-b->bound.x<12*256){p=gsupport(b,gv(-4096,0,0));if(p.x<12*256)contact(g,i,-1,p,gv(-4096,0,0),12*256-p.x,0,16);}
        if(!(opening&&g->arcade.tower_side)&&b->p.x+b->bound.x>196*256){p=gsupport(b,gv(4096,0,0));if(p.x>196*256)contact(g,i,-1,p,gv(4096,0,0),p.x-196*256,0,17);}
    }
    if(b->p.z+b->bound.z>18*256&&b->p.z-b->bound.z<43*256&&b->p.y-b->bound.y<18*256){p=gsupport(b,gv(0,-4096,0));if(p.y<18*256)contact(g,i,-1,p,gv(0,-4096,0),18*256-p.y,0,18);}
    if(b->p.z+b->bound.z>0&&b->p.z-b->bound.z<18*256&&b->p.y>front-16*256&&b->p.y-b->bound.y<front){p=gsupport(b,gv(0,-4096,0));p.z=b->p.z<0?0:b->p.z>18*256?18*256:b->p.z;if(p.y<front)contact(g,i,-1,p,gv(0,-4096,0),front-p.y,speed,19);}
}
static int32_t previous_velocity(float value){
    union {float f;uint32_t u;} bits={.f=value};unsigned exponent=(bits.u>>23)&255;uint32_t magnitude;
    int shift=(int)exponent-142;
    if(shift>=0)magnitude=shift>7?100000u:((bits.u&0x7fffffu)|0x800000u)<<(unsigned)shift;
    else magnitude=shift<=-32?0:((bits.u&0x7fffffu)|0x800000u)>>(unsigned)-shift;
    if(magnitude>100000u)magnitude=100000u;
    return bits.u>>31?-(int32_t)magnitude:(int32_t)magnitude;
}
#define CP_PAIR_WORDS ((CP_MAX_COINS+31)/32)
static uint32_t s_candidate[CP_PAIR_WORDS];
static int32_t s_broad_width;
static int32_t broad_min(const cp_game_t *g,unsigned i){return g->coins[i].active?s_geometry[i].p.x-s_geometry[i].bound.x:INT32_MAX;}
static CP_HOT void broadphase(const cp_game_t *g){
    s_broad_width=0;
    for(unsigned i=0;i<CP_MAX_COINS;++i)if(g->coins[i].active&&2*s_geometry[i].bound.x>s_broad_width)s_broad_width=2*s_geometry[i].bound.x;
    for(unsigned i=1;i<CP_MAX_COINS;++i){uint16_t item=s_broad_order[i];int32_t key=broad_min(g,item);unsigned j=i;
        while(j&&broad_min(g,s_broad_order[j-1])>key){s_broad_order[j]=s_broad_order[j-1];--j;}s_broad_order[j]=item;}
}
/* Stream one exact X-overlap row instead of retaining a quadratic bit matrix.
 * A global maximum width gives a conservative lower bound for the binary search.
 * Bit iteration retains the original body-index contact order. */
static void broad_row(const cp_game_t *g,unsigned i){
    memset(s_candidate,0,sizeof(s_candidate));
    int32_t start=broad_min(g,i),end=s_geometry[i].p.x+s_geometry[i].bound.x+24;
    int32_t lower=start-s_broad_width-24;unsigned lo=0,hi=CP_MAX_COINS;
    while(lo<hi){unsigned mid=(lo+hi)/2;if(broad_min(g,s_broad_order[mid])<lower)lo=mid+1;else hi=mid;}
    for(unsigned k=lo;k<CP_MAX_COINS;++k){unsigned j=s_broad_order[k];if(broad_min(g,j)>=end)break;
        if(j>i&&s_geometry[j].p.x+s_geometry[j].bound.x+24>start)s_candidate[j/32]|=1u<<(j%32);}
}
static CP_HOT void pairs(cp_game_t *g){
    broadphase(g);
    for(unsigned i=0;i<CP_MAX_COINS;++i){
        cp_coin_t *a=&g->coins[i];if(!a->active)continue;geometry_t *ag=&s_geometry[i];broad_row(g,i);
        for(unsigned word=0;word<CP_PAIR_WORDS;++word){uint32_t candidates=s_candidate[word];
        while(candidates){unsigned j=word*32+(unsigned)__builtin_ctz(candidates);candidates&=candidates-1;
            cp_coin_t *b=&g->coins[j];if(!b->active||(a->sleeping&&b->sleeping))continue;geometry_t *bg=&s_geometry[j];
            gv3 n;int32_t depth;bool speculative=false;
            if(giabs(ag->p.x-bg->p.x)>=ag->bound.x+bg->bound.x+24||giabs(ag->p.y-bg->p.y)>=ag->bound.y+bg->bound.y+24)continue;
            if(!pair_overlap(g,i,j,&n,&depth)){
                /* A thin, almost horizontal disk can cross an entire coin cap
                 * in one 30-Hz step. Test the swept cap interval and footprint. */
                if(ag->n!=5||bg->n!=5||giabs(ag->axis[2].z)<4055||giabs(bg->axis[2].z)<4055)continue;
                int32_t dz=bg->p.z-ag->p.z,sign=dz>=0?1:-1;
                int32_t gap=giabs(dz)-ag->half.z-bg->half.z,closing=sign*(s_fixed[i].sv.z-s_fixed[j].sv.z);
                if(gap<=0||closing<=0||closing/CP_HZ<gap||giabs(ag->p.x-bg->p.x)>=ag->bound.x+bg->bound.x||giabs(ag->p.y-bg->p.y)>=ag->bound.y+bg->bound.y)continue;
                int32_t saved_z=ag->p.z;ag->p.z=bg->p.z;bool footprint=overlap(ag,bg,&n,&depth);ag->p.z=saved_z;
                if(!footprint){continue;}n=gload(ag->axis[2]);if(sign*n.z<0)n=gv(-n.x,-n.y,-n.z);depth=-gap;speculative=true;
            }
            if(!speculative&&depth>=0&&(a->special||b->special))cp_tactics_touch(g,i,j);
            gv3 sa=gsupport(ag,n),sb=gsupport(bg,gv(-n.x,-n.y,-n.z)),p=gv((sa.x+sb.x)/2,(sa.y+sb.y)/2,(sa.z+sb.z)/2);
            if(a->sleeping||b->sleeping){
                /* Classify impact from last solved relative contact motion.
                 * Adding this tick's gravity to just the awake partner must
                 * not turn a resting contact into a fictitious impact. */
                gv3 ar=gs(p,ag->p),br=gs(p,bg->p);packed_v3 ra={(int16_t)ar.x,(int16_t)ar.y,(int16_t)ar.z},rb={(int16_t)br.x,(int16_t)br.y,(int16_t)br.z};
                fv3 va=fv(previous_velocity(a->v.x),previous_velocity(a->v.y),previous_velocity(a->v.z)),vb=fv(previous_velocity(b->v.x),previous_velocity(b->v.y),previous_velocity(b->v.z));
                fv3 motion=fs(fa(vb,arm_velocity(s_fixed[j].sw,rb)),fa(va,arm_velocity(s_fixed[i].sw,ra)));
                bool impact=depth>64||gdot(gv(motion.x,motion.y,motion.z),n)<-3*256;
                if(a->sleeping){a->sleeping=false;a->quiet=impact?0:.5f;a->support_count=0;s_fixed[i].sv.z-=(380*256+CP_HZ/2)/CP_HZ;terrain(g,i);}
                if(b->sleeping){b->sleeping=false;b->quiet=impact?0:.5f;b->support_count=0;s_fixed[j].sv.z-=(380*256+CP_HZ/2)/CP_HZ;terrain(g,j);}}

            if(ag->n==5&&bg->n==5&&giabs(n.z)<614&&giabs(ag->axis[2].z)>4055&&giabs(bg->axis[2].z)>4055)p.z=(ag->p.z+bg->p.z)/2;
            int32_t da=gdot(n,gload(ag->axis[2])),db=gdot(n,gload(bg->axis[2]));
            if(giabs(da)>3932&&giabs(db)>3932){
                gv3 d=gs(bg->p,ag->p);d=gs(d,gm(n,gdot(d,n)));
                uint32_t distance=root32((uint32_t)(d.x*d.x+d.y*d.y+d.z*d.z));
                int32_t radius=(ag->half.x<bg->half.x?ag->half.x:bg->half.x)-(int32_t)distance/2;if(radius<26)radius=26;radius/=2;
                gv3 mid=gv((ag->p.x+bg->p.x)/2,(ag->p.y+bg->p.y)/2,(ag->p.z+bg->p.z)/2);mid=ga(mid,gm(n,gdot(gs(p,mid),n)));
                gv3 ca=ga(ag->p,gm(gload(ag->axis[2]),da>0?ag->half.z:-ag->half.z)),cb=ga(bg->p,gm(gload(bg->axis[2]),db>0?-bg->half.z:bg->half.z));
                static const int16_t cs[3]={4096,-2048,-2048},sn[3]={0,3547,-3547};
                for(unsigned k=0;k<3;++k){gv3 sample=ga(mid,gtransform(ag,(radius*cs[k])>>12,(radius*sn[k])>>12,0));
                    int32_t ta=gdot(gs(ca,sample),gload(ag->axis[2]))*4096/da,tb=gdot(gs(cb,sample),gload(bg->axis[2]))*4096/db;
                    if(speculative||ta-tb>=-24)contact(g,i,j,ga(sample,gm(n,(ta+tb)/2)),n,ta-tb,0,k);
                }
            }else contact(g,i,j,p,n,depth,0,3);
        }
    }
}
}
void cp_rebuild_support(cp_game_t *g)
{
    for(unsigned i=0;i<CP_MAX_COINS;++i)s_broad_order[i]=(uint16_t)i;
    cp_game_rod(g,g->game_time,&s_rod);
    bool resting[CP_MAX_COINS];s_count=0;s_surface_count=0;memset(s_warm,0,sizeof(s_warm));s_front=44*256+(int32_t)(g->pusher_y*256);s_surface_speed=(int32_t)(g->pusher_v*256);
    for(unsigned i=0;i<CP_MAX_COINS;++i) {
        resting[i]=g->coins[i].sleeping;g->coins[i].sleeping=false;g->coins[i].support_count=0;
        if(g->coins[i].active) {fixed_body(i,&g->coins[i],false);terrain(g,i);}
    }
    pairs(g);
    for(unsigned i=0;i<CP_MAX_COINS;++i) g->coins[i].sleeping=resting[i]&&g->coins[i].support_count>0;
}
static CP_HOT void solve(cp_game_t *g)
{
    /* The shared-Jacobian hash added a complete coefficient calculation to
     * every dense contact before iteration. Measurements on 256 bodies showed
     * that the hash is slower than recomputing uncached coefficients locally. */
    unsigned cache_count=(sizeof(s_workspace.bytes)-s_count*sizeof(contact_t))/sizeof(jacobian_t);
    if(cache_count>s_count)cache_count=s_count;
#ifdef CP_DISABLE_JACOBIAN_CACHE
    cache_count=0;
#endif
    unsigned cache_first=s_count-cache_count;
    jacobian_t *cache=(jacobian_t *)(s_workspace.bytes+s_count*sizeof(contact_t));
    for(unsigned k=cache_first;k<s_count;++k)jacobian(&s_contacts[k],&cache[k-cache_first],false);
#define CONTACT_JAC(I,C,L) ((I)>=cache_first?&cache[(I)-cache_first]:(L))

    for(unsigned k=0;k<s_count;++k) {
        contact_t *c=&s_contacts[k];if(!(c->normal|c->f1|c->f2))continue;jacobian_t local;const jacobian_t *jac=CONTACT_JAC(k,c,&local);if(jac==&local)jacobian(c,&local,!(c->f1|c->f2));impulse(&s_fixed[c->a],c->b>=0?&s_fixed[c->b]:NULL,c,jac,c->normal,c->f1,c->f2);
    }
    for(unsigned pass=0;pass<SOLVER_PASSES;++pass)for(unsigned k=0;k<s_count;++k) {
        unsigned index=pass%2?s_count-1-k:k;contact_t *c=&s_contacts[index];fixed_body_t *a=&s_fixed[c->a],*b=c->b>=0?&s_fixed[c->b]:NULL;
        jacobian_t local;const jacobian_t *jac=CONTACT_JAC(index,c,&local);if(jac==&local)jacobian(c,&local,false);packed_v3 t1=jac->t1,t2=jac->t2;fv3 rel=relative(a,b,c);
        int32_t old=c->normal;c->normal=old+((c->bias-velocity_dot(rel,c->n))*jac->in>>12);if(c->normal<0)c->normal=0;
        int32_t jn=c->normal-old;
        /* Effective-mass coupling gives the exact tangent velocity change
         * from the normal impulse. Apply normal/friction together once. */
        int32_t v1=velocity_dot(rel,t1)+(int32_t)((int64_t)jac->kn1*jn>>14),v2=velocity_dot(rel,t2)+(int32_t)((int64_t)jac->kn2*jn>>14),f1=c->f1,f2=c->f2;
        c->f1-=((v1*jac->in1+v2*jac->incross)>>12);c->f2-=((v1*jac->incross+v2*jac->in2)>>12);
        /* Polished stationary payout bed must slide under a loaded pile.
         * The driven upper shelf keeps its traction; coin-to-coin friction
         * still supports towers without bonding the whole pile together. */
        unsigned slot=c->slot;
        int32_t friction=b?64:slot<4?140:slot==24?8:slot>=19?30:40;
        if(g->rogue.active&&!b&&slot>=4&&slot<16)friction=friction*g->rogue.friction/100;
        int32_t limit=(c->normal*friction)>>8;
        if(!b&&slot==39){
            /* Longitudinal grooves guide travel toward the deck while
             * scrubbing sideways skid. This is contact friction, not a
             * replacement/selection of the final X coordinate. */
            int32_t lateral=c->normal*100>>8,longitudinal=c->normal*22>>8;
            if(c->f1>lateral){c->f1=lateral;}
            if(c->f1< -lateral){c->f1=-lateral;}
            if(c->f2>longitudinal){c->f2=longitudinal;}
            if(c->f2< -longitudinal){c->f2=-longitudinal;}
        }else{
        uint64_t length2=(uint64_t)((int64_t)c->f1*c->f1+(int64_t)c->f2*c->f2);
        if(length2>(uint64_t)((int64_t)limit*limit)) {
            /* One Newton correction from a tight two-component norm estimate.
             * Error below 0.3%, without an iterative sqrt in every solver pass. */
            uint32_t x=(uint32_t)(c->f1<0?-c->f1:c->f1),y=(uint32_t)(c->f2<0?-c->f2:c->f2);
            uint32_t hi=x>y?x:y;unsigned shift=hi>32767?32-__builtin_clz(hi)-15:0;
            x>>=shift;y>>=shift;uint32_t lo=x>y?y:x;hi=x>y?x:y;
            uint32_t guess=hi+(lo*3)/8,length=(guess+(x*x+y*y)/guess)/2;
            int32_t ratio=(int32_t)(((uint32_t)limit>>shift)*65536u/length);
            c->f1=fm(c->f1,ratio);c->f2=fm(c->f2,ratio);
        }
        }
        impulse(a,b,c,jac,jn,c->f1-f1,c->f2-f2);
    }
    /* Re-project support normals after tangent coupling. Low-friction bed
     * contacts must carry gravity without relying on friction to prop a disk
     * upright. Two cheap support-normal sweeps prevent persistent sinking. */
    for(unsigned pass=0;pass<2;++pass)for(unsigned k=0;k<s_count;++k){
        unsigned index=pass?s_count-1-k:k;contact_t *c=&s_contacts[index];if(c->n.z>-15000&&c->n.z<15000)continue;
        fixed_body_t *a=&s_fixed[c->a],*b=c->b>=0?&s_fixed[c->b]:NULL;jacobian_t local;const jacobian_t *jac=CONTACT_JAC(index,c,&local);if(jac==&local)jacobian(c,&local,true);int32_t old=c->normal;
        c->normal=old+((c->bias-velocity_dot(relative(a,b,c),c->n))*jac->in>>12);
        if(c->normal<0)c->normal=0;
        impulse(a,b,c,jac,c->normal-old,0,0);
    }
    for(unsigned k=0;k<s_count;++k) {
        contact_t *c=&s_contacts[k];uint32_t key=contact_key(c);packed_v3 t1,t2;contact_basis(c,&t1,&t2);warm_t *w=&s_warm[warm_index(key)];
        *w=(warm_t){key,g->steps,cache_normal(c->normal),cache_vector(fa(linear_j(t1,c->f1),linear_j(t2,c->f2))),
            (int8_t)((c->n.x*127)>>14),(int8_t)((c->n.y*127)>>14),(int8_t)((c->n.z*127)>>14)};
    }

}
#undef CONTACT_JAC
static __attribute__((noinline)) void rogue_forces(cp_game_t *g){
    cp_rogue_t *r=&g->rogue;if(!r->active)return;
    bool pulse=r->storm_ticks&&g->steps%12==0;
    for(unsigned i=0;i<CP_MAX_COINS;++i){cp_coin_t *b=&g->coins[i];
        if(!b->active||b->feeder||b->p.y<82||b->p.y>CP_FRONT_EDGE||b->p.x<12||b->p.x>196||b->p.z<0)continue;
        fixed_body_t *f=&s_fixed[i];const geometry_t *p=&s_geometry[i];
        bool force=r->relic[CP_R_MAGNET]||r->float_ticks||pulse||r->ripple_ticks==11;
        if(force){b->sleeping=false;b->quiet=0;}
        if(b->sleeping)continue;
        if(r->gravity<100)f->sv.z+=(380*256*(100-r->gravity))/(CP_HZ*100);
        if(r->relic[CP_R_MAGNET]){int strength=r->relic[CP_R_MAGNET]+((r->synergies&16)?1:0);
            f->sv.x+=(104*256-p->p.x)*strength/600;f->sv.y+=strength*18;}
        if(r->float_ticks){f->sv.z+=380*256*r->gravity/(CP_HZ*100);
            int target=(34*256-p->p.z)*2;if(target>25*256)target=25*256;if(target<-25*256)target=-25*256;
            f->sv.z+=(target-f->sv.z)/5;}
        if(pulse){f->sv.x+=(p->p.x<104*256?-1:1)*10*256;f->sv.y+=10*256;f->sv.z+=(18+r->relic[CP_R_THUNDER]*4)*256;}
        if(r->ripple_ticks==11){int dx=p->p.x-r->ripple_x*256,dy=p->p.y-r->ripple_y*256;
            if(giabs(dx)+giabs(dy)<70*256){int n=r->relic[CP_R_RIPPLE];f->sv.x+=dx*n/32;f->sv.y+=dy*n/32;f->sv.z+=3*n*256;}}
    }
}
static void tick(cp_game_t *g)
{
    uint32_t t0=clock_now(),t1,t2,t3;
    cp_game_rod(g,g->game_time+CP_DT,&s_rod);
    s_count=0;s_surface_count=0;s_penetration=0;s_factory_cycles=0;g->cache_hits=g->sat_calls=0;g->max_penetration=0;int32_t old_front=44*256+(int32_t)(g->pusher_y*256);
    /* Sleeping is conditional on the contact support graph. A moving/removed
     * support wakes the full stack, including coins no longer touching it. */
    for(unsigned pass=0;pass<3;++pass) for(unsigned i=0;i<CP_MAX_COINS;++i) {
        cp_coin_t *b=&g->coins[i];if(!b->active||!b->sleeping) continue;
        const fixed_body_t *f=&s_fixed[i];const geometry_t *geo=&s_geometry[i];gv3 p,bound;
        if(f->valid&&pose_position(f,b)&&pose_rotation(geo,b)){p=geo->p;bound=gload(geo->bound);}
        else{p=gv((int32_t)(b->p.x*256),(int32_t)(b->p.y*256),(int32_t)(b->p.z*256));bound=gv((int32_t)(b->bound.x*256),(int32_t)(b->bound.y*256),(int32_t)(b->bound.z*256));}
        /* Only contact with the moving shelf wakes a supported island.
         * Tall lower-bed coins are not on that shelf merely because z>14. */
        bool shelf=p.z-bound.z<=18*256+24&&p.z+bound.z>=18*256&&
            p.y-bound.y<old_front+24&&p.y+bound.y>18*256;
        bool wake=!b->support_count||shelf||
            (p.z-bound.z<18*256&&p.y-bound.y<old_front+51&&p.y>old_front-16*256);
        for(unsigned k=0;k<b->support_count;++k) if(b->support[k]>=0) {
            cp_coin_t *s=&g->coins[b->support[k]];if(!s->active||!s->sleeping) wake=true;
        }
        if(wake){b->sleeping=false;b->quiet=0;}
    }
    for(unsigned i=0;i<CP_MAX_COINS;++i){cp_coin_t *b=&g->coins[i];if(!b->active||b->feeder)continue;
        bool tray=g->arcade.tower_ticks&&b->p.y>84&&b->p.y<126&&(g->arcade.tower_side?b->p.x>150:b->p.x<58);
        if(tray||((g->arcade.hole_ticks||g->arcade.shake_ticks||(g->rogue.active&&(g->rogue.relic[CP_R_MAGNET]||g->rogue.float_ticks||g->rogue.storm_ticks||g->rogue.ripple_ticks)))&&b->p.y>82)){b->sleeping=false;b->quiet=0;}}
    float previous=g->pusher_y;
    g->phase+=CP_DT*((g->rush_ticks||g->circulation||g->arcade.fever_ticks)?2*PI/2.1f:2*PI/2.8f);if(g->phase>2*PI) g->phase-=2*PI;
    g->pusher_y=8+(g->rogue.active?g->rogue.stroke:35)*(1-cosf(g->phase))*.5f;
    g->pusher_v=(g->pusher_y-previous)/CP_DT;
    for(unsigned i=0;i<CP_MAX_COINS;++i) {
        cp_coin_t *b=&g->coins[i];if(!b->active) continue;
        if(!b->sleeping) {
            b->support_count=0;

            /* Axes were updated after the previous integration. */
        }
    }
    for(unsigned i=0;i<CP_MAX_COINS;++i) if(g->coins[i].active)fixed_body(i,&g->coins[i],true);
    for(unsigned i=0;i<CP_MAX_COINS;++i){cp_coin_t *b=&g->coins[i];if(!b->active||b->feeder||b->p.y<82)continue;
        fixed_body_t *f=&s_fixed[i];
        if(g->arcade.hole_ticks){int32_t dx=104*256-s_geometry[i].p.x,dy=122*256-s_geometry[i].p.y;
            uint32_t d=root32((uint32_t)((int64_t)dx*dx+(int64_t)dy*dy));if(d<256)d=256;
            int32_t vx=(int32_t)((int64_t)(dx*155-dy*25)*256/d),vy=(int32_t)((int64_t)(dy*155+dx*25)*256/d);
            f->sv.x+=(vx-f->sv.x)/4;f->sv.y+=(vy-f->sv.y)/4;f->sw.z=3*4096;
            if(d<18*256){b->feeder_mask|=CP_BODY_SIPHON;f->sv.z-=10*256;}}
        if(g->arcade.shake_ticks&&g->steps%15==0){
            /* The cabinet shakes the bed. Upper coins receive the impulse
             * through contacts instead of every layer being launched at once. */
            if(!g->rogue.active||s_geometry[i].p.z-s_geometry[i].bound.z<384){
                f->sv.x+=(g->steps%30?1:-1)*(g->rogue.active?18:35)*256;
                f->sv.y+=(g->rogue.active?12:22)*256;f->sv.z+=(g->rogue.active?12:35)*256;
            }
        }
    }
    rogue_forces(g);
    s_front=44*256+(int32_t)(g->pusher_y*256);s_surface_speed=(int32_t)(g->pusher_v*256);
    t1=clock_now();
    for(unsigned i=0;i<CP_MAX_COINS;++i) if(g->coins[i].active&&!g->coins[i].sleeping) terrain(g,i);
    uint32_t after_ground=clock_now();pairs(g);t2=clock_now();g->ground_us=after_ground-t1;g->pairs_us=t2-after_ground;g->factory_us=s_factory_cycles/160;solve(g);t3=clock_now();
    for(unsigned i=0;i<CP_MAX_COINS;++i) {
        cp_coin_t *b=&g->coins[i];if(!b->active) continue;
        if(!b->sleeping){fixed_body_t *f=&s_fixed[i];f->p=fa(f->p,fv((int32_t)(((int64_t)f->sv.x*DT16)>>8),(int32_t)(((int64_t)f->sv.y*DT16)>>8),(int32_t)(((int64_t)f->sv.z*DT16)>>8)));
            b->p=cp_v(cp_fixed_float(f->p.x,16),cp_fixed_float(f->p.y,16),cp_fixed_float(f->p.z,16));s_geometry[i].p=gv(f->p.x>>8,f->p.y>>8,f->p.z>>8);integrate_orientation(i,b);}
        if(b->p.z<-26) {
            if((b->feeder_mask&CP_BODY_SIPHON)||(b->p.y>CP_FRONT_EDGE-7&&b->p.x>=18&&b->p.x<=190)) {
                unsigned value=box(b)?(b->kind==CP_PRIZE_RED?40:b->kind==CP_PRIZE_BLUE?60:25):(b->kind==CP_CHIP?25:1);
                if(g->rogue.active&&box(b))value=b->kind==CP_PRIZE_RED?18:b->kind==CP_PRIZE_BLUE?24:12;
                if(g->rogue.active&&b->kind==CP_CHIP)value=12;
                if(b->trait&CP_TRAIT_DOUBLE)value*=2;
                if(g->arcade.fever_ticks){if(g->rogue.active)value+=(value+(g->collected&1u))/2;else value*=2;}
                cp_tactics_collect(g,i);
                if(b->feeder_mask&CP_BODY_SIPHON)++g->arcade.siphoned;
                if(g->rogue.active)cp_rogue_collect(g,b->kind,(b->feeder_mask&CP_BODY_SIPHON)!=0,(int)b->p.x,(int)b->p.y,value);
                else{g->bank+=value;g->score+=value;}++g->collected;
                if(box(b)){++g->prizes;
                    if(g->features){g->collection_mask|=(uint8_t)(1u<<(b->kind-CP_PRIZE_RED));
                        bool complete=g->collection_mask==7;
                        if(complete&&g->rogue.active){g->collection_mask=0;complete=++g->rogue.seal_progress>=3;if(complete)g->rogue.seal_progress=0;}
                        cp_reward_event(g,complete?CP_EVENT_JACKPOT:(cp_event_t)(1+b->kind-CP_PRIZE_RED));}
                }
                if(g->score>g->best) g->best=g->score;
                g->payout_flash=.8f;
            } else{++g->lost;cp_tactics_lost(g,i);if(!box(b)&&b->kind!=CP_CHIP)cp_rogue_lost(g);}
            if((b->feeder_mask&CP_BODY_CLONE)&&g->bonus_queue<4096){++g->bonus_queue;++g->arcade.clones;}
            b->active=false;continue;
        }
        if(!b->sleeping) {
            fixed_body_t *f=&s_fixed[i];f->sw=fv((f->sw.x*4088)>>12,(f->sw.y*4088)>>12,(f->sw.z*4088)>>12);
            uint64_t v2=(uint64_t)((int64_t)f->sv.x*f->sv.x+(int64_t)f->sv.y*f->sv.y+(int64_t)f->sv.z*f->sv.z);
            uint64_t w2=(uint64_t)((int64_t)f->sw.x*f->sw.x+(int64_t)f->sw.y*f->sw.y+(int64_t)f->sw.z*f->sw.z);
            if(v2<1048576&&w2<1509949&&b->support_count&&f->p.y>99*65536&&f->p.z>-65536)b->quiet+=CP_DT;else b->quiet=0;
            if(b->quiet>.35f){b->sleeping=true;f->sv=f->sw=fv(0,0,0);}
            if(v2>3774873600ull){uint32_t len=root32((uint32_t)(v2>>8));f->sv=fv((int32_t)((int64_t)f->sv.x*3840/len),(int32_t)((int64_t)f->sv.y*3840/len),(int32_t)((int64_t)f->sv.z*3840/len));}
            if(w2>3288334336ull){uint32_t len=root32((uint32_t)(w2>>8));f->sw=fv((int32_t)((int64_t)f->sw.x*3584/len),(int32_t)((int64_t)f->sw.y*3584/len),(int32_t)((int64_t)f->sw.z*3584/len));}
            b->v=cp_v(cp_fixed_float(f->sv.x,8),cp_fixed_float(f->sv.y,8),cp_fixed_float(f->sv.z,8));b->w=cp_v(cp_fixed_float(f->sw.x,12),cp_fixed_float(f->sw.y,12),cp_fixed_float(f->sw.z,12));
        }
    }
    g->max_penetration=cp_fixed_float(s_penetration,8);
    g->prep_us=t1-t0;g->contact_us=t2-t1;g->solve_us=t3-t2;g->finish_us=clock_now()-t3;
    g->contacts=s_count;if(s_count>g->peak_contacts) g->peak_contacts=s_count;
    g->game_time+=CP_DT;++g->steps;g->payout_flash=fmaxf(0,g->payout_flash-CP_DT);
    gameplay_tick(g);cp_rogue_tick(g);cp_tactics_tick(g);
    if(g->autoplay&&(!g->bonus_queue||g->rogue.active)) {g->auto_clock+=CP_DT;if(g->auto_clock>(g->rogue.active?.95f:.6f)){g->auto_clock=0;(void)cp_drop(g);}}
}
void cp_step(cp_game_t *g,float elapsed)
{
    if(g->paused||!isfinite(elapsed)||elapsed<=0) return;
    g->accumulator+=fminf(elapsed,.25f);
    while(g->accumulator+1e-6f>=CP_DT) {tick(g);if(g->paused){g->accumulator=0;break;}g->accumulator-=CP_DT;}
}
bool cp_valid(const cp_game_t *g)
{
    if(!g->rng||!isfinite(g->phase)||!isfinite(g->game_time)||g->bank>10000000) return false;
    if(g->rush_ticks>35*CP_HZ||g->guard_ticks>53*CP_HZ||g->event_ticks>4*CP_HZ||g->bonus_clock>CP_HZ||g->bonus_queue>4096||g->collection_mask>7||g->event_kind>CP_EVENT_MAX)return false;
    if(!cp_tactics_valid(&g->tactics)||!cp_rogue_valid(&g->rogue)||!cp_arcade_valid(&g->arcade))return false;
    for(unsigned k=0;k<3;++k)if(g->restock_ticks[k]>6*CP_HZ)return false;
    for(unsigned i=0;i<CP_MAX_COINS;++i) {
        const cp_coin_t *b=&g->coins[i];if(!b->active) continue;
        if(b->special>CP_SPECIAL_COUNT||b->special_ticks>8*CP_HZ||b->feeder>2||b->kind>CP_CHIP||!isfinite(b->p.x)||!isfinite(b->p.y)||!isfinite(b->p.z)||
            !isfinite(b->v.x)||!isfinite(b->v.y)||!isfinite(b->v.z)||!isfinite(b->q.w)||
            !isfinite(b->q.x)||!isfinite(b->q.y)||!isfinite(b->q.z)||
            !isfinite(b->w.x)||!isfinite(b->w.y)||!isfinite(b->w.z)||fabsf(b->p.x)>1000||fabsf(b->p.y)>1000||fabsf(b->p.z)>1000) return false;
    }
    return true;
}

bool cp_pose_fixed(const cp_game_t *g,unsigned i,cp_pose_fixed_t *out){
    if(i>=CP_MAX_COINS||!out){return false;}const cp_coin_t *b=&g->coins[i];const fixed_body_t *f=&s_fixed[i];const geometry_t *geo=&s_geometry[i];
    if(!b->active||!f->valid||f->kind!=b->kind||!pose_position(f,b)||!pose_rotation(geo,b))return false;
    out->p[0]=geo->p.x;out->p[1]=geo->p.y;out->p[2]=geo->p.z;
    for(unsigned k=0;k<3;++k){out->axis[k][0]=geo->axis[k].x;out->axis[k][1]=geo->axis[k].y;out->axis[k][2]=geo->axis[k].z;}
    for(unsigned k=0;k<4;++k)out->q[k]=geo->q[k];
    out->half[0]=geo->half.x;out->half[1]=geo->half.y;out->half[2]=geo->half.z;return true;
}

unsigned cp_prize_count(const cp_game_t *g){unsigned count=0;for(unsigned i=0;i<CP_MAX_COINS;++i)if(g->coins[i].active&&cp_kind_prize(g->coins[i].kind))++count;return count;}
bool cp_next_round(cp_game_t *g){
    if(cp_prize_count(g))return false;
    uint32_t bank=g->bank+(g->prizes==3?20:0),score=g->score,best=g->best,round=g->round;
    cp_init(g,g->rng,bank?bank:30,best,round+1);g->score=score;return true;
}

bool cp_arcade_valid(const cp_arcade_t *a){
    const unsigned bag=(1u<<CP_EVENT_RUSH)|(1u<<CP_EVENT_RAIN)|(1u<<CP_EVENT_SAVE)|(1u<<CP_EVENT_BLACKHOLE)|
        (1u<<CP_EVENT_DOUBLE)|(1u<<CP_EVENT_TOWERS)|(1u<<CP_EVENT_SHAKE)|(1u<<CP_EVENT_FEVER);
    return a->lane<3&&a->result<=CP_EVENT_MAX&&a->last_result<=CP_EVENT_MAX&&a->tower_side<2&&a->next_side<2&&a->tower_queue<=4&&
        a->clock<=18*CP_HZ&&a->spin_ticks<=45&&a->reveal_ticks<=30&&a->cooldown<=10*CP_HZ&&a->play_ticks<=8*CP_HZ&&!(a->bag&~bag)&&
        a->hole_ticks<=28*CP_HZ&&a->double_ticks<=42*CP_HZ&&a->shake_ticks<=18*CP_HZ&&a->fever_ticks<=35*CP_HZ&&
        a->clone_clock<=5&&a->tower_clock<=24*CP_HZ&&a->tower_ticks<=CP_TOWER_TICKS;
}
