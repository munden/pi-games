/* play.c - Block Tower game logic: physics world, towers, fingers, sounds, rebuild, idle behaviour.
 *
 * Fingers: touching a block grabs it with a soft spring (many fingers, many blocks); touching empty space drops a
 * new block from the sky at that x and, once the finger starts dragging, shoves blocks like a paddle. The big
 * button on the toy chest tidies everything into the chest and rains a fresh set of towers down.
 */
#include "bt.h"

#include <math.h>
#include <string.h>

#define BLOCK_DENSITY   0.0005f
#define BALL_DENSITY    0.0006f
#define GRAB_FREQ       5.0f
#define GRAB_DAMP       0.8f
#define PICK_PAD        14.0f             /* hit-test padding around anything grabbable (px) */
#define PAD_R           24.0f             /* radius of the finger paddle */
#define THROW_MAX       1300.0f           /* fastest throw (px/s) */
#define HOLD_MAX        1500.0f           /* fastest a held block may move: thin blocks never tunnel */
#ifndef HINT_AFTER
#define HINT_AFTER      3.0f              /* seconds untouched before the hint hand shows */
#endif
#ifndef ATTRACT_AFTER
#define ATTRACT_AFTER   20.0f             /* seconds untouched before a ball rolls in (tests may shorten it) */
#endif

/* ================================================================== particles */
static Part *part_alloc(Game *g)
{
    Part *best = &g->parts[0];
    for (int i = 0; i < MAX_PARTS; i++) {
        if (g->parts[i].kind == PT_NONE) return &g->parts[i];
        if (g->parts[i].life < best->life) best = &g->parts[i];
    }
    return best;       /* full: steal the one closest to its end */
}

void fx_stars(Game *g, float x, float y, float z, int n, float speed, float size)
{
    for (int i = 0; i < n; i++) {
        Part *p = part_alloc(g);
        float a = pg_rand_range(0, PG_TAU), s = speed * pg_rand_range(0.35f, 1.0f);
        *p = (Part){ pg_v3(x, y, z), pg_v3(cosf(a) * s, sinf(a) * s + speed * 0.35f, pg_rand_range(-20, 20)),
                     pg_rand_range(0.7f, 1.15f), 1.0f, size * pg_rand_range(0.7f, 1.2f), 0, pg_rand_range(0, PG_TAU),
                     pg_rand_range(-5, 5), pg_rand_range(0, 360), PT_STAR };
        p->max = p->life;
    }
}

void fx_dust(Game *g, float x, float y, float z, int n, float size)
{
    for (int i = 0; i < n; i++) {
        Part *p = part_alloc(g);
        float dir = (i & 1) ? 1.0f : -1.0f, s = pg_rand_range(40, 110);
        *p = (Part){ pg_v3(x + dir * pg_rand_range(0, 16), y + pg_rand_range(0, 6), z), pg_v3(dir * s, pg_rand_range(10, 40), pg_rand_range(-10, 30)),
                     pg_rand_range(0.45f, 0.75f), 1.0f, size * pg_rand_range(0.7f, 1.0f), size * pg_rand_range(0.7f, 1.2f),
                     pg_rand_range(0, PG_TAU), pg_rand_range(-1, 1), 0, PT_DUST };
        p->max = p->life;
    }
}

static void parts_update(Game *g, float dt)
{
    for (int i = 0; i < MAX_PARTS; i++) {
        Part *p = &g->parts[i];
        if (p->kind == PT_NONE) continue;
        p->life -= dt;
        if (p->life <= 0) { p->kind = PT_NONE; continue; }
        p->p = pg_v3_add(p->p, pg_v3_scale(p->v, dt));
        p->rot += p->spin * dt;
        p->size += p->grow * dt;
        if (p->kind == PT_STAR) { p->v.y -= 520.0f * dt; p->v.x *= 1.0f - 1.5f * dt; }
        else { float k = 1.0f - 2.2f * dt; p->v.x *= k; p->v.y *= k; }
    }
}

/* ============================================================ blocks and world */
static Block *block_alloc(Game *g)
{
    for (int i = 0; i < MAX_BLOCKS; i++) if (!g->blocks[i].body) return &g->blocks[i];
    return NULL;
}

static Block *add_box(Game *g, int type, int skin, float x, float y, float ang)
{
    Block *b = block_alloc(g);
    if (!b) return NULL;
    const ShapeDef *s = bt_shape(type);
    PG_Body *p = pg_world_add_box(g->w, x, y, s->hx, s->hy, ang, BLOCK_DENSITY);
    if (!p) return NULL;
    p->friction = 0.6f; p->restitution = 0.1f; p->lin_damp = 0.08f; p->ang_damp = 0.35f;
    memset(b, 0, sizeof *b);
    p->tag = TAG_BLOCK; p->user = b;
    b->body = p; b->kind = KIND_BOX; b->type = (uint8_t)type; b->skin = (uint8_t)skin;
    b->shade = pg_rand_range(0.90f, 1.0f); b->pop = 1.0f; b->birth = ++g->seq;
    return b;
}

static Block *add_ball(Game *g, int which, float x, float y)
{
    Block *b = block_alloc(g);
    if (!b) return NULL;
    PG_Body *p = pg_world_add_circle(g->w, x, y, bt_ball_r(which), BALL_DENSITY);
    if (!p) return NULL;
    p->friction = 0.5f; p->restitution = 0.45f; p->lin_damp = 0.5f; p->ang_damp = 0.9f;
    memset(b, 0, sizeof *b);
    p->tag = TAG_BLOCK; p->user = b;
    b->body = p; b->kind = KIND_BALL; b->skin = (uint8_t)which;
    b->shade = 1.0f; b->pop = 1.0f; b->birth = ++g->seq;
    return b;
}

