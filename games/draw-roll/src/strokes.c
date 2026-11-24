/* strokes.c - Draw & Roll: ramps drawn by fingers.
 *
 * A finger's drag is resampled every STROKE_STEP px. Every piece is
 *   - a static physics segment (half thickness STROKE_R) that balls roll on immediately, and
 *   - a 3D tube: one shared unit mesh stretched by a model matrix (no per-frame mesh allocation).
 * Smooth joints use a mitred extension of the tube, sharp ones get a small sphere; the two free ends of a stroke get
 * a round cap. When more than LIMIT_MAX pieces exist the oldest ones thin out and fade (their physics thins with
 * them) and are only then removed. Wiping turns every piece into a tumbling chunk of debris plus sparkles.
 */
#include "strokes.h"
#include "fx.h"

#include <string.h>

#define POOL        330
#define LIMIT_MAX   240
#define MAX_PENS    18
#define PEN_AUX     17
#define MAX_DEBRIS  130
#define SPH_ANGLE   0.30f            /* joints that turn more than this (rad) get a sphere */
#define SH_DX       15.0f            /* shadow offset on the back wall */
#define SH_DY      (-19.0f)
#define FADE_SECS   1.0f

enum { PS_FREE = 0, PS_ALIVE, PS_FADING };

typedef struct {
    uint8_t  state;
    bool     sph_a, sph_b, has_next;     /* round cap / joint sphere at the ends (stroke order); a later piece continues from b */
    float    ax, ay, bx, by, len;        /* ends in stroke order */
    float    dirx, diry;                 /* unit a->b */
    float    ext_a, ext_b;               /* mitre extension at a / b */
    float    cx, cy, c, s, hl;           /* cached tube placement: centre, cos/sin of its angle, half length */
    float    z, age, pulse, fade, phase, ttl;   /* ttl > 0: fades by itself after that many seconds (hint ramps) */
    uint32_t uid;
    PG_Color color;
    uint32_t stroke;
    int      prev, next;
    uint32_t prev_uid, next_uid;
    PG_Body *body;
} Piece;

typedef struct {
    bool     active, rainbow, quiet;
    float    lx, ly, tx, ty, z, stagger, ttl;
    int      last_piece, pieces, color_idx;
    uint32_t stroke;
    PG_Color color;
    float    hue;
} Pen;

typedef struct {
    float x, y, z, vx, vy, vz, ang, spin, rx, spinx, life, max_life, hl;
    PG_Color color;
} Debris;

static PG_World *world;
static Piece     pieces[POOL];
static Pen       pens[MAX_PENS];
typedef struct { int idx; uint32_t uid; } QEntry;
static QEntry    q_ring[POOL];                       /* pieces in creation order (entries of pieces that left early are skipped) */
static int       q_head, q_tail, alive_count;        /* q_tail - q_head <= POOL; alive_count = pieces not fading */
static uint32_t  uid_ctr;
static Debris    debris[MAX_DEBRIS];
static uint32_t  stroke_serial;
static int       color_rot;
static PG_Mesh  *m_tube, *m_cap, *m_quad;
static PG_Tex    t_tube, t_shadow;

static const PG_Color PAL[10] = {
    { 255,  84, 104, 255 }, { 255, 150,  52, 255 }, { 255, 208,  56, 255 }, { 150, 222,  64, 255 }, {  56, 208, 128, 255 },
    {  52, 204, 214, 255 }, {  84, 160, 255, 255 }, { 140, 112, 255, 255 }, { 232,  92, 214, 255 }, { 255, 128, 170, 255 } };

PG_Color strokes_palette(int idx) { return PAL[((idx % 10) + 10) % 10]; }

/* -------------------------------------------------------------------- assets */
static PG_Mesh *build_tube(int n)
{
    MeshB b = mb_begin(2 * (n + 1), n * 6);
    for (int k = 0; k < 2; k++)
        for (int j = 0; j <= n; j++) {
            float a = PG_TAU * (float)j / (float)n, y = sinf(a), z = -cosf(a);     /* j=0 is the hidden back */
            mb_vert(&b, pg_v3(k ? 1.0f : -1.0f, y, z), pg_v3(0, y, z), (float)k, (float)j / (float)n, pg_rgb(255, 255, 255));
        }
    for (int j = 0; j < n; j++) mb_quad(&b, j, j + 1, (n + 1) + j + 1, (n + 1) + j);
    return mb_end(&b);
}

