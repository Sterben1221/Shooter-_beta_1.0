/*
 * PS2 FPS - BETA 2
 * Raycaster estilo Wolfenstein para PlayStation 2 (PS2SDK + gsKit).
 *
 * Controles:
 *   Stick izq. / D-pad arriba-abajo ... avanzar / retroceder
 *   Stick der. X / D-pad izq-der ...... girar
 *   Cuadrado / Circulo ................ moverse de lado
 *   X o R2 ............................ disparar
 *   L1 / R1 ........................... cambiar de arma
 *   START ............................. reiniciar (al morir o ganar)
 * Las puertas se abren al acercarte (las de color piden su llave).
 * La salida es la puerta verde EXIT, detras de la puerta roja.
 */

#include <tamtypes.h>
#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <gsKit.h>
#include <dmaKit.h>
#include <libpad.h>
#ifdef USE_SOUND
#include <audsrv.h>
#include <sbv_patches.h>
#endif
#include <malloc.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets.h"

/* Tipos de item (los 8 primeros coinciden con el icono de items_b64) */
enum { IT_MEDKIT, IT_VEST, IT_AMMO_BUL, IT_AMMO_ROCK, IT_AMMO_FUEL,
       IT_KEY_B, IT_KEY_Y, IT_KEY_R,
       IT_W_MP, IT_W_CHAIN, IT_W_ROCKET, IT_W_FLAME, IT_DROP };

#include "level.h"   /* mapa, enemigos e items (generado por tools/make_level.py) */

#define PI_F        3.14159265f
#define FOV         0.66f
#define COLW        4
#define MAXCOLS     256
#define MAX_ENEMIES 48
#define MAX_ITEMS   160

/* ------------------------------------------------------------------ */
/* Tipos                                                               */
/* ------------------------------------------------------------------ */
enum { E_IDLE, E_CHASE, E_ATTACK, E_PAIN, E_DEAD, E_RETURN };
enum { G_PLAY, G_DEAD, G_WIN };

typedef struct {
    float x, y, face, homeX, homeY, homeFace;
    int hp, state, timer, frame, cool, lost;
} Enemy;

typedef struct { float x, y; int type, active, val; } Item;

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
static const int AMMO_TYPE[WEAPON_COUNT] = { -1, 0, 0, 0, 1, 2 };  /* balas, cohetes, combustible */
static const int AMMO_MAX[3] = { 200, 20, 100 };

/* ------------------------------------------------------------------ */
/* Estado global                                                       */
/* ------------------------------------------------------------------ */
static unsigned char wallMap[MAPH][MAPW];
static float doorOpen[MAPH][MAPW];
static unsigned char doorMoving[MAPH][MAPW];
static short dmap[MAPH][MAPW];
static short homeMap[MAX_ENEMIES][MAPH][MAPW];

static Enemy enemies[MAX_ENEMIES];
static int nEnemies;
static Item items[MAX_ITEMS];
static int nItems;

static float px, py, pa, bob, stepAcc;
static int health, armor, weapon, wcool, wtime, shots, hurtFlash, gameState, frameCount;
static int have[WEAPON_COUNT], ammo[3], haveKey[3], kills;
static char msgText[40];
static int msgTimer;

static GSGLOBAL *gs;
static GSTEXTURE wtex[WEAPON_COUNT * WEAPON_FRAMES];
static GSTEXTURE etex[ENEMY_FRAMES];
static GSTEXTURE walltex[WALL_TEXTURES];
static GSTEXTURE itemtex[ITEM_TEXTURES];
static int SW = 640, SH = 448, ncols = 160;
static float zbuf[MAXCOLS];

/* ------------------------------------------------------------------ */
/* Sonido (pasos). Se compila solo con -DUSE_SOUND                     */
/* ------------------------------------------------------------------ */
#define SND_RATE   22050
#define SND_FRAME  368          /* muestras por cuadro a 60 fps */
#define SND_STEPLEN 1800
#define NVOICE     8
static short stepWave[SND_STEPLEN];
static struct { int pos; float vol; int active; } voices[NVOICE];
static int audio_ok = 0;

#ifdef USE_SOUND
extern unsigned char audsrv_irx[];
extern unsigned int size_audsrv_irx;
#endif

static void audio_init(void)
{
#ifdef USE_SOUND
    int i, ret = 0;
    float y = 0.0f;
    audsrv_fmt_t fmt;

    sbv_patch_enable_lmb();
    SifLoadModule("rom0:LIBSD", 0, NULL);
    if (SifExecModuleBuffer(audsrv_irx, size_audsrv_irx, 0, NULL, &ret) < 0) return;
    if (audsrv_init() != 0) return;
    fmt.freq = SND_RATE; fmt.bits = 16; fmt.channels = 1;
    if (audsrv_set_format(&fmt) != 0) return;
    audsrv_set_volume(0x3FFF);

    /* paso: golpe grave + ruido filtrado que se apaga */
    for (i = 0; i < SND_STEPLEN; i++) {
        float env = expf(-(float)i / 330.0f);
        float n = ((rand() & 0xFFFF) / 32768.0f) - 1.0f;
        float thump = sinf(2.0f * PI_F * 85.0f * (float)i / SND_RATE) * expf(-(float)i / 520.0f);
        y = y * 0.82f + n * 0.18f;
        stepWave[i] = (short)((y * 0.9f + thump * 0.8f) * env * 15000.0f);
    }
    audio_ok = 1;
#endif
}