int play_box_count(const Game *g)
{
    int n = 0;
    for (int i = 0; i < MAX_BLOCKS; i++) n += g->blocks[i].body && g->blocks[i].kind == KIND_BOX;
    return n;
}

static PG_Body *static_box(Game *g, float x, float y, float hx, float hy)
{
    PG_Body *b = pg_world_add_box(g->w, x, y, hx, hy, 0, 0);
    if (b) { b->friction = 0.6f; b->restitution = 0.05f; b->tag = TAG_STATIC; }
    return b;
}

static int skin_count(int type)
{
    switch (bt_shape(type)->family) {
    case FAM_CUBE: return N_CUBE_SKINS;
    case FAM_LONG: return N_LONG_SKINS;
    case FAM_TALL: return N_TALL_SKINS;
    default:       return N_FLAT_SKINS;
    }
}

static int rand_skin(int type) { return pg_rand_int(0, skin_count(type) - 1); }

/* shuffled deck of (type, skin): a child sees every block before any repeats */
static void bag_fill(Game *g)
{
    int n = 0;
    for (int s = 0; s < N_CUBE_SKINS; s++) g->bag[n++] = T_CUBE << 8 | s;
    for (int s = 0; s < 4; s++) g->bag[n++] = T_SMALL << 8 | pg_rand_int(0, N_CUBE_SKINS - 1);
    for (int s = 0; s < N_LONG_SKINS; s++) g->bag[n++] = T_LONG << 8 | s;
    for (int s = 0; s < N_TALL_SKINS; s++) g->bag[n++] = T_TALL << 8 | s;
    g->bag[n++] = T_FLAT << 8; g->bag[n++] = T_FLAT << 8;
    for (int i = n - 1; i > 0; i--) { int j = pg_rand_int(0, i), t = g->bag[i]; g->bag[i] = g->bag[j]; g->bag[j] = t; }
    g->bag_n = n; g->bag_i = 0;
}

static int bag_next(Game *g)
{
    if (g->bag_i >= g->bag_n) bag_fill(g);
    return g->bag[g->bag_i++];
}

/* ---------------------------------------------------------------- ghosts */
static Ghost *ghost_alloc(Game *g)
{
    for (int i = 0; i < MAX_GHOSTS; i++) if (g->ghosts[i].mode == GH_NONE) return &g->ghosts[i];
    return NULL;
}

/* ------------------------------------------------------------- the layout */
enum { R_COLUMN, R_PYR3, R_PYR4, R_GATE, R_STAIRS, R_DOMINO, R_TOWER, R_COUNT };
static const uint8_t WIDTH_ORDER[5] = { T_LONG, T_FLAT, T_CUBE, T_TALL, T_SMALL };      /* wide to narrow */

static float plan_add(Plan *pl, int type, int skin, float x, float y_bottom, int level, int group)
{
    const ShapeDef *s = bt_shape(type);
    if (pl->n >= MAX_PLAN) return y_bottom + 2 * s->hy;
    Place *p = &pl->p[pl->n++];
    p->kind = KIND_BOX; p->type = (uint8_t)type; p->skin = (uint8_t)skin; p->level = (uint8_t)level; p->group = (uint8_t)group;
    p->x = x; p->y = y_bottom + s->hy + 0.4f;
    return p->y + s->hy;
}

static float recipe_column(Plan *pl, float cx, int group)
{
    float top = FLOOR_Y, target = pg_rand_range(250, 330), hw = 0;
    float r = pg_randf();
    int idx = r < 0.12f ? 0 : r < 0.5f ? 1 : 2;
    for (int lvl = 0; top - FLOOR_Y < target && lvl < 7; lvl++) {
        int type = WIDTH_ORDER[idx];
        top = plan_add(pl, type, rand_skin(type), cx + pg_rand_range(-3, 3), top, lvl, group);
        hw = fmaxf(hw, bt_shape(type)->hx);
        if (pg_randf() < 0.45f && idx < 4) idx++;
    }
    return hw;
}

static float recipe_pyramid(Plan *pl, float cx, int base, int group)
{
    float top = FLOOR_Y;
    for (int r = 0; r < base; r++) {
        int n = base - r;
        float row_top = top;
        for (int i = 0; i < n; i++)
            row_top = plan_add(pl, T_CUBE, rand_skin(T_CUBE), cx + ((float)i - (float)(n - 1) * 0.5f) * 80.7f, top, r, group);
        top = row_top;
    }
    return (float)base * 40.4f;
}

static float recipe_gate(Plan *pl, float cx, int group)
{
    float top = FLOOR_Y;
    for (int lvl = 0; lvl < 2; lvl++) {
        float t = top;
        for (int s = -1; s <= 1; s += 2) t = plan_add(pl, T_CUBE, rand_skin(T_CUBE), cx + (float)s * 62.0f, top, lvl, group);
        top = t;
    }
    float roof = plan_add(pl, T_LONG, rand_skin(T_LONG), cx, top, 2, group);
    int topper = pg_rand_int(0, 2);
    if (topper == 1) plan_add(pl, T_CUBE, rand_skin(T_CUBE), cx, roof, 3, group);
    else if (topper == 2) { plan_add(pl, T_SMALL, rand_skin(T_SMALL), cx - 42, roof, 3, group); plan_add(pl, T_SMALL, rand_skin(T_SMALL), cx + 42, roof, 3, group); }
    return 102.0f;
}

static float recipe_stairs(Plan *pl, float cx, int group)
{
    for (int col = 0; col < 3; col++) {
        float top = FLOOR_Y;
        for (int lvl = 0; lvl <= col; lvl++) top = plan_add(pl, T_CUBE, rand_skin(T_CUBE), cx + (float)(col - 1) * 80.7f, top, lvl, group);
    }
    return 121.0f;
}

