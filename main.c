/*
 * PS2 FPS - BETA
 * Raycaster estilo Wolfenstein para PlayStation 2 (PS2SDK + gsKit).
 *
 * Controles:
 *   Stick izq. / D-pad arriba-abajo ... avanzar / retroceder
 *   Stick der. X / D-pad izq-der ...... girar
 *   Cuadrado / Circulo ................ moverse de lado
 *   X o R2 ............................ disparar
 *   L1 / R1 ........................... cambiar de arma
 *   START ............................. reiniciar (al morir o ganar)
 */

#include <tamtypes.h>
#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <gsKit.h>
#include <dmaKit.h>
#include <libpad.h>
#include <malloc.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets.h"

/* ------------------------------------------------------------------ */
/* Mapa: 1-3 = paredes de colores, 4 = pilar, P = jugador, E = enemigo */
/* ------------------------------------------------------------------ */
#define MAPW 24
#define MAPH 24

static const char *level[MAPH] = {
    "111111112222222233333333",
    "1......12......2.......3",
    "1.P....12...E..2...E...3",
    "1......................3",
    "1......12..44..2.......3",
    "1......12......2.....E.3",
    "1......12....E.2.......3",
    "111.11112......233333333",
    "1.......2..44..2.......3",
    "1.......2......2.......3",
    "1..E......E........E...3",
    "1.......2......2.......3",
    "1.......2......2.....E.3",
    "1.......222..222.......3",
    "111.1111222..222333.3333",
    "11....................33",
    "11................4...33",
    "11..........E.........33",
    "11...E4...4...4...4...33",
    "11................E...33",
    "11....4..E............33",
    "11....................33",
    "111111112222222233333333",
    "111111112222222233333333",
};

#define PI_F       3.14159265f
#define FOV        0.66f
#define COLW       4
#define MAXCOLS    256
#define MAX_ENEMIES 32

/* ------------------------------------------------------------------ */
/* Tipos                                                               */
/* ------------------------------------------------------------------ */
enum { E_IDLE, E_CHASE, E_ATTACK, E_PAIN, E_DEAD };
enum { G_PLAY, G_DEAD, G_WIN };

typedef struct {
    float x, y;
    int hp, state, timer, frame, cool;
} Enemy;

typedef struct {
    int dmg, cd;
    float range, tol;
    int multi, splash, noise;
} Weapon;

typedef struct {
    float fwd, strafe, turn;
    int fire, prev, next, start;
} Input;

static const Weapon WEAP[WEAPON_COUNT] = {
    /* dmg cd  alcance tol   multi splash ruido */
    {  2, 22,  1.7f, 0.0f,  0,    0,     0 },  /* cuchillo        */
    {  2, 24, 14.0f, 0.0f,  0,    0,     1 },  /* pistola         */
    {  1,  8, 14.0f, 0.0f,  0,    0,     1 },  /* metralleta      */
    {  1,  4, 14.0f, 0.0f,  0,    0,     1 },  /* ametralladora   */
    {  6, 70, 16.0f, 0.0f,  0,    3,     1 },  /* lanzacohetes    */
    {  1,  4,  6.0f, 0.35f, 1,    0,     1 },  /* lanzallamas     */
};

/* ------------------------------------------------------------------ */
/* Estado global                                                       */
/* ------------------------------------------------------------------ */
static unsigned char wallMap[MAPH][MAPW];
static int dmap[MAPH][MAPW];

static Enemy enemies[MAX_ENEMIES];
static int nEnemies;

static float px, py, pa;
static int health, weapon, wcool, wtime, shots, hurtFlash, gameState, frameCount;
static float bob;

static GSGLOBAL *gs;
static GSTEXTURE wtex[WEAPON_COUNT * WEAPON_FRAMES];
static GSTEXTURE etex[ENEMY_FRAMES];
static int SW = 640, SH = 448, ncols = 160;
static float zbuf[MAXCOLS];

/* ------------------------------------------------------------------ */
/* Utilidades del mundo                                                */
/* ------------------------------------------------------------------ */
static int is_wall(float x, float y)
{
    int ix = (int)floorf(x), iy = (int)floorf(y);
    if (ix < 0 || iy < 0 || ix >= MAPW || iy >= MAPH) return 1;
    return wallMap[iy][ix] != 0;
}

