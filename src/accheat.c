/*
 * accheat - internal mod menu for AssaultCube 1.3.0.2 (macOS, x86_64)
 *
 * Loaded with DYLD_INSERT_LIBRARIES. Hooks SDL_GL_SwapWindow for the
 * per-frame stuff and SDL_PollEvent for the menu input.
 */
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl.h>
#include <mach-o/dyld.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* globals, unslid (image base 0x100000000) */
#define G_PLAYER1   0x1001cbd38
#define G_PLAYERS   0x1001cbd40
#define G_NPLAYERS  0x1001cbd4c
#define G_GAMEMODE  0x1001cbb84
#define G_PROJ      0x1001d5be8
#define G_MVP       0x1001d5d44
#define G_CURFONT   0x1001d6f00
#define G_GUNS      0x1001b1550
#define F_DRAWTEXT  0x1000c1140

/* playerent */
#define P_POS       0x08
#define P_YAW       0x38
#define P_PITCH     0x3c
#define P_EYEH      0x54
#define P_ABOVEEYE  0x5c
#define P_STATE     0x7a
#define P_HEALTH    0x100
#define P_ARMOUR    0x104
#define P_NAME      0x219
#define P_TEAM      0x320
#define P_WEAPON    0x3a0

/* weapon */
#define W_AMMO      0x20
#define W_MAG       0x28

#define NUMGUNS     9
#define GUN_GRENADE 7

typedef struct {
    char modelname[23], title[42];
    short sound, reload, reloadtime, attackdelay, damage, piercing, projspeed, part, spread, recoil,
        magsize, mdl_kick_rot, mdl_kick_back, recoilincrease, recoilbase, maxrecoil, recoilbackfade, pushfactor;
    char isauto;
} guninfo;
_Static_assert(sizeof(guninfo) == 0x68, "guninfo size");

#define DEG (3.14159265f / 180.f)

#define G(T, a)    (*(T *)((a) + slide))
#define F(T, p, o) (*(T *)((char *)(p) + (o)))

static uintptr_t slide;
static int ready;
static void (*draw_text)(const char *, int, int, int, int, int, int, int, int);
static guninfo guns_orig[NUMGUNS];

extern void SDL_GL_SwapWindow(void *w);
extern int SDL_PollEvent(void *ev);
extern const uint8_t *SDL_GetKeyboardState(int *n);
extern uint32_t SDL_GetMouseState(int *x, int *y);

/* settings */
static int aim_on = 1, aim_key = 0, aim_fov = 30, aim_smooth = 1, aim_bone = 0, aim_drawfov = 1;
static int esp_box = 1, esp_health = 1, esp_name = 1, esp_dist = 0, esp_lines = 0, esp_team = 0;
static int god = 0, inf_ammo = 0, rapid = 0, fullauto = 0, norecoil = 0, nospread = 0;

static const char *aimkeys[] = {"LShift", "Clic droit", "Toujours"};
static const char *bones[] = {"Tete", "Corps"};

enum { TOGGLE, SLIDER, CHOICE };
typedef struct { const char *name; int type, *v, min, max; const char **choices; } item;
typedef struct { const char *name; item items[8]; int n; } tab;

static tab tabs[] = {
    {"Aimbot", {{"Actif", TOGGLE, &aim_on}, {"Touche", CHOICE, &aim_key, 0, 2, aimkeys},
                {"FOV", SLIDER, &aim_fov, 1, 180}, {"Smooth", SLIDER, &aim_smooth, 1, 20},
                {"Cible", CHOICE, &aim_bone, 0, 1, bones}, {"Cercle FOV", TOGGLE, &aim_drawfov}}, 6},
    {"Visuels", {{"Boites", TOGGLE, &esp_box}, {"Barre de vie", TOGGLE, &esp_health},
                 {"Noms", TOGGLE, &esp_name}, {"Distance", TOGGLE, &esp_dist},
                 {"Lignes", TOGGLE, &esp_lines}, {"Coequipiers", TOGGLE, &esp_team}}, 6},
    {"Joueur", {{"God mode", TOGGLE, &god}, {"Munitions infinies", TOGGLE, &inf_ammo},
                {"Tir rapide", TOGGLE, &rapid}, {"Full auto", TOGGLE, &fullauto},
                {"Pas de recul", TOGGLE, &norecoil}, {"Pas de dispersion", TOGGLE, &nospread}}, 6},
};
#define NTABS 3