static float recipe_dominoes(Plan *pl, float cx, int group)
{
    int s0 = pg_rand_int(0, N_TALL_SKINS - 1);
    for (int i = 0; i < 4; i++) plan_add(pl, T_TALL, (s0 + i) % N_TALL_SKINS, cx + ((float)i - 1.5f) * 92.0f, FLOOR_Y, 0, group);
    return 168.0f;
}

static float recipe_tower(Plan *pl, float cx, int group)
{
    float top = FLOOR_Y;
    int s0 = pg_rand_int(0, N_TALL_SKINS - 1);
    top = plan_add(pl, T_TALL, s0, cx, top, 0, group);
    top = plan_add(pl, T_TALL, (s0 + 1) % N_TALL_SKINS, cx + pg_rand_range(-3, 3), top, 1, group);
    plan_add(pl, T_SMALL, rand_skin(T_SMALL), cx, top, 2, group);
    return 30.0f;
}

static void plan_layout(Plan *pl)
{
    static const float width[R_COUNT] = { 160, 242, 323, 205, 242, 336, 62 };
    static const float x0 = -WALL_X + 140.0f, x1 = CHEST_X - CHEST_HX - 18.0f;     /* zone between the left ball and the chest */
    pl->n = 0;
    int pick[3] = { R_TOWER, R_COLUMN, R_STAIRS }, np = 0;
    for (int attempt = 0; attempt < 12 && np < 3; attempt++) {         /* a random set of three that fits, if there is one */
        int order[R_COUNT], cand[3], nc = 0;
        for (int i = 0; i < R_COUNT; i++) order[i] = i;
        for (int i = R_COUNT - 1; i > 0; i--) { int j = pg_rand_int(0, i), t = order[i]; order[i] = order[j]; order[j] = t; }
        float sum = 0;
        for (int i = 0; i < R_COUNT && nc < 3; i++) {
            float w = width[order[i]];
            if (sum + w + 24.0f * (float)(nc + 2) <= x1 - x0) { cand[nc++] = order[i]; sum += w; }
        }
        if (nc > np) { np = nc; for (int i = 0; i < nc; i++) pick[i] = cand[i]; }
    }
    if (np < 2) { pick[0] = R_TOWER; pick[1] = R_COLUMN; np = 2; }
    float sum = 0;
    for (int k = 0; k < np; k++) sum += width[pick[k]];
    float gap = (x1 - x0 - sum) / (float)(np + 1), cx = x0 + gap;
    for (int k = 0; k < np; k++) {
        float w = width[pick[k]], c = cx + w * 0.5f;
        switch (pick[k]) {
        case R_COLUMN:  recipe_column(pl, c, k); break;
        case R_PYR3:    recipe_pyramid(pl, c, 3, k); break;
        case R_PYR4:    recipe_pyramid(pl, c, 4, k); break;
        case R_GATE:    recipe_gate(pl, c, k); break;
        case R_STAIRS:  recipe_stairs(pl, c, k); break;
        case R_DOMINO:  recipe_dominoes(pl, c, k); break;
        default:        recipe_tower(pl, c, k); break;
        }
        cx += w + gap;
    }
    /* the two big soft balls: one on the floor at the far left, one perched on the toy chest */
    if (pl->n + 2 <= MAX_PLAN) {
        pl->p[pl->n++] = (Place){ KIND_BALL, 0, 0, 0, 5, -WALL_X + bt_ball_r(0) + 4.0f, FLOOR_Y + bt_ball_r(0) + 0.6f };
        pl->p[pl->n++] = (Place){ KIND_BALL, 0, 1, 0, 6, WALL_X - bt_ball_r(1) - 4.0f, CHEST_TOP + bt_ball_r(1) + 0.6f };
    }
}

static void build_instant(Game *g)
{
    plan_layout(&g->plan);
    for (int i = 0; i < g->plan.n; i++) {
        const Place *p = &g->plan.p[i];
        if (p->kind == KIND_BOX) add_box(g, p->type, p->skin, p->x, p->y, 0);
        else add_ball(g, p->skin, p->x, p->y);
    }
}

/* ============================================================== rain / tidy */
static void start_rain(Game *g)
{
    plan_layout(&g->plan);
    g->phase = PH_RAIN;
    for (int i = 0; i < g->plan.n; i++) {
        const Place *p = &g->plan.p[i];
        Ghost *gh = ghost_alloc(g);
        if (!gh) {       /* no free ghost: just place it */
            if (p->kind == KIND_BOX) add_box(g, p->type, p->skin, p->x, p->y, 0); else add_ball(g, p->skin, p->x, p->y);
            continue;
        }
        float hy = p->kind == KIND_BOX ? bt_shape(p->type)->hy : bt_ball_r(p->skin);
        float d = RAIN_TOP + hy - p->y, tf = bt_fall_time(d);
        float land = 1.5f + (float)p->group * 0.30f + (float)p->level * 0.18f + pg_rand_range(0, 0.04f);
        if (p->kind == KIND_BALL) land = 1.35f + (float)p->skin * 0.45f;
        *gh = (Ghost){ GH_RAIN, p->kind, p->type, p->skin, pg_rand_range(0.9f, 1.0f), -(land - tf), tf,
                       p->x, RAIN_TOP + hy, p->x, p->y, 0, 0, 0, 0 };
        if (gh->t > 0) gh->t = 0;
    }
    pg_sfx_whoosh(0.3f);
}

static void unhold_block(Game *g, Block *b)
{
    for (int i = 0; i < PG_MAX_TOUCH; i++) {
        Finger *f = &g->fingers[i];
        if (f->mode == F_GRAB && f->blk == b) { f->mode = F_DEAD; f->blk = NULL; }
    }
    b->holders = 0;
}

static void remove_block(Game *g, Block *b)
{
    unhold_block(g, b);
    pg_world_remove(g->w, b->body);
    b->body = NULL;
}