static PG_Image *paint_tube(void)
{
    /* barber-pole candy stripes on white: the finger colour is multiplied in, so stripes stay subtle */
    PG_Image *im = pg_img_new(32, 32, 0xFFFFFFFFu);
    for (int j = 0; j < 32; j++)
        for (int i = 0; i < 32; i++) {
            float ph = fmodf((float)i / 32.0f * 2.0f + (float)j / 32.0f, 1.0f);
            im->px[j * 32 + i] = ph < 0.5f ? PG_RGB24(0xFFFFFF) : PG_RGB24(0xE4E4F2);
        }
    pg_img_noise(im, 3, 21);
    pg_img_ps1(im);
    return im;
}

static PG_Image *paint_shadow_disc(void)
{
    /* a soft bluish disc; overlapping stamps every 16 px blend into one even, soft band (no seams, no ribs) */
    PG_Image *im = pg_img_new(32, 32, 0);
    for (int j = 0; j < 32; j++)
        for (int i = 0; i < 32; i++) {
            float d = sqrtf(((float)i - 15.5f) * ((float)i - 15.5f) + ((float)j - 15.5f) * ((float)j - 15.5f)) / 16.0f;
            float a = d >= 1.0f ? 0.0f : 1.0f - d;
            a = a * a * (3.0f - 2.0f * a);
            im->px[j * 32 + i] = PG_ARGB((uint32_t)(a * 255.0f), 34, 50, 124);
        }
    return im;
}

void strokes_init(PG_World *w)
{
    world = w;
    m_tube = build_tube(8);
    m_cap  = pg_mesh_sphere(1.0f, 7, 3);
    m_quad = pg_mesh_quad(1.0f, 1.0f);
    PG_Image *sheet = pg_img_new(64, 32, 0x00FFFFFFu), *part;                       /* candy stripes | soft shadow disc */
    part = paint_tube();        img_blit(sheet, part, 0, 0);  pg_img_free(part);
    part = paint_shadow_disc(); img_blit(sheet, part, 32, 0); pg_img_free(part);
    PG_Tex all = tex_finish(sheet, false);
    t_tube = tex_sub(all, 64, 32, 0, 0, 32, 32);
    t_shadow = tex_sub(all, 64, 32, 32, 0, 32, 32);
    memset(pieces, 0, sizeof pieces);
    memset(pens, 0, sizeof pens);
    memset(debris, 0, sizeof debris);
    q_head = q_tail = 0; alive_count = 0; uid_ctr = 0;
    stroke_serial = 0; color_rot = 0;
}

void strokes_shutdown(void)
{
    pg_mesh_free(m_tube); pg_mesh_free(m_cap); pg_mesh_free(m_quad);
    m_tube = m_cap = m_quad = NULL;
}

/* ------------------------------------------------------------------ helpers */
static inline PG_M4 place(float x, float y, float z, float c, float s, float sx, float sy, float sz)
{
    PG_M4 m;
    memset(&m, 0, sizeof m);
    m.m[0] = c * sx; m.m[1] = s * sx; m.m[4] = -s * sy; m.m[5] = c * sy; m.m[10] = sz;
    m.m[12] = x; m.m[13] = y; m.m[14] = z; m.m[15] = 1.0f;
    return m;
}

/* cache the tube placement; the tube's local +x is oriented to the right so texture and shading never flip */
static void piece_place(Piece *p)
{
    float ox = p->dirx, oy = p->diry, es = p->ext_a, ee = p->ext_b;
    if (ox < 0.0f) { ox = -ox; oy = -oy; float t = es; es = ee; ee = t; }
    float shift = (ee - es) * 0.5f;
    p->cx = (p->ax + p->bx) * 0.5f + ox * shift;
    p->cy = (p->ay + p->by) * 0.5f + oy * shift;
    p->hl = p->len * 0.5f + (es + ee) * 0.5f;
    p->c = ox; p->s = oy;
}