static int menu_open = 1, cur_tab = 0, cur_item = 0;

static void adjust(item *it, int dir)
{
    if (it->type == TOGGLE) {
        *it->v ^= 1;
        return;
    }
    int step = (it->type == SLIDER && it->max > 50) ? 5 : 1;
    int v = *it->v + dir * step;
    if (it->type == CHOICE) {
        int span = it->max - it->min + 1;
        v = it->min + ((v - it->min) % span + span) % span;
    }
    if (v < it->min) v = it->min;
    if (v > it->max) v = it->max;
    *it->v = v;
}

/* SDL_Event: type @0, key.repeat @13, key.keysym.scancode @16 */
enum { EV_KEYDOWN = 0x300, EV_TEXTINPUT = 0x303, EV_MOTION = 0x400, EV_BTNDOWN = 0x401, EV_WHEEL = 0x403 };
enum { SC_RETURN = 40, SC_ESC = 41, SC_TAB = 43, SC_F1 = 58, SC_INSERT = 73,
       SC_RIGHT = 79, SC_LEFT = 80, SC_DOWN = 81, SC_UP = 82, SC_LSHIFT = 225 };

static int menu_input(const uint8_t *ev)
{
    if (!ready)
        return 0;

    uint32_t type = *(uint32_t *)ev;
    int sc = type == EV_KEYDOWN ? *(int32_t *)(ev + 16) : -1;

    if (sc == SC_F1 || sc == SC_INSERT) {
        if (!ev[13])
            menu_open ^= 1;
        return 1;
    }
    if (!menu_open)
        return 0;

    /* let key/button releases through, otherwise held keys get stuck */
    if (type == EV_TEXTINPUT || type == EV_MOTION || type == EV_BTNDOWN || type == EV_WHEEL)
        return 1;
    if (type != EV_KEYDOWN)
        return 0;

    tab *t = &tabs[cur_tab];
    item *it = &t->items[cur_item];
    switch (sc) {
    case SC_UP:     cur_item = (cur_item + t->n - 1) % t->n; break;
    case SC_DOWN:   cur_item = (cur_item + 1) % t->n; break;
    case SC_TAB:    cur_tab = (cur_tab + 1) % NTABS; cur_item = 0; break;
    case SC_LEFT:   adjust(it, -1); break;
    case SC_RIGHT:  adjust(it, 1); break;
    case SC_RETURN: if (it->type == TOGGLE) adjust(it, 1); break;
    case SC_ESC:    menu_open = 0; break;
    }
    return 1;
}

/* drawing */
static float ui;
#define TEXT_H (22 * ui)

static void rect(float x0, float y0, float x1, float y1, int fill)
{
    glBegin(fill ? GL_QUADS : GL_LINE_LOOP);
    glVertex2f(x0, y0);
    glVertex2f(x1, y0);
    glVertex2f(x1, y1);
    glVertex2f(x0, y1);
    glEnd();
}

static void circle(float cx, float cy, float r)
{
    glBegin(GL_LINE_LOOP);
    for (int i = 0; i < 64; i++) {
        float a = i * 6.2832f / 64;
        glVertex2f(cx + r * cosf(a), cy + r * sinf(a));
    }
    glEnd();
}

static float font_scale(void)
{
    char *f = G(char *, G_CURFONT);
    return f ? TEXT_H / F(int, f, 0x24) : 0;
}

/* same advance as draw_text: glyph width + 1, '\f' codes are invisible */
static float text_w(const char *s)
{
    char *f = G(char *, G_CURFONT);
    if (!f)
        return 0;
    char *chars = F(char *, f, 0x10);
    int n = F(int, f, 0x1c), w = 0;
    for (; *s; s++) {
        if (*s == '\f') {
            if (s[1]) s++;
            continue;
        }
        if (*s == ' ') {
            w += F(int, f, 0x20);
            continue;
        }
        int i = *s - F(int, f, 0x38);
        if (i >= 0 && i < n)
            w += *(short *)(chars + i * 0x14) + 1;
    }
    return w * font_scale();
}