static void start_tidy(Game *g, int automatic)
{
    g->auto_rebuild = automatic;
    g->attract = 0;
    int idx[MAX_BLOCKS], n = 0;
    float key[MAX_BLOCKS];
    for (int i = 0; i < MAX_BLOCKS; i++) {
        if (!g->blocks[i].body) continue;
        idx[n] = i; key[n] = fabsf(g->blocks[i].body->x - CHEST_X) + 0.1f * fabsf(g->blocks[i].body->y); n++;
    }
    for (int i = 1; i < n; i++) {         /* farthest from the chest first */
        int ti = idx[i]; float tk = key[i]; int j = i - 1;
        while (j >= 0 && key[j] < tk) { idx[j + 1] = idx[j]; key[j + 1] = key[j]; j--; }
        idx[j + 1] = ti; key[j + 1] = tk;
    }
    g->phase = PH_TIDY;
    g->tidy_notes = 0; g->tidy_note_cd = 0;
    int made = 0;
    for (int k = 0; k < n; k++) {
        Block *b = &g->blocks[idx[k]];
        Ghost *gh = ghost_alloc(g);
        if (gh) {
            PG_Body *p = b->body;
            float ex = CHEST_X + pg_rand_range(-30, 30), ey = CHEST_TOP + 26;
            *gh = (Ghost){ GH_TIDY, b->kind, b->type, b->skin, b->shade, -(float)k / (float)(n > 1 ? n : 1) * 0.85f, pg_rand_range(0.62f, 0.74f),
                           p->x, p->y, ex, ey, (p->x + ex) * 0.5f, fmaxf(p->y, ey) + pg_rand_range(150, 230), p->angle, pg_rand_range(-4.5f, 4.5f) };
            made++;
        }
        remove_block(g, b);
    }
    pg_sfx_whoosh(0.35f);
    if (made == 0) start_rain(g);
}

/* oldest box that is not under another box and not held (a tower does not collapse because of a fade) */
static Block *oldest_free_box(Game *g)
{
    Block *best = NULL, *any = NULL;
    for (int i = 0; i < MAX_BLOCKS; i++) {
        Block *o = &g->blocks[i];
        if (!o->body || o->kind != KIND_BOX || o->holders) continue;
        if (!any || o->birth < any->birth) any = o;
        const ShapeDef *so = bt_shape(o->type);
        float co = fabsf(o->body->c), so_ = fabsf(o->body->s);
        float ox = so->hx * co + so->hy * so_, oy = so->hx * so_ + so->hy * co;
        int supports = 0;
        for (int j = 0; j < MAX_BLOCKS && !supports; j++) {
            Block *q = &g->blocks[j];
            if (!q->body || q == o) continue;
            float qx, qy;
            if (q->kind == KIND_BOX) { const ShapeDef *sq = bt_shape(q->type); qx = sq->hx * fabsf(q->body->c) + sq->hy * fabsf(q->body->s); qy = sq->hx * fabsf(q->body->s) + sq->hy * fabsf(q->body->c); }
            else qx = qy = q->body->radius;
            if (q->body->y > o->body->y && fabsf(q->body->x - o->body->x) < ox + qx - 4.0f &&
                (q->body->y - qy) - (o->body->y + oy) < 8.0f) supports = 1;
        }
        if (!supports && (!best || o->birth < best->birth)) best = o;
    }
    return best ? best : any;
}

static void fade_out(Game *g, Block *b)
{
    PG_Body *p = b->body;
    Ghost *gh = ghost_alloc(g);
    if (gh) *gh = (Ghost){ GH_FADE, b->kind, b->type, b->skin, b->shade, 0, 0.5f, p->x, p->y, p->x, p->y, 0, 0, p->angle, p->vx * 0.001f };
    fx_stars(g, p->x, p->y, 40, 6, 170, 24);
    remove_block(g, b);
}

static int box_cap(const Game *g)
{
    if (!g->headless && pg_frame_ms() > 12.0f) return 28;      /* degrade gracefully when the Pi is struggling */
    return BOX_CAP;
}

static void make_room(Game *g)
{
    int guard = 8;
    while (play_box_count(g) >= box_cap(g) && guard-- > 0) {
        Block *o = oldest_free_box(g);
        if (!o) break;
        fade_out(g, o);
    }
}

/* a new block drops from the sky at x; returns 1 if one was made */
static int drop_block(Game *g, float x)
{
    if (g->spawn_cd > 0) return 0;
    int e = bag_next(g), type = e >> 8, skin = e & 255;
    const ShapeDef *s = bt_shape(type);
    x = bt_clampf(x, -WALL_X + s->hx + 3.0f, WALL_X - s->hx - 3.0f);
    float y = CEIL_Y - s->hy - 6.0f, rb = hypotf(s->hx, s->hy);
    for (int i = 0; i < MAX_BLOCKS; i++) {          /* the top of the room must be free */
        const Block *o = &g->blocks[i];
        if (!o->body) continue;
        float ro = o->kind == KIND_BALL ? o->body->radius : hypotf(bt_shape(o->type)->hx, bt_shape(o->type)->hy);
        float dx = o->body->x - x, dy = o->body->y - y;
        if (dx * dx + dy * dy < (ro + rb) * (ro + rb)) return 0;
    }
    make_room(g);
    Block *b = add_box(g, type, skin, x, y, pg_rand_range(-0.1f, 0.1f));
    if (!b) return 0;
    b->pop = 0;
    b->body->vy = -60.0f;
    g->spawn_cd = 0.14f;
    fx_stars(g, x, y, 44, 6, 150, 22);
    return 1;
}

static void tap_feedback(Game *g, float wx, float wy)
{
    float k = bt_clampf((wx + WALL_X) / (2 * WALL_X), 0, 1);
    pg_sfx_chime(2 + (int)(k * 7.0f), 0.45f);
    fx_stars(g, wx, wy, 60, 7, 190, 20);
}