static void free_piece(Piece *p)
{
    if (p->body) { pg_world_remove(world, p->body); p->body = NULL; }
    if (p->state == PS_ALIVE) alive_count--;
    p->state = PS_FREE;
}

static Piece *alloc_piece(void)
{
    for (int i = 0; i < POOL; i++) if (pieces[i].state == PS_FREE) return &pieces[i];
    int best = -1; float bf = 2.0f;                     /* pool exhausted (extreme load): steal the most faded piece */
    for (int i = 0; i < POOL; i++) if (pieces[i].state == PS_FADING && pieces[i].fade < bf) { bf = pieces[i].fade; best = i; }
    if (best < 0) return NULL;
    free_piece(&pieces[best]);
    return &pieces[best];
}

static void start_fade(Piece *p)
{
    if (p->state != PS_ALIVE) return;
    p->state = PS_FADING;
    alive_count--;
    p->fade = 1.0f;
}

/* the creation-order queue; entries of pieces that already left (ttl, stolen slots) are dropped when it fills up */
static void q_push(int idx, uint32_t uid)
{
    if (q_tail - q_head >= POOL) {
        int w = q_head;
        for (int r = q_head; r < q_tail; r++) {
            QEntry e = q_ring[r % POOL];
            if (pieces[e.idx].uid == e.uid && pieces[e.idx].state == PS_ALIVE) q_ring[(w++) % POOL] = e;
        }
        q_tail = w;
        if (q_tail - q_head >= POOL) {                       /* every entry is a live piece: the oldest one has to go */
            QEntry e = q_ring[q_head % POOL];
            q_head++;
            if (pieces[e.idx].uid == e.uid) start_fade(&pieces[e.idx]);
        }
    }
    q_ring[q_tail % POOL] = (QEntry){ idx, uid };
    q_tail++;
}

static Piece *add_piece(Pen *pen, float ax, float ay, float bx, float by)
{
    float dx = bx - ax, dy = by - ay, len = sqrtf(dx * dx + dy * dy);
    if (len < 1.0f) return NULL;
    Piece *p = alloc_piece();
    if (!p) return NULL;
    PG_Body *body = pg_world_add_segment(world, ax, ay, bx, by, STROKE_R);
    if (!body) return NULL;
    memset(p, 0, sizeof *p);
    body->friction = 0.12f; body->restitution = 0.5f; body->tag = TAG_STROKE; body->user = p;
    p->body = body; p->state = PS_ALIVE; alive_count++;
    p->uid = ++uid_ctr; p->ttl = pen->ttl;
    p->ax = ax; p->ay = ay; p->bx = bx; p->by = by; p->len = len;
    p->dirx = dx / len; p->diry = dy / len;
    p->z = pen->z; p->fade = 1.0f;
    p->age = pen->stagger > 0.0f ? -pen->stagger * (float)pen->pieces : 0.3f;   /* pens: the live tail already grew in, no pop; the first ramp pops in one by one */
    p->phase = (float)pen->pieces * 0.55f;
    p->color = pen->rainbow ? pg_hsv(pen->hue, 0.62f, 1.0f) : pen->color;
    p->stroke = pen->stroke;
    p->sph_a = pen->last_piece < 0;
    p->sph_b = true;
    p->prev = p->next = -1;
    if (pen->last_piece >= 0) {
        Piece *q = &pieces[pen->last_piece];
        if (q->state == PS_ALIVE && q->stroke == pen->stroke) {
            p->prev = pen->last_piece; p->prev_uid = q->uid;
            q->has_next = true; q->next = (int)(p - pieces); q->next_uid = p->uid;
            float th = acosf(clampf(q->dirx * p->dirx + q->diry * p->diry, -1.0f, 1.0f));
            if (th < SPH_ANGLE) {                           /* smooth bend: mitre the two tubes, no sphere needed */
                float e = fmaxf(STROKE_R * tanf(th * 0.5f), 1.8f);        /* always overlap a little: no cracks between tubes */
                q->ext_b = e; p->ext_a = e; q->sph_b = false;
                piece_place(q);
            }
        }
    }
    piece_place(p);
    q_push((int)(p - pieces), p->uid);
    return p;
}