static int blocked(float x, float y, float r)
{
    return is_wall(x - r, y - r) || is_wall(x + r, y - r) ||
           is_wall(x - r, y + r) || is_wall(x + r, y + r);
}

static void try_move(float *x, float *y, float dx, float dy, float r)
{
    if (!blocked(*x + dx, *y, r)) *x += dx;
    if (!blocked(*x, *y + dy, r)) *y += dy;
}

static int los(float x0, float y0, float x1, float y1)
{
    float dx = x1 - x0, dy = y1 - y0;
    float d = sqrtf(dx * dx + dy * dy);
    int n = (int)(d / 0.1f) + 1, i;
    for (i = 1; i < n; i++) {
        float t = (float)i / (float)n;
        if (is_wall(x0 + dx * t, y0 + dy * t)) return 0;
    }
    return 1;
}

static float norm_angle(float a)
{
    return atan2f(sinf(a), cosf(a));
}

/* Mapa de distancias (BFS) desde el jugador, para que los enemigos lo sigan */
static void build_dmap(void)
{
    static int qx[MAPW * MAPH], qy[MAPW * MAPH];
    int head = 0, tail = 0, x, y;
    for (y = 0; y < MAPH; y++)
        for (x = 0; x < MAPW; x++) dmap[y][x] = 9999;
    x = (int)px; y = (int)py;
    if (x < 0 || y < 0 || x >= MAPW || y >= MAPH) return;
    dmap[y][x] = 0; qx[tail] = x; qy[tail] = y; tail++;
    while (head < tail) {
        static const int dxs[4] = { 1, -1, 0, 0 }, dys[4] = { 0, 0, 1, -1 };
        int cx = qx[head], cy = qy[head], k;
        head++;
        for (k = 0; k < 4; k++) {
            int nx = cx + dxs[k], ny = cy + dys[k];
            if (nx < 0 || ny < 0 || nx >= MAPW || ny >= MAPH) continue;
            if (wallMap[ny][nx] || dmap[ny][nx] != 9999) continue;
            dmap[ny][nx] = dmap[cy][cx] + 1;
            qx[tail] = nx; qy[tail] = ny; tail++;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Juego                                                               */
/* ------------------------------------------------------------------ */
static void reset_game(void)
{
    int x, y;
    nEnemies = 0;
    memset(wallMap, 0, sizeof(wallMap));
    for (y = 0; y < MAPH; y++) {
        for (x = 0; x < MAPW; x++) {
            char c = level[y][x];
            if (c >= '1' && c <= '4') {
                wallMap[y][x] = (unsigned char)(c - '0');
            } else if (c == 'P') {
                px = x + 0.5f; py = y + 0.5f;
            } else if (c == 'E' && nEnemies < MAX_ENEMIES) {
                Enemy *e = &enemies[nEnemies++];
                memset(e, 0, sizeof(*e));
                e->x = x + 0.5f; e->y = y + 0.5f;
                e->hp = 6; e->state = E_IDLE; e->frame = EF_STAND;
            }
        }
    }
    pa = 0.0f;
    health = 100; weapon = 1; wcool = 0; wtime = 100; shots = 0;
    hurtFlash = 0; gameState = G_PLAY; bob = 0.0f;
    build_dmap();
}

static int enemies_alive(void)
{
    int i, n = 0;
    for (i = 0; i < nEnemies; i++)
        if (enemies[i].state != E_DEAD) n++;
    return n;
}

static void hurt_enemy(Enemy *e, int dmg)
{
    if (e->state == E_DEAD) return;
    e->hp -= dmg;
    e->timer = 0;
    e->cool = 20;
    if (e->hp <= 0) e->state = E_DEAD;
    else             e->state = E_PAIN;
}

static void player_shoot(void)
{
    const Weapon *w = &WEAP[weapon];
    int i, best = -1;
    float bestd = 1e9f;

    if (w->noise) {
        for (i = 0; i < nEnemies; i++) {
            Enemy *e = &enemies[i];
            float dx = e->x - px, dy = e->y - py;
            if (e->state == E_IDLE && dx * dx + dy * dy < 14.0f * 14.0f) {
                e->state = E_CHASE; e->timer = 0;
            }
        }
    }

    for (i = 0; i < nEnemies; i++) {
        Enemy *e = &enemies[i];
        float dx = e->x - px, dy = e->y - py, d, ang, tol;
        if (e->state == E_DEAD) continue;
        d = sqrtf(dx * dx + dy * dy);
        if (d > w->range || d < 0.01f) continue;
        ang = fabsf(norm_angle(atan2f(dy, dx) - pa));
        tol = (w->tol > 0.0f) ? w->tol : atanf(0.30f / d);
        if (ang > tol) continue;
        if (!los(px, py, e->x, e->y)) continue;
        if (w->multi) {
            hurt_enemy(e, w->dmg);
        } else if (d < bestd) {
            bestd = d; best = i;
        }
    }

    if (best >= 0) {
        Enemy *t = &enemies[best];
        hurt_enemy(t, w->dmg);
        if (w->splash) {
            for (i = 0; i < nEnemies; i++) {
                float dx, dy;
                if (i == best || enemies[i].state == E_DEAD) continue;
                dx = enemies[i].x - t->x; dy = enemies[i].y - t->y;
                if (dx * dx + dy * dy < 2.2f * 2.2f) hurt_enemy(&enemies[i], w->splash);
            }
        }
    }
}

static void enemy_step_toward(Enemy *e, float tx, float ty, float speed)
{
    float dx = tx - e->x, dy = ty - e->y;
    float d = sqrtf(dx * dx + dy * dy);
    if (d < 0.001f) return;
    try_move(&e->x, &e->y, dx / d * speed, dy / d * speed, 0.28f);
}

static void enemy_update(Enemy *e)
{
    float dx = px - e->x, dy = py - e->y;
    float d = sqrtf(dx * dx + dy * dy);

    switch (e->state) {
    case E_IDLE:
        e->frame = EF_STAND;
        if (d < 10.0f && los(e->x, e->y, px, py)) { e->state = E_CHASE; e->timer = 0; }
        break;

    case E_CHASE: {
        int see = los(e->x, e->y, px, py);
        e->timer++;
        e->frame = EF_WALK + (e->timer / 8) % 4;
        if (e->cool > 0) e->cool--;
        if (see && d < 8.0f && e->cool == 0) {
            e->state = E_ATTACK; e->timer = 0; e->frame = EF_ATTACK;
            break;
        }
        if (d > 1.1f) {
            if (see) {
                enemy_step_toward(e, px, py, 0.025f);
            } else {
                int cx = (int)e->x, cy = (int)e->y, bx = cx, by = cy, k;
                static const int dxs[4] = { 1, -1, 0, 0 }, dys[4] = { 0, 0, 1, -1 };
                int bestv = dmap[cy][cx];
                for (k = 0; k < 4; k++) {
                    int nx = cx + dxs[k], ny = cy + dys[k];
                    if (nx < 0 || ny < 0 || nx >= MAPW || ny >= MAPH) continue;
                    if (dmap[ny][nx] < bestv) { bestv = dmap[ny][nx]; bx = nx; by = ny; }
                }
                enemy_step_toward(e, bx + 0.5f, by + 0.5f, 0.025f);
            }
        }
        break;
    }

    case E_ATTACK:
        e->timer++;
        e->frame = EF_ATTACK + (e->timer / 8 > 2 ? 2 : e->timer / 8);
        if (e->timer == 16) {
            int chance = (int)(90.0f - d * 8.0f);
            if (chance < 20) chance = 20;
            if (los(e->x, e->y, px, py) && (rand() % 100) < chance) {
                health -= 6 + rand() % 7;
                hurtFlash = 14;
            }
        }
        if (e->timer >= 26) {
            e->state = E_CHASE; e->timer = 0;
            e->cool = 40 + rand() % 40;
        }
        break;

    case E_PAIN:
        e->timer++;
        e->frame = EF_PAIN + (e->timer > 7 ? 1 : 0);
        if (e->timer >= 14) { e->state = E_CHASE; e->timer = 0; }
        break;

    case E_DEAD:
        e->timer++;
        e->frame = EF_DEATH + (e->timer / 6 > 4 ? 4 : e->timer / 6);
        break;
    }
}

static void game_update(const Input *in)
{
    int i;
    frameCount++;

    if (gameState != G_PLAY) {
        if (in->start) reset_game();
        return;
    }

    /* giro y movimiento */
    pa += in->turn * 0.045f;
    if (pa > PI_F) pa -= 2.0f * PI_F;
    if (pa < -PI_F) pa += 2.0f * PI_F;
    {
        float f = in->fwd, s = in->strafe;
        float m = sqrtf(f * f + s * s);
        float sp = 0.045f, dx, dy;
        if (m > 1.0f) { f /= m; s /= m; }
        dx = (cosf(pa) * f - sinf(pa) * s) * sp;
        dy = (sinf(pa) * f + cosf(pa) * s) * sp;
        try_move(&px, &py, dx, dy, 0.25f);
        if (m > 0.1f) bob += 0.22f; else bob *= 0.9f;
    }

    /* armas */
    if (wcool > 0) wcool--;
    wtime++;
    if (in->prev && wcool == 0) { weapon = (weapon + WEAPON_COUNT - 1) % WEAPON_COUNT; wcool = 8; wtime = 100; }
    if (in->next && wcool == 0) { weapon = (weapon + 1) % WEAPON_COUNT; wcool = 8; wtime = 100; }
    if (in->fire && wcool == 0) {
        player_shoot();
        wcool = WEAP[weapon].cd;
        wtime = 0;
        shots++;
    }

    /* enemigos */
    if (frameCount % 10 == 0) build_dmap();
    for (i = 0; i < nEnemies; i++) enemy_update(&enemies[i]);

    if (hurtFlash > 0) hurtFlash--;
    if (health <= 0) { health = 0; gameState = G_DEAD; }
    else if (enemies_alive() == 0) gameState = G_WIN;
}

/* ------------------------------------------------------------------ */
/* Dibujo                                                              */
/* ------------------------------------------------------------------ */
static u64 rgba(int r, int g, int b, int a)
{
    return GS_SETREG_RGBAQ(r, g, b, a, 0x00);
}

static void rect(float x1, float y1, float x2, float y2, u64 c)
{
    gsKit_prim_sprite(gs, x1, y1, x2, y2, 1, c);
}

static float shade(float d)
{
    float f = 1.0f / (1.0f + d * 0.09f);
    if (f < 0.22f) f = 0.22f;
    return f;
}

static const int WCOL[5][3] = {
    { 0, 0, 0 }, { 60, 80, 175 }, { 140, 140, 145 }, { 155, 100, 55 }, { 175, 60, 50 }
};

static void draw_world(void)
{
    float dirx = cosf(pa), diry = sinf(pa);
    float plx = -diry * FOV, ply = dirx * FOV;
    int c, i;

    /* techo y piso */
    rect(0, 0, (float)SW, SH / 2.0f, rgba(48, 48, 56, 0x80));
    for (i = 0; i < 8; i++) {
        int v = 40 + i * 11;
        float y0 = SH / 2.0f + i * (SH / 2.0f) / 8.0f;
        float y1 = SH / 2.0f + (i + 1) * (SH / 2.0f) / 8.0f;
        rect(0, y0, (float)SW, y1 + 1, rgba(v, v, v, 0x80));
    }

    /* paredes */
    for (c = 0; c < ncols; c++) {
        float camx = 2.0f * (c + 0.5f) / (float)ncols - 1.0f;
        float rdx = dirx + plx * camx, rdy = diry + ply * camx;
        int mx = (int)floorf(px), my = (int)floorf(py);
        float ddx = (rdx == 0.0f) ? 1e30f : fabsf(1.0f / rdx);
        float ddy = (rdy == 0.0f) ? 1e30f : fabsf(1.0f / rdy);
        int stepx, stepy, side = 0, hit = 0, type = 1, guard = 0;
        float sdx, sdy, dist, lh, y0, y1, f;
        int r, g, b;

        if (rdx < 0) { stepx = -1; sdx = (px - mx) * ddx; }
        else         { stepx =  1; sdx = (mx + 1.0f - px) * ddx; }
        if (rdy < 0) { stepy = -1; sdy = (py - my) * ddy; }
        else         { stepy =  1; sdy = (my + 1.0f - py) * ddy; }

        while (!hit && guard++ < 64) {
            if (sdx < sdy) { sdx += ddx; mx += stepx; side = 0; }
            else           { sdy += ddy; my += stepy; side = 1; }
            if (mx < 0 || my < 0 || mx >= MAPW || my >= MAPH) { hit = 1; type = 1; }
            else if (wallMap[my][mx]) { hit = 1; type = wallMap[my][mx]; }
        }
        dist = (side == 0) ? (sdx - ddx) : (sdy - ddy);
        if (dist < 0.05f) dist = 0.05f;
        zbuf[c] = dist;

        lh = SH / dist;
        y0 = SH / 2.0f - lh / 2.0f; if (y0 < 0) y0 = 0;
        y1 = SH / 2.0f + lh / 2.0f; if (y1 > SH) y1 = (float)SH;

        f = shade(dist) * (side ? 0.7f : 1.0f);
        r = (int)(WCOL[type][0] * f);
        g = (int)(WCOL[type][1] * f);
        b = (int)(WCOL[type][2] * f);
        rect((float)(c * COLW), y0, (float)(c * COLW + COLW), y1, rgba(r, g, b, 0x80));
    }
}

static void draw_enemy(const Enemy *e)
{
    float dirx = cosf(pa), diry = sinf(pa);
    float plx = -diry * FOV, ply = dirx * FOV;
    float sx = e->x - px, sy = e->y - py;
    float inv = 1.0f / (plx * diry - dirx * ply);
    float tx = inv * (diry * sx - dirx * sy);
    float ty = inv * (-ply * sx + plx * sy);
    float scr, h, x0, x1, top, v;
    int c0, c1, c, shd;

    if (ty < 0.4f) return;
    scr = (SW / 2.0f) * (1.0f + tx / ty);
    h = SH / ty;
    x0 = scr - h / 2.0f; x1 = scr + h / 2.0f;
    top = SH / 2.0f - h / 2.0f;
    c0 = (int)floorf(x0 / COLW); c1 = (int)floorf((x1 - 1.0f) / COLW);
    if (c0 < 0) c0 = 0;
    if (c1 > ncols - 1) c1 = ncols - 1;

    v = shade(ty) * 1.25f; if (v > 1.0f) v = 1.0f;
    shd = (int)(0x80 * v);

    c = c0;
    while (c <= c1) {
        if (ty < zbuf[c]) {
            int s = c;
            float xs, xe, u1, u2;
            while (c <= c1 && ty < zbuf[c]) c++;
            xs = (float)(s * COLW); xe = (float)(c * COLW);
            if (xs < x0) xs = x0;
            if (xe > x1) xe = x1;
            if (xe <= xs) continue;
            u1 = (xs - x0) / h * ENEMY_SIZE;
            u2 = (xe - x0) / h * ENEMY_SIZE;
            gsKit_prim_sprite_texture(gs, &etex[e->frame],
                xs, top, u1, 0.0f,
                xe, top + h, u2, (float)ENEMY_SIZE,
                1, rgba(shd, shd, shd, 0x80));
        } else {
            c++;
        }
    }
}

static void draw_enemies(void)
{
    int order[MAX_ENEMIES], i, j;
    float dd[MAX_ENEMIES];
    for (i = 0; i < nEnemies; i++) {
        float dx = enemies[i].x - px, dy = enemies[i].y - py;
        order[i] = i; dd[i] = dx * dx + dy * dy;
    }
    /* lejos -> cerca */
    for (i = 1; i < nEnemies; i++) {
        int o = order[i]; float d = dd[o];
        for (j = i - 1; j >= 0 && dd[order[j]] < d; j--) order[j + 1] = order[j];
        order[j + 1] = o;
    }
    for (i = 0; i < nEnemies; i++) draw_enemy(&enemies[order[i]]);
}

static void draw_weapon(void)
{
    int frame = 0;
    float size = WEAPON_SIZE * 2.4f * (SH / 448.0f);
    float x, y, bx, by;
    const Weapon *w = &WEAP[weapon];

    if (w->cd <= 8) {
        if (wtime < w->cd) frame = (shots & 1) ? 1 : 2;
    } else {
        if (wtime < 5) frame = 1; else if (wtime < 11) frame = 2;
    }
    bx = sinf(bob) * 8.0f;
    by = fabsf(cosf(bob)) * 8.0f;
    x = SW / 2.0f - size / 2.0f + bx;
    y = SH - size + 10.0f + by;
    gsKit_prim_sprite_texture(gs, &wtex[weapon * WEAPON_FRAMES + frame],
        x, y, 0.0f, 0.0f,
        x + size, y + size, (float)WEAPON_SIZE, (float)WEAPON_SIZE,
        1, rgba(0x80, 0x80, 0x80, 0x80));
}

/* Fuente 5x5 minima, dibujada con rectangulos */
static const char *glyph(char ch)
{
    switch (ch) {
    case 'A': return " ### #   #######   ##   #";
    case 'D': return "#### #   ##   ##   ##### ";
    case 'E': return "######    #### #    #####";
    case 'I': return "#####  #    #    #  #####";
    case 'N': return "#   ###  ## # ##  ###   #";
    case 'O': return " ### #   ##   ##   # ### ";
    case 'P': return "#### #   ##### #    #    ";
    case 'R': return "#### #   ##### #  # #   #";
    case 'S': return " #####     ###     ##### ";
    case 'T': return "#####  #    #    #    #  ";
    case 'U': return "#   ##   ##   ##   # ### ";
    case 'W': return "#   ##   ## # ### ###   #";
    case 'Y': return "#   # # #   #    #    #  ";
    default:  return NULL;
    }
}

static void draw_text(const char *s, float x, float y, float sc, u64 col)
{
    for (; *s; s++, x += 6.0f * sc) {
        const char *g = glyph(*s);
        int i;
        if (!g) continue;
        for (i = 0; i < 25; i++) {
            if (g[i] == '#') {
                float gx = x + (i % 5) * sc, gy = y + (i / 5) * sc;
                rect(gx, gy, gx + sc, gy + sc, col);
            }
        }
    }
}

static float text_width(const char *s, float sc) { return (float)strlen(s) * 6.0f * sc - sc; }

static void draw_hud(void)
{
    int i, alive = enemies_alive();
    float m = 24.0f, hw = 150.0f;
    float hx = m, hy = SH - m - 14.0f;

    /* vida */
    rect(hx - 2, hy - 2, hx + hw + 2, hy + 14 + 2, rgba(240, 240, 240, 0x80));
    rect(hx, hy, hx + hw, hy + 14, rgba(30, 0, 0, 0x80));
    rect(hx, hy, hx + hw * (health / 100.0f), hy + 14, rgba(210, 20, 20, 0x80));

    /* enemigos que quedan */
    for (i = 0; i < nEnemies; i++) {
        float x = SW - m - (nEnemies - i) * 12.0f, y = SH - m - 10.0f;
        rect(x, y, x + 9, y + 9, (i < alive) ? rgba(220, 40, 40, 0x80) : rgba(70, 70, 70, 0x80));
    }

    /* arma elegida */
    for (i = 0; i < WEAPON_COUNT; i++) {
        float x = m + i * 16.0f, y = m;
        rect(x, y, x + 12, y + 12, (i == weapon) ? rgba(255, 220, 40, 0x80) : rgba(80, 80, 80, 0x80));
    }

    /* mira */
    rect(SW / 2.0f - 1, SH / 2.0f - 9, SW / 2.0f + 1, SH / 2.0f - 3, rgba(255, 255, 255, 0x60));
    rect(SW / 2.0f - 1, SH / 2.0f + 3, SW / 2.0f + 1, SH / 2.0f + 9, rgba(255, 255, 255, 0x60));
    rect(SW / 2.0f - 9, SH / 2.0f - 1, SW / 2.0f - 3, SH / 2.0f + 1, rgba(255, 255, 255, 0x60));
    rect(SW / 2.0f + 3, SH / 2.0f - 1, SW / 2.0f + 9, SH / 2.0f + 1, rgba(255, 255, 255, 0x60));
}

static void draw_overlay(void)
{
    if (hurtFlash > 0) {
        int a = hurtFlash * 5; if (a > 0x50) a = 0x50;
        rect(0, 0, (float)SW, (float)SH, rgba(220, 0, 0, a));
    }
    if (gameState == G_DEAD) {
        const char *t = "YOU DIED", *p = "PRESS START";
        rect(0, 0, (float)SW, (float)SH, rgba(150, 0, 0, 0x50));
        draw_text(t, (SW - text_width(t, 10)) / 2.0f, SH / 2.0f - 60, 10, rgba(255, 255, 255, 0x80));
        draw_text(p, (SW - text_width(p, 4)) / 2.0f, SH / 2.0f + 20, 4, rgba(255, 220, 40, 0x80));
    } else if (gameState == G_WIN) {
        const char *t = "YOU WIN", *p = "PRESS START";
        rect(0, 0, (float)SW, (float)SH, rgba(0, 110, 0, 0x50));
        draw_text(t, (SW - text_width(t, 10)) / 2.0f, SH / 2.0f - 60, 10, rgba(255, 255, 255, 0x80));
        draw_text(p, (SW - text_width(p, 4)) / 2.0f, SH / 2.0f + 20, 4, rgba(255, 220, 40, 0x80));
    }
}

static void game_render(void)
{
    gsKit_clear(gs, rgba(0, 0, 0, 0x80));
    draw_world();
    draw_enemies();
    draw_weapon();
    draw_hud();
    draw_overlay();
}

/* ------------------------------------------------------------------ */
/* Carga de sprites (base64 -> RLE -> paleta -> RGBA32 -> VRAM)        */
/* ------------------------------------------------------------------ */
static int b64val(int c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static int b64_decode(const char *s, unsigned char *out)
{
    int n = 0, acc = 0, bits = 0;
    for (; *s; s++) {
        int v = b64val((unsigned char)*s);
        if (v < 0) continue;
        acc = (acc << 6) | v; bits += 6;
        if (bits >= 8) { bits -= 8; out[n++] = (unsigned char)((acc >> bits) & 0xFF); }
    }
    return n;
}

static void load_sheet(const char *b64, int nframes, int size, GSTEXTURE *out)
{
    int blen = (int)(strlen(b64) / 4 * 3 + 8);
    int total = nframes * size * size;
    unsigned char *raw = (unsigned char *)malloc(blen);
    unsigned char *idx = (unsigned char *)malloc(total);
    unsigned char *pal;
    int n, pos = 0, f, p;

    n = b64_decode(b64, raw);
    pal = raw;
    for (p = 768; p + 1 < n && pos < total; p += 2) {
        int run = raw[p], val = raw[p + 1];
        while (run-- > 0 && pos < total) idx[pos++] = (unsigned char)val;
    }

    for (f = 0; f < nframes; f++) {
        u32 *mem = (u32 *)memalign(128, size * size * 4);
        for (p = 0; p < size * size; p++) {
            int i = idx[f * size * size + p];
            if (i == 0) mem[p] = 0;
            else mem[p] = (u32)pal[i * 3] | ((u32)pal[i * 3 + 1] << 8) |
                          ((u32)pal[i * 3 + 2] << 16) | (0x80u << 24);
        }
        FlushCache(0);

        memset(&out[f], 0, sizeof(GSTEXTURE));
        out[f].Width = size;
        out[f].Height = size;
        out[f].PSM = GS_PSM_CT32;
        out[f].Mem = mem;
        out[f].Filter = GS_FILTER_NEAREST;
        out[f].Vram = gsKit_vram_alloc(gs, gsKit_texture_size(size, size, GS_PSM_CT32), GSKIT_ALLOC_USERBUFFER);
        gsKit_texture_upload(gs, &out[f]);
    }
    free(idx);
    free(raw);
}

/* ------------------------------------------------------------------ */
/* Control (libpad)                                                    */
/* ------------------------------------------------------------------ */
static char padBuf[256] __attribute__((aligned(64)));
static int padOk = 0;

static void wait_pad_stable(void)
{
    int i, st;
    for (i = 0; i < 2000000; i++) {
        st = padGetState(0, 0);
        if (st == PAD_STATE_STABLE || st == PAD_STATE_FINDCTP1) return;
    }
}

static void pad_setup(void)
{
    SifInitRpc(0);
    SifLoadModule("rom0:SIO2MAN", 0, NULL);
    SifLoadModule("rom0:PADMAN", 0, NULL);
    padInit(0);
    if (padPortOpen(0, 0, padBuf) == 0) return;
    padOk = 1;
    wait_pad_stable();
    padSetMainMode(0, 0, PAD_MMODE_DUALSHOCK, PAD_MMODE_LOCK);
    wait_pad_stable();
}

static float stick(int v)
{
    float f = (v - 128) / 128.0f;
    if (f > -0.22f && f < 0.22f) return 0.0f;
    return f;
}

static void read_input(Input *in)
{
    struct padButtonStatus b;
    u32 p;
    static u32 last = 0;
    int st;

    memset(in, 0, sizeof(*in));
    if (!padOk) return;
    st = padGetState(0, 0);
    if (st != PAD_STATE_STABLE && st != PAD_STATE_FINDCTP1) return;
    if (padRead(0, 0, &b) == 0) return;
    p = 0xffff ^ b.btns;

    if ((b.mode >> 4) == 7) { /* DualShock: sticks analogicos activos */
        in->fwd    = -stick(b.ljoy_v);
        in->strafe =  stick(b.ljoy_h);
        in->turn   =  stick(b.rjoy_h);
    }
    if (p & PAD_UP)     in->fwd += 1.0f;
    if (p & PAD_DOWN)   in->fwd -= 1.0f;
    if (p & PAD_LEFT)   in->turn -= 1.0f;
    if (p & PAD_RIGHT)  in->turn += 1.0f;
    if (p & PAD_SQUARE) in->strafe -= 1.0f;
    if (p & PAD_CIRCLE) in->strafe += 1.0f;
    if (p & (PAD_CROSS | PAD_R2)) in->fire = 1;

    in->prev  = ((p & PAD_L1) && !(last & PAD_L1));
    in->next  = ((p & PAD_R1) && !(last & PAD_R1));
    in->start = ((p & PAD_START) && !(last & PAD_START));
    last = p;

    if (in->fwd > 1.0f) in->fwd = 1.0f;
    if (in->fwd < -1.0f) in->fwd = -1.0f;
    if (in->turn > 1.0f) in->turn = 1.0f;
    if (in->turn < -1.0f) in->turn = -1.0f;
    if (in->strafe > 1.0f) in->strafe = 1.0f;
    if (in->strafe < -1.0f) in->strafe = -1.0f;
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */
int main(int argc, char **argv)
{
    Input in;
    (void)argc; (void)argv;

    pad_setup();

    gs = gsKit_init_global();
    gs->PSM = GS_PSM_CT16;
    gs->PSMZ = GS_PSMZ_16S;
    gs->ZBuffering = GS_SETTING_OFF;
    gs->DoubleBuffering = GS_SETTING_ON;
    gs->PrimAlphaEnable = GS_SETTING_ON;

    dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC,
                D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);
    dmaKit_chan_init(DMA_CHANNEL_GIF);

    gsKit_init_screen(gs);
    gsKit_set_primalpha(gs, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
    gsKit_mode_switch(gs, GS_ONESHOT);

    SW = gs->Width; SH = gs->Height;
    ncols = SW / COLW;
    if (ncols > MAXCOLS) ncols = MAXCOLS;

    load_sheet(weapons_b64, WEAPON_COUNT * WEAPON_FRAMES, WEAPON_SIZE, wtex);
    load_sheet(enemy_b64, ENEMY_FRAMES, ENEMY_SIZE, etex);

    srand(12345);
    reset_game();

    for (;;) {
        read_input(&in);
        game_update(&in);
        game_render();
        gsKit_queue_exec(gs);
        gsKit_sync_flip(gs);
    }
    return 0;
}