/* ================================================================= sounds */
static void squash_hit(Block *b, float speed, float x, float y)
{
    if (!b || b->kind != KIND_BOX) return;
    float lx, ly;
    pg_body_world_to_local(b->body, x, y, &lx, &ly);
    const ShapeDef *s = bt_shape(b->type);
    b->squash_axis = fabsf(ly) / s->hy > fabsf(lx) / s->hx ? 0 : 1;
    b->squash_v += bt_clampf(speed * 0.0072f, 0.4f, 3.6f);
}

static void on_contact(PG_Body *a, PG_Body *bo, float speed, float x, float y, void *ud)
{
    Game *g = ud;
    if (a->tag == TAG_PAD || bo->tag == TAG_PAD) {
        if (speed > 70 && g->snd_tokens >= 1.0f) { g->snd_tokens -= 1.0f; pg_sfx_tap(bt_clampf(speed / 1500.0f, 0.15f, 0.4f)); }
        return;
    }
    Block *ba = a->tag == TAG_BLOCK ? a->user : NULL, *bb = bo->tag == TAG_BLOCK ? bo->user : NULL;
    squash_hit(ba, speed, x, y); squash_hit(bb, speed, x, y);
    if (speed > 230.0f) fx_dust(g, x, y, 50, 2 + (int)(speed / 260.0f), 30.0f);
    if (speed > 440.0f) fx_stars(g, x, y, 56, 2 + (int)(speed / 700.0f), 150, 18);
    if (g->snd_tokens < 1.0f) return;
    if ((ba && ba->snd_cd > 0 && (!bb || bb->snd_cd > 0)) || (!ba && bb && bb->snd_cd > 0)) return;
    float v = bt_clampf((speed - 45.0f) / 900.0f, 0.0f, 1.0f), vol = 0.16f + 0.5f * v;
    g->snd_tokens -= 1.0f;
    if (ba) ba->snd_cd = 0.07f;
    if (bb) bb->snd_cd = 0.07f;
    int ball = (ba && ba->kind == KIND_BALL) || (bb && bb->kind == KIND_BALL);
    if (ball) pg_sfx_boing(vol * 0.55f, 0.72f);
    else if (speed < 170.0f) pg_sfx_tap(vol);
    else pg_sfx_thud(vol);
    const Block *bx = (ba && ba->kind == KIND_BOX) ? ba : (bb && bb->kind == KIND_BOX) ? bb : NULL;
    if (bx && speed > 120.0f) {            /* a pitched wooden clack: any mix of these notes sounds friendly */
        float f = pg_note_hz(5 + bt_shape(bx->type)->pitch);
        pg_tone(PG_WAVE_SINE, f, f * 0.97f, 110.0f, vol * 0.32f);
    }
}

/* ================================================================== fingers */
static int touch_world(const PG_Touch *t, float *x, float *y)
{
    PG_V3 p;
    if (!pg3d_plane_z(t->x, t->y, PICK_Z, &p)) return 0;
    *x = p.x; *y = p.y;
    return 1;
}

static void touch_world_vel(const PG_Touch *t, float *vx, float *vy)
{
    PG_V3 a, b;
    *vx = *vy = 0;
    if (!pg3d_plane_z(t->x, t->y, PICK_Z, &a) || !pg3d_plane_z(t->x + t->vx * 0.02f, t->y + t->vy * 0.02f, PICK_Z, &b)) return;
    *vx = (b.x - a.x) / 0.02f; *vy = (b.y - a.y) / 0.02f;
}

/* signed distance from a point to a block (negative inside) */
static float block_dist(const Block *b, float x, float y)
{
    const PG_Body *p = b->body;
    if (b->kind == KIND_BALL) return hypotf(x - p->x, y - p->y) - p->radius;
    float lx, ly;
    pg_body_world_to_local(p, x, y, &lx, &ly);
    float ex = fabsf(lx) - p->hx, ey = fabsf(ly) - p->hy;
    return hypotf(fmaxf(ex, 0), fmaxf(ey, 0)) + fminf(fmaxf(ex, ey), 0);
}

static Block *pick_block(Game *g, float x, float y)
{
    Block *best = NULL;
    float bd = 1e9f;
    for (int i = 0; i < MAX_BLOCKS; i++) {
        Block *b = &g->blocks[i];
        if (!b->body) continue;
        float d = block_dist(b, x, y);
        if (d <= PICK_PAD && d < bd) { bd = d; best = b; }
    }
    return best;
}

static int button_hit(const PG_Touch *t)
{
    float cx, cy, cx2, cy2, d;
    if (!pg3d_project(pg_v3(CHEST_X, BTN_Y, BTN_Z), &cx, &cy, &d)) return 0;
    if (!pg3d_project(pg_v3(CHEST_X + BTN_R, BTN_Y, BTN_Z), &cx2, &cy2, NULL)) return 0;
    float rad = fabsf(cx2 - cx) + 14.0f;
    return hypotf(t->x - cx, t->y - cy) <= rad;
}

static void press_button(Game *g)
{
    g->press = 1.0f;
    if (g->phase != PH_PLAY) { pg_sfx_tap(0.35f); return; }       /* already tidying: just a friendly click */
    g->hint_taps = 0;
    start_tidy(g, 0);
}