/* -------------------------------------------------------------------- pens */
int strokes_next_color(void)
{
    for (int k = 0; k < 10; k++) {
        int idx = (color_rot + k) % 10;
        bool used = false;
        for (int i = 0; i < MAX_PENS; i++) if (pens[i].active && !pens[i].rainbow && pens[i].color_idx == idx) used = true;
        if (!used) { color_rot = idx + 1; return idx; }
    }
    return color_rot++ % 10;
}

void strokes_pen_set_ttl(int pen, float seconds) { if (pen >= 0 && pen < MAX_PENS) pens[pen].ttl = seconds; }
bool strokes_pen_active(int pen) { return pen >= 0 && pen < MAX_PENS && pens[pen].active; }

static float clamp_x(float x) { return clampf(x, -WALL_IN_X + STROKE_R + 3.0f, WALL_IN_X - STROKE_R - 3.0f); }
static float clamp_y(float y) { return clampf(y, -262.0f, 300.0f); }

static void pen_begin(int idx, float x, float y, int color_idx, bool quiet, float stagger)
{
    Pen *pen = &pens[idx];
    memset(pen, 0, sizeof *pen);
    x = clamp_x(x); y = clamp_y(y);
    pen->active = true; pen->quiet = quiet; pen->stagger = stagger;
    pen->lx = pen->tx = x; pen->ly = pen->ty = y;
    pen->last_piece = -1;
    pen->stroke = ++stroke_serial;
    pen->rainbow = color_idx < 0;
    pen->color_idx = color_idx < 0 ? 0 : color_idx;
    pen->color = strokes_palette(pen->color_idx);
    pen->hue = pg_rand_range(0.0f, 360.0f);
    pen->z = (float)((pen->stroke * 5u) % 7u) * 2.4f - 7.2f;         /* each stroke gets its own thin depth layer */
}

void strokes_pen_down(int idx, float x, float y, int color_idx)
{
    if (idx < 0 || idx >= MAX_PENS) return;
    if (pens[idx].active) strokes_pen_up(idx);
    pen_begin(idx, x, y, color_idx, false, 0.0f);
}

/* walk the pen towards (x,y), laying down pieces; at most max_pieces per call */
static void pen_walk(Pen *pen, int max_pieces)
{
    for (int n = 0; n < max_pieces; n++) {
        float dx = pen->tx - pen->lx, dy = pen->ty - pen->ly, d = sqrtf(dx * dx + dy * dy);
        if (d < STROKE_STEP) break;
        float nx = pen->lx + dx / d * STROKE_STEP, ny = pen->ly + dy / d * STROKE_STEP;
        Piece *p = add_piece(pen, pen->lx, pen->ly, nx, ny);
        pen->lx = nx; pen->ly = ny;
        pen->hue += 13.0f;
        if (p) {
            pen->last_piece = (int)(p - pieces);
            pen->pieces++;
            if (!pen->quiet) {
                if ((pen->pieces % 3) == 1) snd_blip(2 + (pen->pieces / 3) % 8, 0.085f);
                if ((pen->pieces % 3) == 0) fx_burst(nx, ny, 22.0f, 1, 70.0f, pen->rainbow ? pen->hue : 20.0f + (float)pen->color_idx * 36.0f, 25.0f, 15.0f);
            }
        }
    }
}

void strokes_pen_move(int idx, float x, float y)
{
    if (idx < 0 || idx >= MAX_PENS || !pens[idx].active) return;
    Pen *pen = &pens[idx];
    x = clamp_x(x); y = clamp_y(y);
    pen->tx = lerpf(pen->tx, x, 0.85f);                  /* light smoothing: kills sensor jitter, costs well under 1 tick of lag */
    pen->ty = lerpf(pen->ty, y, 0.85f);
    pen_walk(pen, 4);
}