static void text(const char *s, float x, float y, int r, int g, int b)
{
    float sc = font_scale();
    if (!sc)
        return;
    glPushMatrix();
    glTranslatef(x, y, 0);
    glScalef(sc, sc, 1);
    glEnable(GL_TEXTURE_2D);
    draw_text(s, 0, 0, r, g, b, 255, -1, -1);
    glDisable(GL_TEXTURE_2D);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glPopMatrix();
}

static void draw_menu(void)
{
    float x = 40 * ui, y = 80 * ui, w = 380 * ui, row = 30 * ui, pad = 12 * ui;
    tab *t = &tabs[cur_tab];
    float h = row * (t->n + 2) + pad * 2 + 24 * ui;

    glColor4f(0.06f, 0.07f, 0.09f, 0.92f);
    rect(x, y, x + w, y + h, 1);
    glColor4f(0.95f, 0.45f, 0.1f, 1);
    rect(x, y, x + w, y + 3 * ui, 1);
    glLineWidth(1);
    glColor4f(1, 1, 1, 0.12f);
    rect(x, y, x + w, y + h, 0);
    text("AssaultCube Mod Menu", x + pad, y + 8 * ui, 255, 140, 40);

    float ty = y + row + 4 * ui, tw = (w - pad * 2) / NTABS;
    for (int i = 0; i < NTABS; i++) {
        float tx = x + pad + i * tw;
        if (i == cur_tab)
            glColor4f(0.95f, 0.45f, 0.1f, 0.9f);
        else
            glColor4f(1, 1, 1, 0.06f);
        rect(tx + 2 * ui, ty, tx + tw - 2 * ui, ty + row - 4 * ui, 1);
        text(tabs[i].name, tx + (tw - text_w(tabs[i].name)) / 2, ty + 3 * ui, 255, 255, 255);
    }

    float iy = ty + row + 6 * ui;
    for (int i = 0; i < t->n; i++, iy += row) {
        item *it = &t->items[i];
        int sel = i == cur_item, c = sel ? 255 : 200;
        float vx = x + w - pad * 2;
        char buf[32];

        if (sel) {
            glColor4f(1, 1, 1, 0.08f);
            rect(x + pad, iy - 2 * ui, x + w - pad, iy + row - 4 * ui, 1);
        }
        text(it->name, x + pad * 2, iy, c, c, c);

        if (it->type == TOGGLE) {
            const char *s = *it->v ? "ON" : "OFF";
            if (*it->v)
                text(s, vx - text_w(s), iy, 80, 230, 100);
            else
                text(s, vx - text_w(s), iy, 230, 80, 80);
        } else if (it->type == SLIDER) {
            float bw = 110 * ui, bx = vx - bw - 40 * ui, by = iy + 8 * ui;
            float frac = (float)(*it->v - it->min) / (it->max - it->min);
            glColor4f(1, 1, 1, 0.15f);
            rect(bx, by, bx + bw, by + 6 * ui, 1);
            glColor4f(0.95f, 0.45f, 0.1f, 1);
            rect(bx, by, bx + bw * frac, by + 6 * ui, 1);
            snprintf(buf, sizeof buf, "%d", *it->v);
            text(buf, vx - text_w(buf), iy, 255, 255, 255);
        } else {
            snprintf(buf, sizeof buf, "< %s >", it->choices[*it->v]);
            text(buf, vx - text_w(buf), iy, 255, 255, 255);
        }
    }
    text("\f4Haut/Bas  Gauche/Droite  Entree  Tab  F1", x + pad, y + h - row + 2 * ui, 160, 160, 160);
}

/* game */

/* same check as the game's playerinfo(): team mode and same team */
static int teammate(char *a, char *b)
{
    int gm = G(int, G_GAMEMODE);
    if (gm < 0 || gm >= 0x16 || !((0x3368b1u >> gm) & 1))
        return 0;
    return !((F(int, a, P_TEAM) ^ F(int, b, P_TEAM)) & 1);
}