static void finger_down(Game *g, Finger *f, const PG_Touch *t)
{
    float wx, wy;
    memset(f, 0, sizeof *f);
    if (!touch_world(t, &wx, &wy)) { f->mode = F_DEAD; return; }
    f->wx = wx; f->wy = wy;
    if (button_hit(t)) { f->mode = F_BUTTON; press_button(g); return; }
    Block *b = pick_block(g, wx, wy);
    if (b) {
        float lx, ly;
        pg_body_world_to_local(b->body, wx, wy, &lx, &ly);
        if (b->kind == KIND_BOX) { lx = bt_clampf(lx, -b->body->hx, b->body->hx); ly = bt_clampf(ly, -b->body->hy, b->body->hy); }
        else { float l = hypotf(lx, ly); if (l > b->body->radius) { lx *= b->body->radius / l; ly *= b->body->radius / l; } }
        f->mode = F_GRAB; f->blk = b; f->lx = lx; f->ly = ly;
        b->holders++;
        b->squash_v += 1.6f;
        pg_sfx_tap(0.3f);
        fx_stars(g, wx, wy, 60, 3, 90, 16);
    } else {
        f->mode = F_PUSH;
        drop_block(g, wx);
        tap_feedback(g, wx, wy);
    }
}

static void finger_release(Game *g, Finger *f, const PG_Touch *t)
{
    if (f->mode == F_GRAB && f->blk && f->blk->body) {
        Block *b = f->blk;
        if (b->holders) b->holders--;
        if (b->holders == 0) {
            float vx = 0, vy = 0;
            if (t) touch_world_vel(t, &vx, &vy);
            float nvx = 0.5f * b->body->vx + 0.5f * vx, nvy = 0.5f * b->body->vy + 0.5f * vy;
            float sp = hypotf(nvx, nvy);
            if (sp > THROW_MAX) { nvx *= THROW_MAX / sp; nvy *= THROW_MAX / sp; sp = THROW_MAX; }
            b->body->vx = nvx; b->body->vy = nvy;
            if (sp > 700.0f) pg_sfx_whoosh(0.3f);
        }
    }
    if (f->pad) { pg_world_remove(g->w, f->pad); f->pad = NULL; }
    f->mode = F_NONE; f->blk = NULL;
}

static void finger_held(Game *g, Finger *f, const PG_Touch *t)
{
    float wx, wy;
    if (touch_world(t, &wx, &wy)) { f->wx = wx; f->wy = wy; }
    if (f->mode == F_GRAB && (!f->blk || !f->blk->body)) f->mode = F_DEAD;
    if (f->mode == F_PUSH && !f->pad && hypotf(t->x - t->sx, t->y - t->sy) > 12.0f) {
        f->pad = pg_world_add_circle(g->w, f->wx, f->wy, PAD_R, 0);
        if (f->pad) { f->pad->tag = TAG_PAD; f->pad->restitution = 0.1f; f->pad->friction = 0.3f; }
    }
}

static void fingers_update(Game *g)
{
    const PG_Touch *t = pg_touches();
    for (int i = 0; i < PG_MAX_TOUCH; i++) {
        Finger *f = &g->fingers[i];
        switch (t[i].state) {
        case PG_TOUCH_DOWN: finger_down(g, f, &t[i]); break;
        case PG_TOUCH_HELD: finger_held(g, f, &t[i]); break;
        case PG_TOUCH_UP:   finger_release(g, f, &t[i]); break;
        default:            if (f->mode != F_NONE) finger_release(g, f, NULL); break;
        }
    }
}

/* spring the held blocks to the fingers, steer the paddles */
static void fingers_apply(Game *g, float dt)
{
    for (int i = 0; i < PG_MAX_TOUCH; i++) {
        Finger *f = &g->fingers[i];
        float tx = bt_clampf(f->wx, -WALL_X + 24.0f, WALL_X - 24.0f), ty = bt_clampf(f->wy, FLOOR_Y + 12.0f, CEIL_Y - 12.0f);
        if (f->mode == F_GRAB && f->blk && f->blk->body) {
            PG_Body *p = f->blk->body;
            pg_body_grab_step(p, f->lx, f->ly, tx, ty, GRAB_FREQ, GRAB_DAMP, dt);
            float sp = pg_body_speed(p);
            if (sp > HOLD_MAX) { p->vx *= HOLD_MAX / sp; p->vy *= HOLD_MAX / sp; }
/*DBG*/ if (pg_ticks() % 6 == 0) fprintf(stderr, "grab tick=%llu target=(%.0f,%.0f) body=(%.0f,%.0f) ang=%.2f v=%.0f\n", (unsigned long long)pg_ticks(), tx, ty, p->x, p->y, p->angle, sp);
        }
        else if (f->mode == F_PUSH && f->pad) {
            float vx = (tx - f->pad->x) / dt, vy = (ty - f->pad->y) / dt, sp = hypotf(vx, vy);
            if (sp > 1100.0f) { vx *= 1100.0f / sp; vy *= 1100.0f / sp; }
            f->pad->vx = vx; f->pad->vy = vy;
        }
    }
}

/* ========================================================== idle behaviour */
/* the highest block whose top still leaves room above it for a new block to drop (else any block) */
static Block *hint_target(Game *g)
{
    Block *best = NULL, *any = NULL;
    for (int i = 0; i < MAX_BLOCKS; i++) {
        Block *b = &g->blocks[i];
        if (!b->body || b->kind != KIND_BOX) continue;
        float top = b->body->y + bt_shape(b->type)->hy;
        if (!any || b->body->y > any->body->y) any = b;
        if (top < CEIL_Y - 175.0f && (!best || b->body->y > best->body->y)) best = b;
    }
    return best ? best : any;
}