void strokes_pen_up(int idx)
{
    if (idx < 0 || idx >= MAX_PENS || !pens[idx].active) return;
    Pen *pen = &pens[idx];
    float dx = pen->tx - pen->lx, dy = pen->ty - pen->ly;
    if (dx * dx + dy * dy >= 9.0f * 9.0f) {              /* the last short stretch up to where the finger left */
        Piece *p = add_piece(pen, pen->lx, pen->ly, pen->tx, pen->ty);
        if (p) pen->pieces++;
    }
    pen->active = false;
}

void strokes_add_line(const float *xy, int n, int color_idx)
{
    if (n < 2) return;
    Pen *pen = &pens[PEN_AUX];
    pen_begin(PEN_AUX, xy[0], xy[1], color_idx, true, 0.012f);
    for (int i = 1; i < n; i++) {
        /* walk through the polyline in sub-steps so the pen never jumps more than a few pieces at once */
        float sx = pen->tx, sy = pen->ty, ex = xy[i * 2], ey = xy[i * 2 + 1];
        float d = sqrtf((ex - sx) * (ex - sx) + (ey - sy) * (ey - sy));
        int k = (int)(d / STROKE_STEP) + 1;
        for (int j = 1; j <= k; j++) {
            pen->tx = sx + (ex - sx) * (float)j / (float)k;
            pen->ty = sy + (ey - sy) * (float)j / (float)k;
            pen_walk(pen, 4);
        }
    }
    strokes_pen_up(PEN_AUX);
}

/* ---------------------------------------------------------------- wipe/limit */
static void spawn_debris(const Piece *p)
{
    for (int i = 0; i < MAX_DEBRIS; i++) {
        Debris *d = &debris[i];
        if (d->life > 0.0f) continue;
        d->x = (p->ax + p->bx) * 0.5f; d->y = (p->ay + p->by) * 0.5f; d->z = p->z;
        d->vx = pg_rand_range(-150.0f, 150.0f) + p->dirx * 40.0f;
        d->vy = pg_rand_range(-30.0f, 260.0f);
        d->vz = pg_rand_range(-50.0f, 90.0f);
        d->ang = atan2f(p->s, p->c); d->spin = pg_rand_range(-7.0f, 7.0f);
        d->rx = pg_rand_range(0.0f, PG_TAU); d->spinx = pg_rand_range(-6.0f, 6.0f);
        d->max_life = d->life = pg_rand_range(0.9f, 1.5f);
        d->hl = p->len * 0.5f; d->color = p->color;
        return;
    }
}

void strokes_wipe(void)
{
    int n = 0;
    for (int i = 0; i < POOL; i++) if (pieces[i].state != PS_FREE) n++;
    int every = n > MAX_DEBRIS ? (n + MAX_DEBRIS - 1) / MAX_DEBRIS : 1;
    int k = 0;
    for (int i = 0; i < POOL; i++) {
        Piece *p = &pieces[i];
        if (p->state == PS_FREE) continue;
        if (p->age >= 0.0f && (k++ % every) == 0) {
            spawn_debris(p);
            if ((k % 5) == 1) fx_burst((p->ax + p->bx) * 0.5f, (p->ay + p->by) * 0.5f, 24.0f, 1, 150.0f, 200.0f + (float)(i % 9) * 18.0f, 60.0f, 18.0f);
            if ((k % 7) == 2) fx_bubble((p->ax + p->bx) * 0.5f, (p->ay + p->by) * 0.5f, 26.0f, pg_rand_range(18.0f, 34.0f), pg_rand_range(40.0f, 90.0f));
        }
        free_piece(p);
    }
    q_head = q_tail = 0; alive_count = 0;
    for (int i = 0; i < MAX_PENS; i++)                      /* fingers still down keep drawing from where they are */
        if (pens[i].active) pens[i].last_piece = -1;
}

bool strokes_contact(PG_Body *seg)
{
    Piece *p = seg ? (Piece *)seg->user : NULL;
    if (!p || p->state == PS_FREE) return false;
    bool fresh = p->pulse < 0.35f;
    p->pulse = 1.0f;
    if (p->prev >= 0 && pieces[p->prev].uid == p->prev_uid && pieces[p->prev].pulse < 0.55f) pieces[p->prev].pulse = 0.55f;   /* the bump spreads a little */
    if (p->next >= 0 && pieces[p->next].uid == p->next_uid && pieces[p->next].pulse < 0.55f) pieces[p->next].pulse = 0.55f;
    return fresh;
}