static void sfx_step(float vol)
{
    int i, slot = 0, oldest = -1;
    if (!audio_ok || vol <= 0.03f) return;
    if (vol > 1.0f) vol = 1.0f;
    for (i = 0; i < NVOICE; i++) {
        if (!voices[i].active) { slot = i; oldest = 1 << 30; break; }
        if (voices[i].pos > oldest) { oldest = voices[i].pos; slot = i; }
    }
    voices[slot].pos = 0; voices[slot].vol = vol; voices[slot].active = 1;
}

static void audio_update(void)
{
#ifdef USE_SOUND
    int acc[SND_FRAME], i, s, any = 0;
    short buf[SND_FRAME];
    if (!audio_ok) return;
    memset(acc, 0, sizeof(acc));
    for (i = 0; i < NVOICE; i++) {
        if (!voices[i].active) continue;
        any = 1;
        for (s = 0; s < SND_FRAME; s++) {
            int p = voices[i].pos + s;
            if (p >= SND_STEPLEN) break;
            acc[s] += (int)(stepWave[p] * voices[i].vol);
        }
        voices[i].pos += SND_FRAME;
        if (voices[i].pos >= SND_STEPLEN) voices[i].active = 0;
    }
    if (!any) return;
    for (s = 0; s < SND_FRAME; s++) {
        int v = acc[s];
        if (v > 32000) v = 32000;
        if (v < -32000) v = -32000;
        buf[s] = (short)v;
    }
    if (audsrv_available() >= (int)sizeof(buf)) audsrv_play_audio((char *)buf, sizeof(buf));
#endif
}

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

static void turn_toward(float *ang, float target, float rate)
{
    float d = norm_angle(target - *ang);
    if (fabsf(d) <= rate) *ang = target;
    else *ang = norm_angle(*ang + (d > 0 ? rate : -rate));
}

/* Distancias (BFS) desde una celda; se usa para seguir al jugador y volver a casa */
static void bfs(int sx, int sy, short m[MAPH][MAPW])
{
    static int qx[MAPW * MAPH], qy[MAPW * MAPH];
    static const int dxs[4] = { 1, -1, 0, 0 }, dys[4] = { 0, 0, 1, -1 };
    int head = 0, tail = 0, x, y;
    for (y = 0; y < MAPH; y++)
        for (x = 0; x < MAPW; x++) m[y][x] = 9999;
    if (sx < 0 || sy < 0 || sx >= MAPW || sy >= MAPH) return;
    m[sy][sx] = 0; qx[tail] = sx; qy[tail] = sy; tail++;
    while (head < tail) {
        int cx = qx[head], cy = qy[head], k;
        head++;
        for (k = 0; k < 4; k++) {
            int nx = cx + dxs[k], ny = cy + dys[k];
            if (nx < 0 || ny < 0 || nx >= MAPW || ny >= MAPH) continue;
            if (wallMap[ny][nx] || m[ny][nx] != 9999) continue;
            m[ny][nx] = (short)(m[cy][cx] + 1);
            qx[tail] = nx; qy[tail] = ny; tail++;
        }
    }
}

static void build_dmap(void) { bfs((int)px, (int)py, dmap); }

static void rebuild_home_maps(void)
{
    int i;
    for (i = 0; i < nEnemies; i++) bfs((int)enemies[i].homeX, (int)enemies[i].homeY, homeMap[i]);
}

/* ------------------------------------------------------------------ */
/* Mensajes, items y armas                                             */
/* ------------------------------------------------------------------ */
static void setmsg(const char *s)
{
    strncpy(msgText, s, sizeof(msgText) - 1);
    msgText[sizeof(msgText) - 1] = 0;
    msgTimer = 150;
}

static void spawn_item(float x, float y, int type, int val)
{
    int i;
    for (i = 0; i < nItems; i++) if (!items[i].active) break;
    if (i == nItems) { if (nItems >= MAX_ITEMS) return; nItems++; }
    items[i].x = x; items[i].y = y; items[i].type = type; items[i].active = 1; items[i].val = val;
}

static int add_ammo(int type, int n)
{
    if (ammo[type] >= AMMO_MAX[type]) return 0;
    ammo[type] += n;
    if (ammo[type] > AMMO_MAX[type]) ammo[type] = AMMO_MAX[type];
    return 1;
}

static int give_weapon(int w, int n)
{
    have[w] = 1;
    add_ammo(AMMO_TYPE[w], n);
    if (w > weapon) { weapon = w; wcool = 8; wtime = 100; }
    return 1;
}