static int w2s(const float *v, const float *m, float vw, float vh, float *sx, float *sy)
{
    float cx = m[0] * v[0] + m[4] * v[1] + m[8] * v[2] + m[12];
    float cy = m[1] * v[0] + m[5] * v[1] + m[9] * v[2] + m[13];
    float cw = m[3] * v[0] + m[7] * v[1] + m[11] * v[2] + m[15];
    if (cw < 0.1f)
        return 0;
    *sx = (cx / cw + 1) * 0.5f * vw;
    *sy = (1 - cy / cw) * 0.5f * vh;
    return 1;
}

/* AC: forward = (cos(yaw - 90), sin(yaw - 90)), z up */
static void angles_to(const float *from, const float *to, float *yaw, float *pitch)
{
    float dx = to[0] - from[0], dy = to[1] - from[1], dz = to[2] - from[2];
    *yaw = fmodf(atan2f(dy, dx) / DEG + 90 + 360, 360);
    *pitch = atan2f(dz, hypotf(dx, dy)) / DEG;
}

static float wrap180(float a)
{
    return fmodf(fmodf(a, 360) + 540, 360) - 180;
}

/* rebuilt from the original table every frame so turning things off restores them */
static void apply_guns(void)
{
    guninfo *g = &G(guninfo, G_GUNS);
    for (int i = 0; i < NUMGUNS; i++) {
        guninfo v = guns_orig[i];
        if (norecoil) {
            v.recoil = v.recoilincrease = v.recoilbase = v.maxrecoil = 0;
            v.mdl_kick_back = v.mdl_kick_rot = v.pushfactor = 0;
        }
        if (nospread)
            v.spread = 0;
        if (rapid && i != GUN_GRENADE)
            v.attackdelay = 0;
        if (fullauto && i != GUN_GRENADE)
            v.isauto = 1;
        g[i] = v;
    }
}

static void draw_esp(char *me, char *p, int mate, const float *mvp, float vw, float vh)
{
    float *o = &F(float, p, P_POS), *mpos = &F(float, me, P_POS);
    float feet[3] = {o[0], o[1], o[2] - F(float, p, P_EYEH)};
    float head[3] = {o[0], o[1], o[2] + F(float, p, P_ABOVEEYE)};
    float fx, fy, hx, hy;

    if (!w2s(feet, mvp, vw, vh, &fx, &fy) || !w2s(head, mvp, vw, vh, &hx, &hy))
        return;

    float h = fy - hy, half = h * 0.3f;
    glLineWidth(2 * ui);

    if (esp_box) {
        if (mate)
            glColor4f(0.2f, 0.6f, 1, 1);
        else
            glColor4f(1, 0.25f, 0.25f, 1);
        rect(fx - half, hy, fx + half, fy, 0);
    }
    if (esp_health) {
        float frac = fminf(fmaxf(F(int, p, P_HEALTH) / 100.f, 0), 1), bx = fx - half - 6 * ui;
        glColor4f(0, 0, 0, 0.6f);
        rect(bx, hy, bx + 3 * ui, fy, 1);
        glColor4f(1 - frac, frac, 0, 1);
        rect(bx, fy - h * frac, bx + 3 * ui, fy, 1);
    }
    if (esp_lines && !mate) {
        glLineWidth(1);
        glColor4f(1, 1, 1, 0.35f);
        glBegin(GL_LINES);
        glVertex2f(vw / 2, vh);
        glVertex2f(fx, fy);
        glEnd();
    }
    if (esp_name) {
        char name[17];
        snprintf(name, sizeof name, "%.16s", &F(char, p, P_NAME));
        text(name, fx - text_w(name) / 2, hy - TEXT_H - 2 * ui, 255, 255, 255);
    }
    if (esp_dist) {
        char d[16];
        float dx = o[0] - mpos[0], dy = o[1] - mpos[1], dz = o[2] - mpos[2];
        snprintf(d, sizeof d, "%.0f", sqrtf(dx * dx + dy * dy + dz * dz));
        text(d, fx - text_w(d) / 2, fy + 2 * ui, 200, 200, 200);
    }
}