static void launch_ball(Game *g)
{
    /* the ball farthest from the boxes rolls in at the structure nearest to it */
    Block *ball = NULL;
    float cx = 0;
    int boxes = 0;
    for (int i = 0; i < MAX_BLOCKS; i++) {
        const Block *b = &g->blocks[i];
        if (b->body && b->kind == KIND_BOX) { cx += b->body->x; boxes++; }
    }
    if (boxes == 0) { start_tidy(g, 1); return; }
    cx /= (float)boxes;
    for (int i = 0; i < MAX_BLOCKS; i++) {
        Block *b = &g->blocks[i];
        if (!b->body || b->kind != KIND_BALL || b->holders) continue;
        if (!ball || fabsf(b->body->x - cx) > fabsf(ball->body->x - cx)) ball = b;
    }
    if (!ball) { start_tidy(g, 1); return; }
    PG_Body *bb = ball->body;
    Block *near = NULL, *tower = NULL;
    for (int i = 0; i < MAX_BLOCKS; i++) {
        Block *b = &g->blocks[i];
        if (b->body && b->kind == KIND_BOX && (!near || fabsf(b->body->x - bb->x) < fabsf(near->body->x - bb->x))) near = b;
    }
    for (int i = 0; i < MAX_BLOCKS; i++) {          /* the highest block of that structure */
        Block *b = &g->blocks[i];
        if (b->body && b->kind == KIND_BOX && fabsf(b->body->x - near->body->x) < 110.0f && (!tower || b->body->y > tower->body->y)) tower = b;
    }
    /* a lob that arrives at the height of that block */
    float dir = tower->body->x > bb->x ? 1.0f : -1.0f, vx = dir * 820.0f;
    float t = fmaxf(fabsf(tower->body->x - bb->x) - 90.0f, 120.0f) / 820.0f;
    float vy = (tower->body->y - bb->y - 0.5f * GRAVITY * t * t) / t;
    float vy_max = sqrtf(2.0f * -GRAVITY * fmaxf(CEIL_Y - 70.0f - bb->y, 10.0f));
    bb->vx = vx; bb->vy = bt_clampf(vy, -150.0f, vy_max);
    bb->w = -vx / bb->radius * 0.6f;
    g->attract = 1; g->attract_t = 0; g->calm_t = 0;
    pg_sfx_whoosh(0.3f);
}

static float world_calm(Game *g)
{
    float m = 0;
    for (int i = 0; i < MAX_BLOCKS; i++) {
        const Block *b = &g->blocks[i];
        if (!b->body) continue;
        float s = pg_body_speed(b->body) + fabsf(b->body->w) * 14.0f;
        if (s > m) m = s;
    }
    return m;
}

static void idle_update(Game *g, float dt)
{
    int touching = pg_touch_count() > 0;
    if (touching) { g->idle_t = 0; g->attract = 0; g->hint_taps = 0; }
    else g->idle_t += dt;

    if (g->phase == PH_PLAY && !touching) {
        if (g->attract == 0 && g->idle_t > ATTRACT_AFTER) launch_ball(g);
        else if (g->attract == 1) {
            g->attract_t += dt;
            g->calm_t = world_calm(g) < 30.0f ? g->calm_t + dt : 0;
            if ((g->calm_t > 1.3f && g->attract_t > 3.0f) || g->attract_t > 14.0f) start_tidy(g, 1);
        }
        /* a block gently "breathes" now and then so the toy room is never still */
        if (g->idle_t > 1.5f) {
            g->giggle_t -= dt;
            if (g->giggle_t <= 0) {
                g->giggle_t = pg_rand_range(0.9f, 1.8f);
                Block *b = &g->blocks[pg_rand_int(0, MAX_BLOCKS - 1)];
                if (b->body && b->kind == KIND_BOX && !b->holders) { b->squash_axis = 0; b->squash_v += 2.0f; }
            }
        }
    }

    /* the pointing hand: shows where to touch, and demonstrates a tap */
    int show = g->idle_t > HINT_AFTER && g->phase == PH_PLAY && g->attract == 0;
    g->hand_vis += ((show ? 1.0f : 0.0f) - g->hand_vis) * bt_clampf(dt * 4.0f, 0, 1);
    if (g->hand_vis < 0.02f) { g->hand_t = 0; g->hand_tapped = 0; g->hand_x = 99999.0f; return; }
    const Block *tb = hint_target(g);
    float tx = tb ? tb->body->x : 0.0f, ty = tb ? bt_clampf(tb->body->y + 125.0f, FLOOR_Y + 150.0f, CEIL_Y - 70.0f) : 40.0f;
    if (g->hand_x > 9999.0f) { g->hand_x = tx + 420.0f; g->hand_y = ty - 80.0f; g->hand_tx = tx; g->hand_ty = ty; }
    g->hand_tx += (tx - g->hand_tx) * bt_clampf(dt * 1.5f, 0, 1);
    g->hand_ty += (ty - g->hand_ty) * bt_clampf(dt * 1.5f, 0, 1);
    g->hand_x += (g->hand_tx - g->hand_x) * bt_clampf(dt * 3.0f, 0, 1);
    g->hand_y += (g->hand_ty - g->hand_y) * bt_clampf(dt * 3.0f, 0, 1);
    g->hand_t += dt;
    float cyc = fmodf(g->hand_t, 4.6f);
    g->hand_tap = (cyc > 1.7f && cyc < 2.15f) ? sinf((cyc - 1.7f) / 0.45f * PG_PI) : 0.0f;
    if (cyc < 1.0f) g->hand_tapped = 0;
    if (cyc >= 1.92f && !g->hand_tapped && g->hand_vis > 0.9f && g->hint_taps < 3) {
        g->hand_tapped = 1; g->hint_taps++;
        if (drop_block(g, g->hand_tx)) tap_feedback(g, g->hand_tx, g->hand_ty);
    }
}

/* ============================================================ ghost updates */
static void ghost_arrive(Game *g, const Ghost *gh)
{
    fx_stars(g, gh->x1 + pg_rand_range(-24, 24), gh->y1, 60, 2, 120, 20);
    g->lid_kick = 1.0f;
    if (g->tidy_note_cd <= 0) {
        pg_sfx_chime(2 + (g->tidy_notes++ % 8), 0.26f);
        g->tidy_note_cd = 0.11f;
    }
}