int strokes_count(void) { return alive_count; }

void strokes_update(float degrade)
{
    int limit = LIMIT_MAX - (int)(clampf(degrade, 0.0f, 1.0f) * 100.0f);
    while (alive_count > limit && q_head < q_tail) {          /* too many: the oldest piece starts to thin out */
        QEntry e = q_ring[q_head % POOL];
        q_head++;
        Piece *p = &pieces[e.idx];
        if (p->uid == e.uid) start_fade(p);                   /* stale entries (already faded, or the slot was reused) are skipped */
    }
    for (int i = 0; i < POOL; i++) {
        Piece *p = &pieces[i];
        if (p->state == PS_FREE) continue;
        p->age += PG_DT;
        p->pulse = decay(p->pulse, 7.0f, PG_DT);
        if (p->state == PS_ALIVE && p->ttl > 0.0f && (p->ttl -= PG_DT) <= 0.0f) start_fade(p);
        if (p->state == PS_FADING) {
            p->fade -= PG_DT / FADE_SECS;
            if (p->fade <= 0.0f) { free_piece(p); continue; }
            if (p->body) p->body->radius = fmaxf(0.5f, STROKE_R * p->fade);     /* physics thins with the picture */
        }
    }
    for (int i = 0; i < MAX_DEBRIS; i++) {
        Debris *d = &debris[i];
        if (d->life <= 0.0f) continue;
        d->life -= PG_DT;
        d->vy -= 900.0f * PG_DT;
        d->x += d->vx * PG_DT; d->y += d->vy * PG_DT; d->z += d->vz * PG_DT;
        d->ang += d->spin * PG_DT; d->rx += d->spinx * PG_DT;
    }
}

/* ------------------------------------------------------------------ drawing */
void strokes_render_shadows(void)
{
    const unsigned fl = PG3D_BG | PG3D_UNLIT | PG3D_NOFOG;
    for (int i = 0; i < POOL; i++) {
        const Piece *p = &pieces[i];
        if (p->state == PS_FREE || p->age < 0.0f) continue;
        float vis = ease_out_back(p->age / 0.22f) * p->fade;
        if (vis < 0.02f) continue;
        float r = (STROKE_R + 14.0f) * vis;
        uint8_t a = (uint8_t)(clampf(vis, 0.0f, 1.0f) * 88.0f);
        PG_M4 m = place(p->bx + SH_DX, p->by + SH_DY, BACK_Z + 1.0f, 1, 0, r, r, 1.0f);
        pg3d_mesh(m_quad, t_shadow, &m, pg_rgba(255, 255, 255, a), fl);
        if (p->sph_a) {
            m = place(p->ax + SH_DX, p->ay + SH_DY, BACK_Z + 1.0f, 1, 0, r, r, 1.0f);
            pg3d_mesh(m_quad, t_shadow, &m, pg_rgba(255, 255, 255, a), fl);
        }
    }
}