static void aim_at(char *me, char *target)
{
    float *o = &F(float, target, P_POS);
    float tgt[3] = {o[0], o[1], aim_bone ? o[2] - F(float, target, P_EYEH) * 0.5f : o[2]};
    float yaw, pitch;
    float *my = &F(float, me, P_YAW), *mp = &F(float, me, P_PITCH);

    angles_to(&F(float, me, P_POS), tgt, &yaw, &pitch);
    *my = fmodf(*my + wrap180(yaw - *my) / aim_smooth + 360, 360);
    *mp += (pitch - *mp) / aim_smooth;
}

static int aim_held(void)
{
    switch (aim_key) {
    case 0:  return SDL_GetKeyboardState(NULL)[SC_LSHIFT];
    case 1:  return (SDL_GetMouseState(NULL, NULL) & 4) != 0;
    default: return 1;
    }
}

static void frame(void)
{
    char *me = ready ? G(char *, G_PLAYER1) : NULL;
    if (!me)
        return;

    apply_guns();

    int alive = F(uint8_t, me, P_STATE) == 0;
    char *w = F(char *, me, P_WEAPON);
    if (alive && god) {
        F(int, me, P_HEALTH) = 1000;
        F(int, me, P_ARMOUR) = 100;
    }
    if (alive && inf_ammo && w) {
        **(int **)(w + W_AMMO) = 99;
        **(int **)(w + W_MAG) = 30;
    }

    GLint vp[4];
    glGetIntegerv(GL_VIEWPORT, vp);
    float vw = vp[2], vh = vp[3];
    const float *mvp = &G(float, G_MVP);
    ui = vh / 1080.f;

    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, vw, vh, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_ALPHA_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    char *best = NULL;
    float best_ang = aim_fov;
    char **pl = G(char **, G_PLAYERS);
    int n = G(int, G_NPLAYERS);

    for (int i = 0; pl && i < n; i++) {
        char *p = pl[i];
        if (!p || p == me || F(uint8_t, p, P_STATE) != 0 || F(int, p, P_TEAM) == 4)
            continue;

        int mate = teammate(me, p);
        if (!mate || esp_team)
            draw_esp(me, p, mate, mvp, vw, vh);

        if (mate || !alive || !aim_on)
            continue;
        float yaw, pitch;
        angles_to(&F(float, me, P_POS), &F(float, p, P_POS), &yaw, &pitch);
        float ang = hypotf(wrap180(yaw - F(float, me, P_YAW)), pitch - F(float, me, P_PITCH));
        if (ang < best_ang) {
            best_ang = ang;
            best = p;
        }
    }

    if (aim_on && best && !menu_open && aim_held())
        aim_at(me, best);

    if (aim_on && aim_drawfov && aim_fov < 90) {
        /* projmatrix[0] = cot(fov_x / 2) */
        float r = tanf(aim_fov * DEG) * G(float, G_PROJ) * vw / 2;
        glLineWidth(1);
        glColor4f(1, 1, 1, 0.25f);
        circle(vw / 2, vh / 2, r);
    }

    if (menu_open)
        draw_menu();
    else
        text("\f4[F1] menu", 10 * ui, 10 * ui, 255, 255, 255);

    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glPopAttrib();
}

/* hooks */
static void hk_swap(void *win)
{
    frame();
    SDL_GL_SwapWindow(win);
}

static int hk_poll(void *ev)
{
    if (!ev)
        return SDL_PollEvent(ev);
    while (SDL_PollEvent(ev))
        if (!menu_input(ev))
            return 1;
    return 0;
}

__attribute__((used, section("__DATA,__interpose")))
static struct { void *repl, *orig; } interpose[] = {
    {(void *)hk_swap, (void *)SDL_GL_SwapWindow},
    {(void *)hk_poll, (void *)SDL_PollEvent},
};

__attribute__((constructor))
static void init(void)
{
    slide = _dyld_get_image_vmaddr_slide(0);
    draw_text = (void *)(F_DRAWTEXT + slide);
    memcpy(guns_orig, &G(guninfo, G_GUNS), sizeof guns_orig);

    /* cheap version check before touching anything else */
    ready = strcmp(guns_orig[6].modelname, "assault") == 0;
    if (ready)
        fprintf(stderr, "[accheat] loaded (slide %#lx)\n", slide);
    else
        fprintf(stderr, "[accheat] unknown game build, disabled\n");
}