static void ghost_land(Game *g, const Ghost *gh)
{
    Block *b = gh->kind == KIND_BOX ? add_box(g, gh->type, gh->skin, gh->x1, gh->y1, 0) : add_ball(g, gh->skin, gh->x1, gh->y1);
    if (!b) return;
    float ta = RAIN_V / RAIN_A;
    b->body->vy = -(gh->dur < ta ? RAIN_A * gh->dur : RAIN_V);
    b->shade = gh->shade;
}

static void ghosts_update(Game *g, float dt)
{
    int tidy = 0, rain = 0;
    if (g->tidy_note_cd > 0) g->tidy_note_cd -= dt;
    for (int i = 0; i < MAX_GHOSTS; i++) {
        Ghost *gh = &g->ghosts[i];
        if (gh->mode == GH_NONE) continue;
        gh->t += dt;
        int done = gh->t >= gh->dur;
        if (gh->mode == GH_TIDY) {
            if (done && gh->t >= 0) { ghost_arrive(g, gh); gh->mode = GH_NONE; } else tidy++;
        } else if (gh->mode == GH_RAIN) {
            if (done && gh->t >= 0) { ghost_land(g, gh); gh->mode = GH_NONE; } else rain++;
        } else if (done) gh->mode = GH_NONE;
    }
    if (g->phase == PH_TIDY && tidy == 0) start_rain(g);
    else if (g->phase == PH_RAIN && rain == 0) {
        g->phase = PH_PLAY;
        if (g->auto_rebuild) g->idle_t = HINT_AFTER - 0.5f;
        g->auto_rebuild = 0;
    }
}

/* ================================================================ per tick */
static void blocks_update(Game *g, float dt)
{
    for (int i = 0; i < MAX_BLOCKS; i++) {
        Block *b = &g->blocks[i];
        if (!b->body) continue;
        float a = -900.0f * b->squash - 22.0f * b->squash_v;
        b->squash_v += a * dt;
        b->squash = bt_clampf(b->squash + b->squash_v * dt, -0.22f, 0.22f);
        if (b->pop < 1.0f) b->pop = fminf(1.0f, b->pop + dt / 0.22f);
        b->lift += ((b->holders ? 1.0f : 0.0f) - b->lift) * bt_clampf(dt * 14.0f, 0, 1);
        if (b->snd_cd > 0) b->snd_cd -= dt;
        /* a held block that is flung fast sheds stars */
        if (b->holders && pg_body_speed(b->body) > 500.0f && ((int)(g->seq + i) & 3) == (int)(pg_ticks() & 3))
            fx_stars(g, b->body->x, b->body->y, 50, 1, 60, 14);
    }
}


/*DBG*/ #include <stdio.h>
/*DBG*/ static void dbg_dump(Game *g, const char *tag)
/*DBG*/ {
/*DBG*/     fprintf(stderr, "DUMP %s tick=%llu\n", tag, (unsigned long long)pg_ticks());
/*DBG*/     for (int i = 0; i < MAX_BLOCKS; i++) { Block *b = &g->blocks[i]; if (!b->body) continue;
/*DBG*/         fprintf(stderr, "  #%02d k%d t%d  x=%7.1f y=%7.1f a=%6.3f v=%6.1f\n", i, b->kind, b->type, b->body->x, b->body->y, b->body->angle, pg_body_speed(b->body)); }
/*DBG*/ }

void play_update(Game *g)
{
/*DBG*/ { uint64_t tk = pg_ticks(); if (tk == 3 || tk == 400 || tk == 1000) dbg_dump(g, "t"); if (tk >= 50 && tk <= 60) { int nd = 0, ns = 0; for (int i = 0; i < MAX_PARTS; i++) { if (g->parts[i].kind == PT_DUST) { nd++; if (nd < 3) fprintf(stderr, "dust x=%.0f y=%.0f z=%.0f sz=%.0f life=%.2f/%.2f\n", g->parts[i].p.x, g->parts[i].p.y, g->parts[i].p.z, g->parts[i].size, g->parts[i].life, g->parts[i].max); } if (g->parts[i].kind == PT_STAR) ns++; } fprintf(stderr, "tick %llu dust=%d stars=%d\n", (unsigned long long)tk, nd, ns); } }
    const float dt = PG_DT;
    g->snd_tokens = fminf(3.0f, g->snd_tokens + dt * 22.0f);
    if (g->spawn_cd > 0) g->spawn_cd -= dt;
    if (g->press > 0) g->press = fmaxf(0, g->press - dt * 3.2f);
    if (g->lid_kick > 0) g->lid_kick = fmaxf(0, g->lid_kick - dt * 5.0f);

    fingers_update(g);
    idle_update(g, dt);
    ghosts_update(g, dt);
    fingers_apply(g, dt);
    pg_world_step(g->w, dt);
    blocks_update(g, dt);
    parts_update(g, dt);
}

void play_init(Game *g)
{
    const char *drv = SDL_GetCurrentVideoDriver();
    g->headless = drv && !strcmp(drv, "dummy");
    g->w = pg_world_new(0, GRAVITY);
    if (!g->w) pg_fatal("out of memory (physics world)");
    pg_world_set_contact_fn(g->w, on_contact, g);
    static_box(g, 0, FLOOR_Y - 100, 900, 100);                  /* floor */
    static_box(g, -WALL_X - 100, 0, 100, 800);                  /* left wall */
    static_box(g, WALL_X + 100, 0, 100, 800);                   /* right wall */
    static_box(g, 0, CEIL_Y + 100, 900, 100);                   /* ceiling */
    g->chest = static_box(g, CHEST_X, CHEST_Y, CHEST_HX, CHEST_HY);
    g->snd_tokens = 3.0f;
    g->hand_x = 99999.0f;
    bag_fill(g);
    build_instant(g);
}

void play_shutdown(Game *g)
{
    for (int i = 0; i < PG_MAX_TOUCH; i++) g->fingers[i].pad = NULL;
    pg_world_free(g->w);
    g->w = NULL;
}
