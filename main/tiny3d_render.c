#include "tiny3d_render.h"
#include <limits.h>
#include <stdbool.h>

#define C_SCENE_BG 0x11162A
#define C_FLOOR    0x171D31
#define C_GRID     0x26304D
#define C_DIE      0xF7F1E3
#define C_PIP      0x101116
#define C_EDGE     0x8E8B82
#define C_SHADOW   0x080A11

typedef struct { int16_t x, y; int32_t z_q8; } screen_vertex_t;

typedef struct {
    uint8_t v[4];
    t3_vec3_t normal_q14;
    t3_vec3_t center_q8;
    t3_vec3_t u_q8;
    t3_vec3_t v_q8;
    uint8_t value;
} cube_face_t;

typedef struct {
    uint8_t index;
    int32_t depth;
    t3_vec3_t world_normal_q14;
} visible_face_t;

static const cube_face_t FACES[6] = {
    {{3,7,6,2}, {0,T3_Q14_ONE,0},  {0,12 * 256,0},  {6 * 256,0,0},   {0,0,6 * 256}, 1},
    {{0,1,5,4}, {0,-T3_Q14_ONE,0}, {0,-(12 * 256),0}, {6 * 256,0,0},   {0,0,-(6 * 256)},6},
    {{0,1,3,2}, {0,0,-T3_Q14_ONE}, {0,0,-(12 * 256)}, {6 * 256,0,0},   {0,6 * 256,0},  2},
    {{4,5,7,6}, {0,0,T3_Q14_ONE},  {0,0,12 * 256},  {-(6 * 256),0,0},  {0,6 * 256,0},  5},
    {{1,3,7,5}, {T3_Q14_ONE,0,0},  {12 * 256,0,0},  {0,0,6 * 256},   {0,6 * 256,0},  3},
    {{0,4,6,2}, {-T3_Q14_ONE,0,0}, {-(12 * 256),0,0}, {0,0,-(6 * 256)},  {0,6 * 256,0},  4},
};