void strokes_render(void)
{
    float t = pg_time();
    for (int i = 0; i < POOL; i++) {
        const Piece *p = &pieces[i];
        if (p->state == PS_FREE || p->age < 0.0f) continue;
        float pop = ease_out_back(p->age / 0.22f);
        float vis = pop * p->fade;
        if (vis < 0.03f) continue;
        float rs = STROKE_R * vis * (1.0f + 0.26f * p->pulse);
        float shim = 0.97f + 0.09f * sinf(t * 2.3f - p->phase);
        PG_Color c = col_mul(p->color, shim);
        if (p->pulse > 0.02f) c = pg_color_lerp(c, pg_rgb(255, 255, 255), 0.38f * p->pulse);
        c.a = (uint8_t)(clampf(p->fade * 1.7f, 0.0f, 1.0f) * 255.0f);
        PG_V3 q = at_depth(p->cx, p->cy, p->z);              /* the layer offset must not move the ramp away from its physics */
        PG_M4 m = place(q.x, q.y, q.z, p->c, p->s, p->hl, rs, rs);
        pg3d_mesh(m_tube, t_tube, &m, c, 0);
        if (p->sph_a) {
            q = at_depth(p->ax, p->ay, p->z);
            m = place(q.x, q.y, q.z, 1.0f, 0.0f, rs, rs, rs);
            pg3d_mesh(m_cap, t_tube, &m, c, 0);
            if (p->prev < 0) pg3d_shine(q, rs, 0.5f);
        }
        if (p->sph_b) {
            q = at_depth(p->bx, p->by, p->z);
            m = place(q.x, q.y, q.z, 1.0f, 0.0f, rs, rs, rs);
            pg3d_mesh(m_cap, t_tube, &m, c, 0);
            if (!p->has_next) pg3d_shine(q, rs, 0.5f);
        }
    }
    for (int i = 0; i < MAX_DEBRIS; i++) {
        const Debris *d = &debris[i];
        if (d->life <= 0.0f) continue;
        float k = smooth01(d->life / 0.45f);
        float r = STROKE_R * (0.45f + 0.55f * smooth01(d->life / 0.7f));
        PG_M4 m = pg_m4_mul(place(d->x, d->y, d->z, cosf(d->ang), sinf(d->ang), 1, 1, 1), pg_m4_mul(pg_m4_rot_x(d->rx), pg_m4_scale(d->hl, r, r)));
        PG_Color c = d->color; c.a = (uint8_t)(255.0f * k);
        pg3d_mesh(m_tube, t_tube, &m, c, 0);
    }
    /* the live tail: from the last laid piece to the finger, so the line is always attached to the fingertip even though
     * the next physical piece only appears every STROKE_STEP px */
    for (int i = 0; i < MAX_PENS; i++) {
        const Pen *pen = &pens[i];
        if (!pen->active || pen->quiet) continue;
        float dx = pen->tx - pen->lx, dy = pen->ty - pen->ly, d = sqrtf(dx * dx + dy * dy);
        if (d < 2.5f) continue;
        float ox = dx / d, oy = dy / d;
        if (ox < 0.0f) { ox = -ox; oy = -oy; }
        PG_Color c = pen->rainbow ? pg_hsv(pen->hue, 0.62f, 1.0f) : pen->color;
        PG_V3 q = at_depth((pen->lx + pen->tx) * 0.5f, (pen->ly + pen->ty) * 0.5f, pen->z);
        PG_M4 m = place(q.x, q.y, q.z, ox, oy, d * 0.5f + 1.5f, STROKE_R, STROKE_R);
        pg3d_mesh(m_tube, t_tube, &m, c, 0);
        q = at_depth(pen->tx, pen->ty, pen->z);
        m = place(q.x, q.y, q.z, 1.0f, 0.0f, STROKE_R, STROKE_R, STROKE_R);
        pg3d_mesh(m_cap, t_tube, &m, c, 0);
        if (pen->last_piece < 0) {
            q = at_depth(pen->lx, pen->ly, pen->z);
            m = place(q.x, q.y, q.z, 1.0f, 0.0f, STROKE_R, STROKE_R, STROKE_R);
            pg3d_mesh(m_cap, t_tube, &m, c, 0);
        }
    }
    /* a soft light under every finger that is drawing: instant feedback even before the first piece exists */
    PG_Tex glow = pg3d_builtin(PG3D_TEX_GLOW);
    for (int i = 0; i < MAX_PENS; i++) {
        const Pen *pen = &pens[i];
        if (!pen->active || pen->quiet) continue;
        PG_Color c = pen->rainbow ? pg_hsv(t * 120.0f, 0.55f, 1.0f) : pen->color;
        PG_V3 pos = at_depth(pen->tx, pen->ty, 46.0f);
        pg3d_sprite(glow, pos, 120.0f, 120.0f, 0, pg_alpha(c, 0.7f), PG3D_ADDITIVE);
        if (!pen->rainbow) pg3d_sprite(glow, pos, 34.0f, 34.0f, 0, pg_rgba(255, 255, 255, 110), PG3D_ADDITIVE);
    }
}