static int pickup(Item *it)
{
    switch (it->type) {
    case IT_MEDKIT:
        if (health >= 100) return 0;
        health += 25; if (health > 100) health = 100;
        setmsg("MEDKIT"); return 1;
    case IT_VEST:
        if (armor >= 100) return 0;
        armor += 50; if (armor > 100) armor = 100;
        setmsg("VEST"); return 1;
    case IT_AMMO_BUL:  if (!add_ammo(0, 20)) return 0; setmsg("BULLETS"); return 1;
    case IT_AMMO_ROCK: if (!add_ammo(1, 4))  return 0; setmsg("ROCKETS"); return 1;
    case IT_AMMO_FUEL: if (!add_ammo(2, 25)) return 0; setmsg("FUEL"); return 1;
    case IT_DROP:      if (!add_ammo(0, it->val)) return 0; return 1;
    case IT_KEY_B: haveKey[0] = 1; setmsg("BLUE KEY"); return 1;
    case IT_KEY_Y: haveKey[1] = 1; setmsg("YELLOW KEY"); return 1;
    case IT_KEY_R: haveKey[2] = 1; setmsg("RED KEY"); return 1;
    case IT_W_MP:     give_weapon(2, 30); setmsg("SMG"); return 1;
    case IT_W_CHAIN:  give_weapon(3, 40); setmsg("CHAINGUN"); return 1;
    case IT_W_ROCKET: give_weapon(4, 5);  setmsg("ROCKET LAUNCHER"); return 1;
    case IT_W_FLAME:  give_weapon(5, 40); setmsg("FLAMETHROWER"); return 1;
    }
    return 0;
}

static int weapon_usable(int w)
{
    if (!have[w]) return 0;
    if (AMMO_TYPE[w] < 0) return 1;
    return ammo[AMMO_TYPE[w]] > 0;
}

static void cycle_weapon(int dir)
{
    int i, w = weapon;
    for (i = 0; i < WEAPON_COUNT; i++) {
        w = (w + dir + WEAPON_COUNT) % WEAPON_COUNT;
        if (weapon_usable(w)) { weapon = w; wcool = 8; wtime = 100; return; }
    }
}

static void damage_player(int dmg)
{
    int a = dmg / 2;
    if (a > armor) a = armor;
    armor -= a;
    health -= dmg - a;
    hurtFlash = 14;
}