static uint16_t rgb565(uint32_t rgb)
{
    uint16_t r = (uint16_t)((rgb >> 19) & 0x1Fu);
    uint16_t g = (uint16_t)((rgb >> 10) & 0x3Fu);
    uint16_t b = (uint16_t)((rgb >> 3) & 0x1Fu);
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static uint32_t shade_rgb(uint32_t base, int32_t intensity, int32_t spec)
{
    if (intensity < 0) intensity = 0;
    if (intensity > 255) intensity = 255;
    if (spec < 0) spec = 0;
    if (spec > 96) spec = 96;

    int32_t r = (int32_t)((base >> 16) & 0xFFu) * intensity / 220 + spec;
    int32_t g = (int32_t)((base >> 8) & 0xFFu) * intensity / 220 + spec;
    int32_t b = (int32_t)(base & 0xFFu) * intensity / 220 + spec;
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static void px(t3d_surface_t *s, int x, int y, uint16_t color)
{
    if (!s || !s->pixels || x < 0 || y < 0 || x >= s->width || y >= s->height) return;
    s->pixels[y * s->stride + x] = color;
}

void t3d_surface_clear(t3d_surface_t *s, uint32_t rgb)
{
    if (!s || !s->pixels) return;
    uint16_t color = rgb565(rgb);
    for (int y = 0; y < s->height; ++y) {
        uint16_t *row = s->pixels + y * s->stride;
        for (int x = 0; x < s->width; ++x) row[x] = color;
    }
}

static int32_t edge(int ax, int ay, int bx, int by, int px_, int py_)
{
    return (int32_t)(px_ - ax) * (by - ay) - (int32_t)(py_ - ay) * (bx - ax);
}

static void triangle(t3d_surface_t *s, screen_vertex_t a, screen_vertex_t b,
                     screen_vertex_t c, uint16_t color)
{
    int min_x = a.x < b.x ? a.x : b.x;
    if (c.x < min_x) min_x = c.x;
    int max_x = a.x > b.x ? a.x : b.x;
    if (c.x > max_x) max_x = c.x;
    int min_y = a.y < b.y ? a.y : b.y;
    if (c.y < min_y) min_y = c.y;
    int max_y = a.y > b.y ? a.y : b.y;
    if (c.y > max_y) max_y = c.y;

    if (min_x < 0) min_x = 0;
    if (min_y < 0) min_y = 0;
    if (max_x >= s->width) max_x = s->width - 1;
    if (max_y >= s->height) max_y = s->height - 1;

    int32_t area = edge(a.x, a.y, b.x, b.y, c.x, c.y);
    if (area == 0) return;
    bool positive = area > 0;

    for (int y = min_y; y <= max_y; ++y) {
        uint16_t *row = s->pixels + y * s->stride;
        for (int x = min_x; x <= max_x; ++x) {
            int32_t w0 = edge(b.x, b.y, c.x, c.y, x, y);
            int32_t w1 = edge(c.x, c.y, a.x, a.y, x, y);
            int32_t w2 = edge(a.x, a.y, b.x, b.y, x, y);
            if (positive ? (w0 >= 0 && w1 >= 0 && w2 >= 0)
                         : (w0 <= 0 && w1 <= 0 && w2 <= 0)) {
                row[x] = color;
            }
        }
    }
}

static void line(t3d_surface_t *s, int x0, int y0, int x1, int y1, uint16_t color)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int sx = x0 < x1 ? 1 : -1;
    int dy_abs = y1 > y0 ? y1 - y0 : y0 - y1;
    int dy = -dy_abs;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    for (;;) {
        px(s, x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = err * 2;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void circle(t3d_surface_t *s, int cx, int cy, int r, uint16_t color)
{
    int rr = r * r;
    for (int y = -r; y <= r; ++y) {
        for (int x = -r; x <= r; ++x) {
            if (x * x + y * y <= rr) px(s, cx + x, cy + y, color);
        }
    }
}

static void ellipse(t3d_surface_t *s, int cx, int cy, int rx, int ry, uint16_t color)
{
    if (rx < 1 || ry < 1) return;
    int64_t rr = (int64_t)rx * rx * ry * ry;
    for (int y = -ry; y <= ry; ++y) {
        for (int x = -rx; x <= rx; ++x) {
            int64_t q = (int64_t)x * x * ry * ry + (int64_t)y * y * rx * rx;
            if (q <= rr) px(s, cx + x, cy + y, color);
        }
    }
}

static screen_vertex_t project(const t3d_surface_t *s, t3_vec3_t p_q8)
{
    /* Elevated camera: 60 degrees down, 30 degree azimuth. Top + two sides remain visible. */
    int32_t horizontal=(int32_t)(((int64_t)14189*p_q8.x-(int64_t)8192*p_q8.z)/16384);
    int32_t forward=(int32_t)(((int64_t)8192*p_q8.x+(int64_t)14189*p_q8.z)/16384);
    int32_t screen_y=(int32_t)((-(int64_t)14189*forward-(int64_t)8192*p_q8.y)/16384);
    int32_t depth=200*256+(int32_t)(((int64_t)8192*forward-(int64_t)14189*p_q8.y)/16384);
    screen_vertex_t out={
        .x=(int16_t)(s->width/2+horizontal*11/(10*256)),
        .y=(int16_t)(s->height/2+8+screen_y*11/(10*256)),
        .z_q8=depth,
    };
    return out;
}

static t3_vec3_t local_vertex(int index)
{
    const int32_t h = RIGID_DIE_HALF_Q8;
    t3_vec3_t v = {
        (index & 1) ? h : -h,
        (index & 2) ? h : -h,
        (index & 4) ? h : -h,
    };
    return v;
}

static int32_t face_light(t3_vec3_t n, int32_t *spec_out)
{
    /* Surface-to-light: upper-left/front, ~45 degree elevation. */
    const t3_vec3_t light = {-10000, 12500, -2600};
    const t3_vec3_t halfv = {-4374, 10118, -12150};
    int32_t diffuse = t3_dot_q14(n, light);
    if (diffuse < 0) diffuse = 0;

    int32_t spec = t3_dot_q14(n, halfv);
    if (spec < 0) spec = 0;
    int32_t s2 = (int32_t)((int64_t)spec * spec >> 14);
    int32_t s4 = (int32_t)((int64_t)s2 * s2 >> 14);
    *spec_out = s4 * 88 / T3_Q14_ONE;

    return 95 + diffuse * 145 / T3_Q14_ONE;
}

static void render_shadow(t3d_surface_t *s, const rigid_die_t *d)
{
    int32_t h = d->pos_q8.y >> 8;
    t3_vec3_t ground = d->pos_q8;
    ground.y = 0;
    ground.x += (h * 5 / 10) * 256;
    ground.z += (h * 3 / 10) * 256;
    screen_vertex_t p = project(s, ground);
    int rx = 11 + h / 9;
    int ry = 4 + h / 28;
    if (rx > 20) rx = 20;
    if (ry > 7) ry = 7;
    ellipse(s, p.x, p.y, rx, ry, rgb565(C_SHADOW));
}

static const int8_t PIPS[6][6][2] = {
    {{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}},
    {{-1,-1},{1,1},{0,0},{0,0},{0,0},{0,0}},
    {{-1,-1},{0,0},{1,1},{0,0},{0,0},{0,0}},
    {{-1,-1},{1,-1},{-1,1},{1,1},{0,0},{0,0}},
    {{-1,-1},{1,-1},{0,0},{-1,1},{1,1},{0,0}},
    {{-1,-1},{-1,0},{-1,1},{1,-1},{1,0},{1,1}},
};
static const uint8_t PIP_COUNT[6] = {1,2,3,4,5,6};

static void render_face_pips(t3d_surface_t *s, const rigid_die_t *d,
                             const cube_face_t *face, int radius)
{
    uint8_t value = face->value;
    for (uint8_t i = 0; i < PIP_COUNT[value - 1]; ++i) {
        int px_ = PIPS[value - 1][i][0];
        int py_ = PIPS[value - 1][i][1];
        t3_vec3_t local = face->center_q8;
        local.x += face->u_q8.x * px_ + face->v_q8.x * py_;
        local.y += face->u_q8.y * px_ + face->v_q8.y * py_;
        local.z += face->u_q8.z * px_ + face->v_q8.z * py_;
        local.x += face->normal_q14.x >> 7;
        local.y += face->normal_q14.y >> 7;
        local.z += face->normal_q14.z >> 7;

        t3_vec3_t world = t3_quat_rotate_q8(d->orientation, local);
        world.x += d->pos_q8.x;
        world.y += d->pos_q8.y;
        world.z += d->pos_q8.z;
        screen_vertex_t p = project(s, world);
        circle(s, p.x, p.y, radius, rgb565(C_PIP));
    }
}

static void render_die(t3d_surface_t *s, const rigid_die_t *d)
{
    screen_vertex_t sv[8];
    t3_vec3_t world[8];
    for (int i = 0; i < 8; ++i) {
        world[i] = t3_quat_rotate_q8(d->orientation, local_vertex(i));
        world[i].x += d->pos_q8.x;
        world[i].y += d->pos_q8.y;
        world[i].z += d->pos_q8.z;
        sv[i] = project(s, world[i]);
    }

    /* Camera-facing direction in world space. */
    const t3_vec3_t view = {-4096, 14189, -7094};
    visible_face_t visible_faces[6];
    int count = 0;
    for (int f = 0; f < 6; ++f) {
        t3_vec3_t n = t3_quat_rotate_q14(d->orientation, FACES[f].normal_q14);
        if (t3_dot_q14(n, view) <= 0) continue;
        int32_t depth = 0;
        for (int k = 0; k < 4; ++k) depth += sv[FACES[f].v[k]].z_q8;
        visible_faces[count++] = (visible_face_t){
            .index = (uint8_t)f,
            .depth = depth / 4,
            .world_normal_q14 = n,
        };
    }

    /* Painter sort: largest camera Z is farther away. */
    for (int i = 0; i < count; ++i) {
        for (int j = i + 1; j < count; ++j) {
            if (visible_faces[j].depth > visible_faces[i].depth) {
                visible_face_t tmp = visible_faces[i];
                visible_faces[i] = visible_faces[j];
                visible_faces[j] = tmp;
            }
        }
    }

    for (int i = 0; i < count; ++i) {
        const cube_face_t *face = &FACES[visible_faces[i].index];
        screen_vertex_t a = sv[face->v[0]];
        screen_vertex_t b = sv[face->v[1]];
        screen_vertex_t c = sv[face->v[2]];
        screen_vertex_t d4 = sv[face->v[3]];

        int32_t spec;
        int32_t intensity = face_light(visible_faces[i].world_normal_q14, &spec);
        uint16_t fill = rgb565(shade_rgb(C_DIE, intensity, spec));
        triangle(s, a, b, c, fill);
        triangle(s, a, c, d4, fill);

        uint16_t edge_color = rgb565(shade_rgb(C_EDGE, intensity + 12, spec / 3));
        line(s, a.x, a.y, b.x, b.y, edge_color);
        line(s, b.x, b.y, c.x, c.y, edge_color);
        line(s, c.x, c.y, d4.x, d4.y, edge_color);
        line(s, d4.x, d4.y, a.x, a.y, edge_color);

        /* Side faces are foreshortened; smaller pips stay separate. */
        int radius = visible_faces[i].world_normal_q14.y > 10000 ? 2 : 1;
        render_face_pips(s, d, face, radius);
    }
}

static void render_floor(t3d_surface_t *s)
{
    t3d_surface_clear(s, 0x123D32);
    uint16_t trim=rgb565(0xB59B65);
    for(int i=0;i<2;i++) {
        line(s,3+i,3+i,s->width-4-i,3+i,trim);
        line(s,3+i,s->height-4-i,s->width-4-i,s->height-4-i,trim);
        line(s,3+i,3+i,3+i,s->height-4-i,trim);
        line(s,s->width-4-i,3+i,s->width-4-i,s->height-4-i,trim);
    }
}
void t3d_render_dice_count(t3d_surface_t *s,const rigid_die_t *dice,uint8_t count)
{
    if(!s||!s->pixels||!dice)return;
    if(count>6)count=6;
    render_floor(s);
    uint8_t order[6];
    for(uint8_t i=0;i<count;i++){ order[i]=i;render_shadow(s,&dice[i]); }
    for(uint8_t i=0;i<count;i++)for(uint8_t j=i+1;j<count;j++)
        if(project(s,dice[order[j]].pos_q8).z_q8>project(s,dice[order[i]].pos_q8).z_q8) {
            uint8_t tmp=order[i];order[i]=order[j];order[j]=tmp;
        }
    for(uint8_t i=0;i<count;i++)render_die(s,&dice[order[i]]);
}
void t3d_render_dice_scene(t3d_surface_t *s,const rigid_die_t dice[2]) {
    t3d_render_dice_count(s,dice,2);
}

static const uint8_t WHEEL[37]={0,32,15,19,4,21,2,25,17,34,6,27,13,36,11,30,8,23,10,5,24,16,33,1,20,14,31,9,22,18,29,7,28,12,35,3,26};
uint8_t t3d_roulette_number(uint8_t pocket){return WHEEL[pocket%37];}
static int32_t sin_q10(int32_t angle) {
    angle%=3600;if(angle<0)angle+=3600;
    int sign=angle>1800?-1:1; if(angle>1800)angle-=1800;
    int64_t p=(int64_t)angle*(1800-angle);
    return (int32_t)(sign*4*p*1024/(4050000-p));
}
static screen_vertex_t polar(t3d_surface_t *s,int angle,int radius) {
    screen_vertex_t p={.x=(int16_t)(s->width/2+sin_q10(angle+900)*radius/1024),
        .y=(int16_t)(s->height/2+sin_q10(angle)*radius*84/(1024*100)),.z_q8=0};return p;
}
static const uint16_t DIGITS[10]={0x7B6F,0x2492,0x73E7,0x73CF,0x5BC9,0x79CF,0x79EF,0x7249,0x7BEF,0x7BCF};
static void digit(t3d_surface_t *s,int x,int y,int n,uint16_t color) {
    uint16_t bits=DIGITS[n%10];for(int r=0;r<5;r++)for(int c=0;c<3;c++)
        if(bits&(1u<<(14-r*3-c)))px(s,x+c,y+r,color);
}
void t3d_render_roulette(t3d_surface_t *s,int32_t wheel_angle,int32_t ball_angle,int ball_radius) {
    if(!s||!s->pixels)return;
    t3d_surface_clear(s,0x153A30);
    int cx=s->width/2,cy=s->height/2;
    ellipse(s,cx+2,cy+4,96,80,rgb565(0x07160F));
    ellipse(s,cx,cy,95,79,rgb565(0xA67B43));
    ellipse(s,cx,cy,91,76,rgb565(0xF0D290));
    ellipse(s,cx,cy,88,74,rgb565(0x291B10));
    ellipse(s,cx,cy,82,69,rgb565(0x97653A));
    for(int i=0;i<37;i++) {
        int a=wheel_angle+i*3600/37,b=wheel_angle+(i+1)*3600/37;
        screen_vertex_t p0=polar(s,a,80),p1=polar(s,b,80),p2=polar(s,b,54),p3=polar(s,a,54);
        uint16_t fill=rgb565(i==0?0x108950:(i&1)?0xB52639:0x151A1D);
        triangle(s,p0,p1,p2,fill);triangle(s,p0,p2,p3,fill);
        line(s,p0.x,p0.y,p3.x,p3.y,rgb565(0xC7A35F));
        screen_vertex_t p=polar(s,(a+b)/2,71);
        int number=WHEEL[i];int left=p.x-(number>=10?3:1);
        if(number>=10)digit(s,left,p.y-2,number/10,rgb565(0xFFF5D9));
        digit(s,left+(number>=10?4:0),p.y-2,number%10,rgb565(0xFFF5D9));
    }
    ellipse(s,cx,cy,52,44,rgb565(0xC5A764));
    ellipse(s,cx,cy,49,41,rgb565(0x5C3920));
    ellipse(s,cx,cy,36,30,rgb565(0x8C5A31));
    for(int i=0;i<8;i++) {
        screen_vertex_t a=polar(s,wheel_angle+i*450,14),b=polar(s,wheel_angle+i*450,46);
        line(s,a.x,a.y,b.x,b.y,rgb565(0xD4B273));
    }
    ellipse(s,cx,cy,13,11,rgb565(0xDEBD74));
    ellipse(s,cx-2,cy-3,6,4,rgb565(0xFFF0BF));
    screen_vertex_t ball=polar(s,ball_angle,ball_radius);
    circle(s,ball.x+1,ball.y+2,4,rgb565(0x090C09));
    circle(s,ball.x,ball.y,3,rgb565(0xF2ECDD));
    circle(s,ball.x-1,ball.y-1,1,rgb565(0xFFFFFF));
}

bool t3d_cube_mesh_valid(void) {
    for(int f=0;f<6;f++) {
        const cube_face_t *face=&FACES[f];
        for(int k=0;k<4;k++) {
            t3_vec3_t a=local_vertex(face->v[k]),b=local_vertex(face->v[(k+1)%4]);
            int64_t plane=(int64_t)a.x*face->normal_q14.x+(int64_t)a.y*face->normal_q14.y+(int64_t)a.z*face->normal_q14.z;
            if(plane!=(int64_t)RIGID_DIE_HALF_Q8*16384)return false;
            int32_t dx=a.x-b.x,dy=a.y-b.y,dz=a.z-b.z;
            if((int64_t)dx*dx+(int64_t)dy*dy+(int64_t)dz*dz!=(int64_t)4*RIGID_DIE_HALF_Q8*RIGID_DIE_HALF_Q8)return false;
        }
    }
    return true;
}
