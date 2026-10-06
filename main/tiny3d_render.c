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
    {{0,3,2,1}, {0,0,-T3_Q14_ONE}, {0,0,-(12 * 256)}, {6 * 256,0,0},   {0,6 * 256,0},  2},
    {{4,5,6,7}, {0,0,T3_Q14_ONE},  {0,0,12 * 256},  {-(6 * 256),0,0},  {0,6 * 256,0},  5},
    {{1,2,6,5}, {T3_Q14_ONE,0,0},  {12 * 256,0,0},  {0,0,6 * 256},   {0,6 * 256,0},  3},
    {{0,4,7,3}, {-T3_Q14_ONE,0,0}, {-(12 * 256),0,0}, {0,0,-(6 * 256)},  {0,6 * 256,0},  4},
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
        int e2 = err << 1;
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
    /* Fixed camera: elevated ~27 degrees and looking toward +Z. */
    const int32_t cos_q14 = 14582;
    const int32_t sin_q14 = 7438;
    int32_t camera_y = (int32_t)(((int64_t)cos_q14 * p_q8.y +
                                  (int64_t)sin_q14 * p_q8.z) >> 14);
    int32_t camera_z = (170 << 8) +
                       (int32_t)(((int64_t)cos_q14 * p_q8.z -
                                  (int64_t)sin_q14 * p_q8.y) >> 14);
    if (camera_z < (32 << 8)) camera_z = 32 << 8;

    const int32_t focal = 154;
    screen_vertex_t out = {
        .x = (int16_t)(s->width / 2 + (int32_t)((int64_t)p_q8.x * focal / camera_z)),
        .y = (int16_t)(s->height - 17 - (int32_t)((int64_t)camera_y * focal / camera_z)),
        .z_q8 = camera_z,
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
    const t3_vec3_t light = {-8192, 11585, -8192};
    const t3_vec3_t halfv = {-4374, 10118, -12150};
    int32_t diffuse = t3_dot_q14(n, light);
    if (diffuse < 0) diffuse = 0;

    int32_t spec = t3_dot_q14(n, halfv);
    if (spec < 0) spec = 0;
    int32_t s2 = (int32_t)((int64_t)spec * spec >> 14);
    int32_t s4 = (int32_t)((int64_t)s2 * s2 >> 14);
    *spec_out = s4 * 88 / T3_Q14_ONE;

    return 54 + diffuse * 168 / T3_Q14_ONE;
}

static void render_shadow(t3d_surface_t *s, const rigid_die_t *d)
{
    int32_t h = d->pos_q8.y >> 8;
    t3_vec3_t ground = d->pos_q8;
    ground.y = 0;
    ground.x += (h * 5 / 10) << 8;
    ground.z += (h * 3 / 10) << 8;
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
    const t3_vec3_t view = {0, 7438, -14582};
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

        int radius = visible_faces[i].depth < (150 << 8) ? 2 : 1;
        render_face_pips(s, d, face, radius);
    }
}

static void render_floor(t3d_surface_t *s)
{
    uint16_t floor_color = rgb565(C_FLOOR);
    uint16_t grid_color = rgb565(C_GRID);
    int horizon = s->height - 37;
    for (int y = horizon; y < s->height; ++y) {
        uint16_t *row = s->pixels + y * s->stride;
        for (int x = 0; x < s->width; ++x) row[x] = floor_color;
    }
    line(s, 0, horizon, s->width - 1, horizon, grid_color);
    for (int y = horizon + 12; y < s->height; y += 12)
        line(s, 0, y, s->width - 1, y, grid_color);
    for (int x = 12; x < s->width; x += 24)
        line(s, s->width / 2, horizon, x, s->height - 1, grid_color);
}

void t3d_render_dice_scene(t3d_surface_t *s, const rigid_die_t dice[2])
{
    if (!s || !s->pixels || !dice) return;
    t3d_surface_clear(s, C_SCENE_BG);
    render_floor(s);
    render_shadow(s, &dice[0]);
    render_shadow(s, &dice[1]);

    /* Draw farther body first using center camera depth. */
    screen_vertex_t p0 = project(s, dice[0].pos_q8);
    screen_vertex_t p1 = project(s, dice[1].pos_q8);
    if (p0.z_q8 > p1.z_q8) {
        render_die(s, &dice[0]);
        render_die(s, &dice[1]);
    } else {
        render_die(s, &dice[1]);
        render_die(s, &dice[0]);
    }
}