/* ------------------------------------------------------------------ */
/* Juego                                                               */
/* ------------------------------------------------------------------ */
static void reset_game(void)
{
    int x, y, i;
    nEnemies = 0; nItems = 0;
    memset(wallMap, 0, sizeof(wallMap));
    memset(doorOpen, 0, sizeof(doorOpen));
    memset(doorMoving, 0, sizeof(doorMoving));
    for (y = 0; y < MAPH; y++) {
        for (x = 0; x < MAPW; x++) {
            char c = level[y][x];
            if (c >= '1' && c <= '4') wallMap[y][x] = (unsigned char)(c - '0');
            else if (c == 'd') wallMap[y][x] = 5;
            else if (c == 'b') wallMap[y][x] = 6;
            else if (c == 'y') wallMap[y][x] = 7;
            else if (c == 'r') wallMap[y][x] = 8;
            else if (c == 'X') wallMap[y][x] = 9;
            else if (c == 'P') { px = x + 0.5f; py = y + 0.5f; }
        }
    }
    for (i = 0; i < N_ENEMY_DEFS && nEnemies < MAX_ENEMIES; i++) {
        Enemy *e = &enemies[nEnemies++];
        memset(e, 0, sizeof(*e));
        e->x = e->homeX = ENEMY_DEFS[i][0] + 0.5f;
        e->y = e->homeY = ENEMY_DEFS[i][1] + 0.5f;
        e->face = e->homeFace = ENEMY_DEFS[i][2] * PI_F / 180.0f;
        e->hp = 6; e->state = E_IDLE; e->frame = 0;
        e->timer = (i * 37) % 500;   /* para que no giren todos a la vez */
    }
    for (i = 0; i < N_ITEM_DEFS; i++)
        spawn_item(ITEM_DEFS[i][0] + 0.5f, ITEM_DEFS[i][1] + 0.5f, ITEM_DEFS[i][2], 0);

    pa = 0.0f; bob = 0.0f; stepAcc = 0.0f;
    health = 100; armor = 0; kills = 0;
    memset(have, 0, sizeof(have)); memset(ammo, 0, sizeof(ammo)); memset(haveKey, 0, sizeof(haveKey));
    have[0] = 1; have[1] = 1; ammo[0] = 30; weapon = 1;
    wcool = 0; wtime = 100; shots = 0;
    hurtFlash = 0; gameState = G_PLAY;
    setmsg("FIND THE EXIT");
    build_dmap();
    rebuild_home_maps();
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
    e->lost = 0;
    if (e->hp <= 0) {
        e->state = E_DEAD;
        kills++;
        if (rand() % 3) spawn_item(e->x, e->y, IT_DROP, 10);
    } else {
        e->state = E_PAIN;
    }
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
            if ((e->state == E_IDLE || e->state == E_RETURN) && dx * dx + dy * dy < 9.0f * 9.0f) {
                e->state = E_CHASE; e->timer = 0; e->lost = 0;
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
        if (w->multi) hurt_enemy(e, w->dmg);
        else if (d < bestd) { bestd = d; best = i; }
    }

    if (best >= 0) {
        Enemy *t = &enemies[best];
        float tx = t->x, ty = t->y;
        hurt_enemy(t, w->dmg);
        if (w->splash) {
            for (i = 0; i < nEnemies; i++) {
                float dx, dy;
                if (i == best || enemies[i].state == E_DEAD) continue;
                dx = enemies[i].x - tx; dy = enemies[i].y - ty;
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
    if (speed > d) speed = d;
    try_move(&e->x, &e->y, dx / d * speed, dy / d * speed, 0.28f);
}

/* Elige la celda vecina con menor distancia segun el mapa m */
static void best_neighbor(const Enemy *e, short m[MAPH][MAPW], float *tx, float *ty)
{
    static const int dxs[4] = { 1, -1, 0, 0 }, dys[4] = { 0, 0, 1, -1 };
    int cx = (int)e->x, cy = (int)e->y, bx = cx, by = cy, k;
    int bestv;
    if (cx < 0 || cy < 0 || cx >= MAPW || cy >= MAPH) { *tx = e->x; *ty = e->y; return; }
    bestv = m[cy][cx];
    for (k = 0; k < 4; k++) {
        int nx = cx + dxs[k], ny = cy + dys[k];
        if (nx < 0 || ny < 0 || nx >= MAPW || ny >= MAPH) continue;
        if (m[ny][nx] < bestv) { bestv = m[ny][nx]; bx = nx; by = ny; }
    }
    *tx = bx + 0.5f; *ty = by + 0.5f;
}

static int enemy_sees(const Enemy *e)
{
    float dx = px - e->x, dy = py - e->y;
    float d = sqrtf(dx * dx + dy * dy);
    if (d > 11.0f) return 0;
    if (!los(e->x, e->y, px, py)) return 0;
    if (d < 2.5f) return 1;
    return fabsf(norm_angle(atan2f(dy, dx) - e->face)) < 1.1f;   /* cono de vision */
}

static void enemy_step_sound(const Enemy *e)
{
    float dx = e->x - px, dy = e->y - py;
    float d = sqrtf(dx * dx + dy * dy), vol;
    if (d > 14.0f) return;
    vol = 1.0f - d / 14.0f;
    if (!los(e->x, e->y, px, py)) vol *= 0.4f;
    sfx_step(vol * 0.85f);
}

static void enemy_update(Enemy *e)
{
    float dx = px - e->x, dy = py - e->y;
    float d = sqrtf(dx * dx + dy * dy);

    switch (e->state) {
    case E_IDLE: {
        float target = e->homeFace + 0.9f * sinf(e->timer * 0.012f);  /* mira a los lados */
        e->frame = 0;
        e->timer++;
        turn_toward(&e->face, target, 0.03f);
        if (enemy_sees(e)) { e->state = E_CHASE; e->timer = 0; e->lost = 0; }
        break;
    }

    case E_CHASE: {
        int see = los(e->x, e->y, px, py);
        e->timer++;
        e->frame = 1 + (e->timer / 8) % 4;
        if (e->timer % 16 == 0) enemy_step_sound(e);
        if (e->cool > 0) e->cool--;
        if (see) e->lost = 0; else e->lost++;
        if (e->lost > 420) { e->state = E_RETURN; e->timer = 0; break; }
        if (see) turn_toward(&e->face, atan2f(dy, dx), 0.12f);
        if (see && d < 8.0f && e->cool == 0) {
            e->state = E_ATTACK; e->timer = 0; e->frame = EF_ATTACK;
            break;
        }
        if (d > 1.1f) {
            float tx, ty;
            if (see) { tx = px; ty = py; }
            else best_neighbor(e, dmap, &tx, &ty);
            enemy_step_toward(e, tx, ty, 0.025f);
            if (!see) turn_toward(&e->face, atan2f(ty - e->y, tx - e->x), 0.12f);
        }
        break;
    }

    case E_RETURN: {
        float hx = e->homeX - e->x, hy = e->homeY - e->y;
        float hd = sqrtf(hx * hx + hy * hy), tx, ty;
        e->timer++;
        e->frame = 1 + (e->timer / 8) % 4;
        if (e->timer % 16 == 0) enemy_step_sound(e);
        if (enemy_sees(e)) { e->state = E_CHASE; e->timer = 0; e->lost = 0; break; }
        if (hd < 0.15f) {
            e->x = e->homeX; e->y = e->homeY;
            e->state = E_IDLE; e->timer = 0;
            break;
        }
        if ((int)e->x == (int)e->homeX && (int)e->y == (int)e->homeY) { tx = e->homeX; ty = e->homeY; }
        else best_neighbor(e, homeMap[e - enemies], &tx, &ty);
        enemy_step_toward(e, tx, ty, 0.022f);
        turn_toward(&e->face, atan2f(ty - e->y, tx - e->x), 0.15f);
        break;
    }

    case E_ATTACK:
        e->timer++;
        turn_toward(&e->face, atan2f(dy, dx), 0.2f);
        e->frame = EF_ATTACK + (e->timer / 8 > 2 ? 2 : e->timer / 8);
        if (e->timer == 16) {
            int chance = (int)(90.0f - d * 8.0f);
            if (chance < 20) chance = 20;
            if (los(e->x, e->y, px, py) && (rand() % 100) < chance) damage_player(6 + rand() % 7);
        }
        if (e->timer >= 26) {
            e->state = E_CHASE; e->timer = 0;
            e->cool = 40 + rand() % 40;
        }
        break;

    case E_PAIN:
        e->timer++;
        e->frame = EF_HURT + (e->timer > 7 ? 1 : 0);
        if (e->timer >= 14) { e->state = E_CHASE; e->timer = 0; e->lost = 0; }
        break;

    case E_DEAD:
        e->timer++;
        e->frame = EF_DEATH + (e->timer / 6 > 4 ? 4 : e->timer / 6);
        break;
    }
}

/* Puertas: se abren al acercarse; las de color piden llave; la verde es la salida */
static void check_doors(void)
{
    int cx = (int)px, cy = (int)py, dx, dy;
    for (dy = -1; dy <= 1; dy++) {
        for (dx = -1; dx <= 1; dx++) {
            int x = cx + dx, y = cy + dy, t;
            float ddx, ddy;
            if (x < 0 || y < 0 || x >= MAPW || y >= MAPH) continue;
            t = wallMap[y][x];
            if (t < 5 || t > 9 || doorMoving[y][x]) continue;
            ddx = x + 0.5f - px; ddy = y + 0.5f - py;
            if (ddx * ddx + ddy * ddy > 1.35f * 1.35f) continue;
            if (t == 5) doorMoving[y][x] = 1;
            else if (t == 9) gameState = G_WIN;
            else if (haveKey[t - 6]) doorMoving[y][x] = 1;
            else if (msgTimer < 20) {
                setmsg(t == 6 ? "NEED BLUE KEY" : (t == 7 ? "NEED YELLOW KEY" : "NEED RED KEY"));
            }
        }
    }
}

static void update_doors(void)
{
    int x, y, opened = 0;
    for (y = 0; y < MAPH; y++) {
        for (x = 0; x < MAPW; x++) {
            if (!doorMoving[y][x]) continue;
            doorOpen[y][x] += 0.03f;
            if (doorOpen[y][x] >= 0.95f) {
                wallMap[y][x] = 0; doorMoving[y][x] = 0; doorOpen[y][x] = 0.0f;
                opened = 1;
            }
        }
    }
    if (opened) { rebuild_home_maps(); build_dmap(); }
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
        float sp = 0.045f, dx, dy, ox = px, oy = py, moved;
        if (m > 1.0f) { f /= m; s /= m; }
        dx = (cosf(pa) * f - sinf(pa) * s) * sp;
        dy = (sinf(pa) * f + cosf(pa) * s) * sp;
        try_move(&px, &py, dx, dy, 0.25f);
        moved = sqrtf((px - ox) * (px - ox) + (py - oy) * (py - oy));
        stepAcc += moved;
        if (stepAcc > 0.75f) { stepAcc = 0.0f; sfx_step(0.55f); }
        if (m > 0.1f) bob += 0.22f; else bob *= 0.9f;
    }

    check_doors();
    update_doors();

    /* armas */
    if (wcool > 0) wcool--;
    wtime++;
    if (in->prev && wcool == 0) cycle_weapon(-1);
    if (in->next && wcool == 0) cycle_weapon(1);
    if (in->fire && wcool == 0) {
        int at = AMMO_TYPE[weapon];
        if (at >= 0 && ammo[at] <= 0) {
            setmsg("NO AMMO");
            cycle_weapon(-1);
            wcool = 15;
        } else {
            if (at >= 0) ammo[at]--;
            player_shoot();
            wcool = WEAP[weapon].cd;
            wtime = 0;
            shots++;
            if (at >= 0 && ammo[at] <= 0 && !weapon_usable(weapon)) {
                setmsg("NO AMMO");
            }
        }
    }

    /* items */
    for (i = 0; i < nItems; i++) {
        float dx, dy;
        if (!items[i].active) continue;
        dx = items[i].x - px; dy = items[i].y - py;
        if (dx * dx + dy * dy < 0.45f * 0.45f && pickup(&items[i])) items[i].active = 0;
    }

    /* enemigos */
    if (frameCount % 10 == 0) build_dmap();
    for (i = 0; i < nEnemies; i++) enemy_update(&enemies[i]);

    if (hurtFlash > 0) hurtFlash--;
    if (msgTimer > 0) msgTimer--;
    if (health <= 0) { health = 0; gameState = G_DEAD; }
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

    /* paredes con textura */
    for (c = 0; c < ncols; c++) {
        float camx = 2.0f * (c + 0.5f) / (float)ncols - 1.0f;
        float rdx = dirx + plx * camx, rdy = diry + ply * camx;
        int mx = (int)floorf(px), my = (int)floorf(py);
        float ddx = (rdx == 0.0f) ? 1e30f : fabsf(1.0f / rdx);
        float ddy = (rdy == 0.0f) ? 1e30f : fabsf(1.0f / rdy);
        int stepx, stepy, side = 0, hit = 0, type = 1, guard = 0, texX, sh;
        float sdx, sdy, dist, lh, ytop, ybot, yt, vmax, y0, y1, v0, v1, wallX, open = 0.0f;

        if (rdx < 0) { stepx = -1; sdx = (px - mx) * ddx; }
        else         { stepx =  1; sdx = (mx + 1.0f - px) * ddx; }
        if (rdy < 0) { stepy = -1; sdy = (py - my) * ddy; }
        else         { stepy =  1; sdy = (my + 1.0f - py) * ddy; }

        while (!hit && guard++ < 96) {
            if (sdx < sdy) { sdx += ddx; mx += stepx; side = 0; }
            else           { sdy += ddy; my += stepy; side = 1; }
            if (mx < 0 || my < 0 || mx >= MAPW || my >= MAPH) { hit = 1; type = 1; mx = my = 0; }
            else if (wallMap[my][mx]) { hit = 1; type = wallMap[my][mx]; }
        }
        dist = (side == 0) ? (sdx - ddx) : (sdy - ddy);
        if (dist < 0.05f) dist = 0.05f;
        zbuf[c] = dist;

        wallX = (side == 0) ? (py + dist * rdy) : (px + dist * rdx);
        wallX -= floorf(wallX);
        texX = (int)(wallX * WALL_SIZE);
        if (texX > WALL_SIZE - 1) texX = WALL_SIZE - 1;
        if (side == 0 && rdx > 0) texX = WALL_SIZE - 1 - texX;
        if (side == 1 && rdy < 0) texX = WALL_SIZE - 1 - texX;

        if (type >= 5 && type <= 8) open = doorOpen[my][mx];
        lh = SH / dist;
        ytop = SH / 2.0f - lh / 2.0f;
        ybot = ytop + lh;
        yt = ytop + open * lh;                 /* la puerta se hunde en el piso */
        vmax = WALL_SIZE * (1.0f - open);
        if (ybot - yt < 1.0f) continue;
        y0 = yt < 0 ? 0 : yt;
        y1 = ybot > SH ? (float)SH : ybot;
        v0 = (y0 - yt) / (ybot - yt) * vmax;
        v1 = (y1 - yt) / (ybot - yt) * vmax;

        sh = (int)(0x80 * shade(dist) * (side ? 0.72f : 1.0f));
        gsKit_prim_sprite_texture(gs, &walltex[type - 1],
            (float)(c * COLW), y0, (float)texX, v0,
            (float)(c * COLW + COLW), y1, (float)texX + 1.0f, v1,
            1, rgba(sh, sh, sh, 0x80));
    }
}

/* Dibuja un sprite plano (billboard) con oclusion por columnas */
static void draw_billboard(GSTEXTURE *t, int tsz, float wx, float wy, float scale, float lift)
{
    float dirx = cosf(pa), diry = sinf(pa);
    float plx = -diry * FOV, ply = dirx * FOV;
    float sx = wx - px, sy = wy - py;
    float inv = 1.0f / (plx * diry - dirx * ply);
    float tx = inv * (diry * sx - dirx * sy);
    float ty = inv * (-ply * sx + plx * sy);
    float scr, h, size, bottom, top, x0, x1, v;
    int c0, c1, c, shd;

    if (ty < 0.4f) return;
    scr = (SW / 2.0f) * (1.0f + tx / ty);
    h = SH / ty;
    size = h * scale;
    bottom = SH / 2.0f + h / 2.0f - lift * h;
    top = bottom - size;
    x0 = scr - size / 2.0f; x1 = scr + size / 2.0f;
    c0 = (int)floorf(x0 / COLW); c1 = (int)floorf((x1 - 1.0f) / COLW);
    if (c0 < 0) c0 = 0;
    if (c1 > ncols - 1) c1 = ncols - 1;

    v = shade(ty) * 1.25f; if (v > 1.0f) v = 1.0f;
    shd = (int)(0x80 * v);

    c = c0;
    while (c <= c1) {
        if (ty < zbuf[c]) {
            int s = c;
            float xs, xe;
            while (c <= c1 && ty < zbuf[c]) c++;
            xs = (float)(s * COLW); xe = (float)(c * COLW);
            if (xs < x0) xs = x0;
            if (xe > x1) xe = x1;
            if (xe <= xs) continue;
            gsKit_prim_sprite_texture(gs, t,
                xs, top, (xs - x0) / size * tsz, 0.0f,
                xe, bottom, (xe - x0) / size * tsz, (float)tsz,
                1, rgba(shd, shd, shd, 0x80));
        } else {
            c++;
        }
    }
}

/* Cuadro del enemigo segun hacia donde mira respecto a la camara (8 angulos) */
static int enemy_frame_index(const Enemy *e)
{
    int k;
    float a;
    if (e->frame >= EF_HURT) return e->frame;
    a = atan2f(e->y - py, e->x - px);
    k = (int)floorf(norm_angle(e->face - a - PI_F) / (PI_F / 4.0f) + 0.5f) & 7;
    return e->frame * 8 + k;
}

typedef struct { float d2; int kind, idx; } Spr;

static void draw_sprites(void)
{
    static Spr list[MAX_ENEMIES + MAX_ITEMS];
    int n = 0, i, j;
    for (i = 0; i < nEnemies; i++) {
        float dx = enemies[i].x - px, dy = enemies[i].y - py;
        list[n].d2 = dx * dx + dy * dy; list[n].kind = 0; list[n].idx = i; n++;
    }
    for (i = 0; i < nItems; i++) {
        float dx, dy;
        if (!items[i].active) continue;
        dx = items[i].x - px; dy = items[i].y - py;
        list[n].d2 = dx * dx + dy * dy; list[n].kind = 1; list[n].idx = i; n++;
    }
    /* lejos -> cerca */
    for (i = 1; i < n; i++) {
        Spr s = list[i];
        for (j = i - 1; j >= 0 && list[j].d2 < s.d2; j--) list[j + 1] = list[j];
        list[j + 1] = s;
    }
    for (i = 0; i < n; i++) {
        if (list[i].kind == 0) {
            const Enemy *e = &enemies[list[i].idx];
            draw_billboard(&etex[enemy_frame_index(e)], ENEMY_SIZE, e->x, e->y, 1.0f, 0.0f);
        } else {
            const Item *it = &items[list[i].idx];
            float lift = 0.0f;
            if (it->type >= IT_W_MP && it->type <= IT_W_FLAME) {
                draw_billboard(&wtex[(it->type - IT_W_MP + 2) * WEAPON_FRAMES], WEAPON_SIZE,
                               it->x, it->y, 0.42f, 0.05f);
            } else if (it->type == IT_DROP) {
                draw_billboard(&itemtex[IT_AMMO_BUL], ITEM_SIZE, it->x, it->y, 0.28f, 0.0f);
            } else {
                if (it->type >= IT_KEY_B) lift = 0.12f + 0.05f * sinf(frameCount * 0.08f);
                draw_billboard(&itemtex[it->type], ITEM_SIZE, it->x, it->y, 0.4f, lift);
            }
        }
    }
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
    case 'B': return "#### #   ##### #   ##### ";
    case 'C': return " #####    #    #     ####";
    case 'D': return "#### #   ##   ##   ##### ";
    case 'E': return "######    #### #    #####";
    case 'F': return "######    #### #    #    ";
    case 'G': return " #####    #  ###   # ####";
    case 'H': return "#   ##   #######   ##   #";
    case 'I': return "#####  #    #    #  #####";
    case 'J': return "  ###    #    ##   # ### ";
    case 'K': return "#   ##  # ###  #  # #   #";
    case 'L': return "#    #    #    #    #####";
    case 'M': return "#   ### ### # ##   ##   #";
    case 'N': return "#   ###  ## # ##  ###   #";
    case 'O': return " ### #   ##   ##   # ### ";
    case 'P': return "#### #   ##### #    #    ";
    case 'Q': return " ### #   ## # ##  #  ## #";
    case 'R': return "#### #   ##### #  # #   #";
    case 'S': return " #####     ###     ##### ";
    case 'T': return "#####  #    #    #    #  ";
    case 'U': return "#   ##   ##   ##   # ### ";
    case 'V': return "#   ##   ##   # # #   #  ";
    case 'W': return "#   ##   ## # ### ###   #";
    case 'X': return "#   # # #   #   # # #   #";
    case 'Y': return "#   # # #   #    #    #  ";
    case 'Z': return "#####   #   #   #   #####";
    case '0': return " ### #  ### # ###  # ### ";
    case '1': return "  #   ##    #    #   ### ";
    case '2': return " ### #   #  ##  #   #####";
    case '3': return "####     # ###     ##### ";
    case '4': return "#  # #  # #####   #    # ";
    case '5': return "######    ####     ##### ";
    case '6': return " ### #    #### #   # ### ";
    case '7': return "#####    #   #   #    #  ";
    case '8': return " ### #   # ### #   # ### ";
    case '9': return " ### #   # ####    # ### ";
    case '.': return "                ##   ##  ";
    case '!': return "  #    #    #         #  ";
    case ':': return "      ##        ##       ";
    case '-': return "          #####          ";
    case '/': return "    #   #   #   #   #    ";
    default: return NULL;
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

static void draw_text_shadow(const char *s, float x, float y, float sc, u64 col)
{
    draw_text(s, x + sc * 0.6f, y + sc * 0.6f, sc, rgba(0, 0, 0, 0x70));
    draw_text(s, x, y, sc, col);
}

static void draw_number(int n, float x, float y, float sc, u64 col)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", n);
    draw_text_shadow(buf, x, y, sc, col);
}

static void stat_row(const char *label, int value, int max, float y, int r, int g, int b)
{
    float m = 24.0f, bx = m + 36.0f, bw = 110.0f;
    draw_text_shadow(label, m, y, 2.0f, rgba(240, 240, 240, 0x80));
    rect(bx - 2, y - 2, bx + bw + 2, y + 12, rgba(240, 240, 240, 0x80));
    rect(bx, y, bx + bw, y + 10, rgba(20, 20, 20, 0x80));
    rect(bx, y, bx + bw * (value / (float)max), y + 10, rgba(r, g, b, 0x80));
    draw_number(value, bx + bw + 10.0f, y - 1.0f, 2.0f, rgba(255, 255, 255, 0x80));
}

static void draw_hud(void)
{
    int i, alive = enemies_alive(), at = AMMO_TYPE[weapon];
    float m = 24.0f;
    static const int kc[3][3] = { { 50, 100, 240 }, { 240, 210, 40 }, { 230, 50, 50 } };
    static const char *aname[3] = { "BULLETS", "ROCKETS", "FUEL" };

    stat_row("HP", health, 100, SH - m - 36.0f, 210, 20, 20);
    stat_row("AR", armor, 100, SH - m - 14.0f, 50, 120, 230);

    /* municion del arma actual */
    if (at >= 0) {
        char buf[16];
        float wd;
        snprintf(buf, sizeof(buf), "%d", ammo[at]);
        wd = text_width(buf, 4.0f);
        draw_text_shadow(buf, SW - m - wd, SH - m - 22.0f, 4.0f, rgba(255, 230, 80, 0x80));
        draw_text_shadow(aname[at], SW - m - text_width(aname[at], 2.0f), SH - m - 38.0f, 2.0f, rgba(220, 220, 220, 0x80));
    }

    /* armas */
    for (i = 0; i < WEAPON_COUNT; i++) {
        float x = m + i * 16.0f, y = m;
        u64 c = (i == weapon) ? rgba(255, 220, 40, 0x80) : (have[i] ? rgba(200, 200, 200, 0x80) : rgba(70, 70, 70, 0x60));
        rect(x, y, x + 12, y + 12, c);
    }
    /* llaves */
    for (i = 0; i < 3; i++) {
        float x = m + i * 22.0f, y = m + 20.0f;
        if (haveKey[i]) rect(x, y, x + 18, y + 11, rgba(kc[i][0], kc[i][1], kc[i][2], 0x80));
        else            rect(x, y, x + 18, y + 11, rgba(60, 60, 60, 0x50));
    }
    /* enemigos que quedan */
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "EN %d", alive);
        draw_text_shadow(buf, SW - m - text_width(buf, 2.0f), m, 2.0f, rgba(255, 120, 120, 0x80));
    }

    /* mira */
    rect(SW / 2.0f - 1, SH / 2.0f - 9, SW / 2.0f + 1, SH / 2.0f - 3, rgba(255, 255, 255, 0x60));
    rect(SW / 2.0f - 1, SH / 2.0f + 3, SW / 2.0f + 1, SH / 2.0f + 9, rgba(255, 255, 255, 0x60));
    rect(SW / 2.0f - 9, SH / 2.0f - 1, SW / 2.0f - 3, SH / 2.0f + 1, rgba(255, 255, 255, 0x60));
    rect(SW / 2.0f + 3, SH / 2.0f - 1, SW / 2.0f + 9, SH / 2.0f + 1, rgba(255, 255, 255, 0x60));

    /* mensaje */
    if (msgTimer > 0 && msgText[0]) {
        draw_text_shadow(msgText, (SW - text_width(msgText, 3.0f)) / 2.0f, SH * 0.22f, 3.0f, rgba(255, 255, 255, 0x80));
    }
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
        char buf[32];
        rect(0, 0, (float)SW, (float)SH, rgba(0, 110, 0, 0x50));
        draw_text(t, (SW - text_width(t, 10)) / 2.0f, SH / 2.0f - 70, 10, rgba(255, 255, 255, 0x80));
        snprintf(buf, sizeof(buf), "KILLS %d", kills);
        draw_text(buf, (SW - text_width(buf, 4)) / 2.0f, SH / 2.0f + 5, 4, rgba(255, 255, 255, 0x80));
        draw_text(p, (SW - text_width(p, 4)) / 2.0f, SH / 2.0f + 45, 4, rgba(255, 220, 40, 0x80));
    }
}

static void game_render(void)
{
    gsKit_clear(gs, rgba(0, 0, 0, 0x80));
    draw_world();
    draw_sprites();
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
        acc = ((acc << 6) | v) & 0xFFFFFF; bits += 6;
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
    audio_init();

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
    load_sheet(walls_b64, WALL_TEXTURES, WALL_SIZE, walltex);
    load_sheet(items_b64, ITEM_TEXTURES, ITEM_SIZE, itemtex);

    srand(12345);
    reset_game();

    for (;;) {
        read_input(&in);
        game_update(&in);
        audio_update();
        game_render();
        gsKit_queue_exec(gs);
        gsKit_sync_flip(gs);
    }
    return 0;
}
