/*
 * DEAD LOCK PROTOCOL - BETA 4
 * Raycaster estilo Wolfenstein para PlayStation 2 (PS2SDK + gsKit).
 *
 * Controles:
 *   Stick izq. / D-pad arriba-abajo ... avanzar / retroceder
 *   Stick der. X / D-pad izq-der ...... girar
 *   Cuadrado / Circulo ................ moverse de lado
 *   X o R2 ............................ disparar
 *   L1 / R1 ........................... cambiar de arma
 *   TRIANGULO ......................... recargar
 *   L2 ................................ soltar arma / cambiar por la del suelo
 *   START ............................. pausa (en el menu: X acepta, O vuelve)
 * (los botones se pueden cambiar en CONTROLES)
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
#include "sounds.h"

/* Tipos de item (los 11 primeros coinciden con el icono de items_b64) */
enum { IT_MEDKIT, IT_VEST, IT_AMMO_PIS, IT_AMMO_SMG, IT_AMMO_CHAIN, IT_AMMO_SHELL,
       IT_AMMO_ROCK, IT_KEY_B, IT_KEY_Y, IT_KEY_R,
       /* armas: tipo - IT_W_KNIFE = numero de arma */
       IT_W_KNIFE, IT_W_PISTOL, IT_W_SHOT, IT_W_MP, IT_W_CHAIN, IT_W_ROCKET };

/* Armas: 0 cuchillo, 1 pistola, 2 escopeta, 3 metralleta, 4 ametralladora, 5 lanzacohetes,
          Solo se pueden llevar MAX_WEAPONS a la vez. */
/* Municion de reserva: 0 pistola, 1 metralleta, 2 ametralladora, 3 cartuchos, 4 cohetes */
#define MAX_WEAPONS 3

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
enum { G_PLAY, G_DEAD, G_WIN, G_MENU, G_PAUSE };
enum { M_MAIN, M_DIFF, M_OPT, M_CTRL, M_PAUSE };
enum { A_FIRE, A_FIRE2, A_RELOAD, A_PREV, A_NEXT, A_LEFT, A_RIGHT, A_SWAP, NACT };

typedef struct {
    float x, y, face, homeX, homeY, homeFace;
    int hp, state, timer, frame, cool, lost, type, gun, aux;   /* gun: 0 pistola, 1 metralleta, 2 jefe */
} Enemy;

typedef struct { float x, y; int type, active, val, delay; } Item;

typedef struct {
    int dmg, cd;
    float range, tol;
    int multi, splash, noise;
    int mag, reload, atype, falloff;   /* cargador, cuadros de recarga, tipo de municion, dano baja con la distancia */
} Weapon;

typedef struct {
    float fwd, strafe, turn;
    int fire, prev, next, start, reload, swap;
} Input;

static const Weapon WEAP[WEAPON_COUNT] = {
    /* dmg cd  alcance tol    multi splash ruido cargador recarga municion falloff */
    {  2, 22,  1.7f, 0.0f,   0,    0,     0,    0,       0,      -1,      0 },  /* cuchillo        */
    {  2, 24, 14.0f, 0.0f,   0,    0,     1,    7,      80,       0,      0 },  /* pistola         */
    {  7, 45,  9.0f, 0.20f,  1,    0,     1,    2,     100,       3,      1 },  /* escopeta        */
    {  1,  8, 14.0f, 0.0f,   0,    0,     1,   30,     110,       1,      0 },  /* metralleta      */
    {  1,  4, 14.0f, 0.0f,   0,    0,     1,  100,     200,       2,      0 },  /* ametralladora   */
    {  6, 70, 16.0f, 0.0f,   0,    3,     1,    1,      70,       4,      0 },  /* lanzacohetes    */
};
#define AMMO_TYPES 5
static const int AMMO_MAX[AMMO_TYPES] = { 70, 180, 300, 32, 20 };
static const int AMMO_PICK[AMMO_TYPES] = { 14, 30, 60, 8, 4 };
static const char *AMMO_NAME[AMMO_TYPES] = { "PISTOL", "SMG", "CHAINGUN", "SHELLS", "ROCKETS" };
static const char *WNAME[WEAPON_COUNT] = { "KNIFE", "PISTOL", "SHOTGUN", "SMG", "CHAINGUN", "ROCKET" };
static const int WGIVE[WEAPON_COUNT] = { 0, 14, 8, 30, 60, 5 };  /* reserva que trae un arma nueva */

/* Cuadros de textura de cada arma (cuantos tiene cada una y donde empieza) */
static const int WNF[WEAPON_COUNT]   = { 4, 8, 8, 4, 4, 4 };
static const int WBASE[WEAPON_COUNT] = { 0, 4, 12, 20, 24, 28 };

/* Dificultad: 0 facil, 1 medio, 2 dificil */
static const float DIFF_DMG[3] = { 0.6f, 1.0f, 1.5f };
static const float DIFF_HP[3]  = { 0.7f, 1.0f, 1.4f };

/* Enemigos: 0 soldado blanco, 1 soldado verde (mas duro) */
/* tipos: 0 blanco, 1 verde, 2 explosivo, 3 guardia azul (metralleta), 4 SS (metralleta), 5 jefe */
static const int   EHP[ENEMY_TYPES]    = { 6, 9, 4, 8, 12, 60 };
static const float ESPEED[ENEMY_TYPES] = { 0.025f, 0.030f, 0.055f, 0.028f, 0.032f, 0.020f };
static const int   EDMG[ENEMY_TYPES]   = { 6, 9, 0, 8, 10, 10 };
static const int   ECOOL[ENEMY_TYPES]  = { 40, 26, 0, 30, 24, 50 };

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
static int have[WEAPON_COUNT], ammo[AMMO_TYPES], mag[WEAPON_COUNT], haveKey[3], kills;
static int reloading, rtime;

/* Ajustes y menu */
static int diff = 1, sens = 5, volume = 10;
static u32 bind[NACT];
static int menuScreen, menuSel, menuBack, rebind = -1;
static u32 padPress;      /* botones recien presionados (con repeticion) */
static char msgText[40];
static int msgTimer;

static GSGLOBAL *gs;
static GSTEXTURE wtex[WEAPON_TOTAL];
static GSTEXTURE etex[ENEMY_SETS];     /* un atlas por tipo (8 x 7 cuadros de 64x64) */
static GSTEXTURE bosstex, facetex;
static int curLevel, grinTimer, faceHurt, faceDir;
static GSTEXTURE walltex[WALL_TEXTURES];
static GSTEXTURE itemtex[ITEM_TEXTURES];
static GSTEXTURE doortex[DOOR_FRAMES];
static GSTEXTURE titletex;
static int SW = 640, SH = 448, ncols = 160;
static float zbuf[MAXCOLS];

/* ------------------------------------------------------------------ */
/* Sonido (pasos). Se compila solo con -DUSE_SOUND                     */
/* ------------------------------------------------------------------ */
#define SND_FRAME  368          /* muestras por cuadro a 60 fps */
#define NVOICE     12
enum { SND_STEP, SND_PISTOL, SND_SHOTGUN, SND_SMG, SND_CHAIN, SND_BOOM };
static const short *SND_DATA[6];
static int SND_LEN[6];
#define BOOM_LEN 14000
static short boomWave[BOOM_LEN];

/* explosion: ruido grave que se apaga + golpe de baja frecuencia */
static void boom_init(void)
{
    int i;
    float y = 0.0f;
    for (i = 0; i < BOOM_LEN; i++) {
        float env = expf(-(float)i / 3200.0f);
        float n = ((rand() & 0xFFFF) / 32768.0f) - 1.0f;
        float thump = sinf(2.0f * PI_F * 48.0f * (float)i / SND_RATE) * expf(-(float)i / 2600.0f);
        y = y * 0.90f + n * 0.10f;
        boomWave[i] = (short)((y * 1.4f + thump * 0.9f) * env * 15000.0f);
    }
    SND_DATA[SND_BOOM] = boomWave; SND_LEN[SND_BOOM] = BOOM_LEN;
}
static struct { int pos, snd; float vol; int active; } voices[NVOICE];
static int audio_ok = 0;
static int ringBytes = 0;     /* tamano del buffer de audsrv */

#ifdef USE_SOUND
extern unsigned char audsrv_irx[];
extern unsigned int size_audsrv_irx;
#endif

static void audio_init(void)
{
#ifdef USE_SOUND
    int ret = 0;
    audsrv_fmt_t fmt;

    sbv_patch_enable_lmb();
    SifLoadModule("rom0:LIBSD", 0, NULL);
    if (SifExecModuleBuffer(audsrv_irx, size_audsrv_irx, 0, NULL, &ret) < 0) return;
    if (audsrv_init() != 0) return;
    fmt.freq = SND_RATE; fmt.bits = 16; fmt.channels = 1;
    if (audsrv_set_format(&fmt) != 0) return;
    audsrv_set_volume(volume * 10);
    ringBytes = audsrv_available();

    audio_ok = 1;
#endif
}

static void play_sound(int snd, float vol)
{
    int i, slot = 0, oldest = -1;
    vol *= volume / 10.0f;
    if (!audio_ok || vol <= 0.03f) return;
    if (vol > 1.0f) vol = 1.0f;
    for (i = 0; i < NVOICE; i++) {
        if (!voices[i].active) { slot = i; oldest = 1 << 30; break; }
        if (voices[i].pos > oldest) { oldest = voices[i].pos; slot = i; }
    }
    voices[slot].pos = 0; voices[slot].snd = snd; voices[slot].vol = vol; voices[slot].active = 1;
}

static void sfx_step(float vol) { play_sound(SND_STEP, vol); }

static void audio_update(void)
{
#ifdef USE_SOUND
    /* Mantiene unos ~100 ms de audio en cola, sin importar a cuantos fps corra el juego
       (antes se mandaba un trozo fijo por cuadro y a menos de 60 fps sonaba cortado). */
    int loops;
    if (!audio_ok) return;
    for (loops = 0; loops < 10; loops++) {
        int acc[SND_FRAME], i, s, any = 0, queued;
        short buf[SND_FRAME];
        queued = ringBytes - audsrv_available();
        if (queued >= 4096) break;                       /* ya hay suficiente en cola */
        memset(acc, 0, sizeof(acc));
        for (i = 0; i < NVOICE; i++) {
            if (!voices[i].active) continue;
            any = 1;
            for (s = 0; s < SND_FRAME; s++) {
                int p = voices[i].pos + s;
                if (p >= SND_LEN[voices[i].snd]) break;
                acc[s] += (int)(SND_DATA[voices[i].snd][p] * voices[i].vol);
            }
            voices[i].pos += SND_FRAME;
            if (voices[i].pos >= SND_LEN[voices[i].snd]) voices[i].active = 0;
        }
        (void)any;      /* sin voces se manda silencio: asi el buffer nunca se vacia ni repite basura */
        for (s = 0; s < SND_FRAME; s++) {
            int v = acc[s];
            if (v > 32000) v = 32000;
            if (v < -32000) v = -32000;
            buf[s] = (short)v;
        }
        if (audsrv_available() < (int)sizeof(buf)) break;
        audsrv_play_audio((char *)buf, sizeof(buf));
    }
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

static int spawn_item(float x, float y, int type, int val)
{
    int i;
    for (i = 0; i < nItems; i++) if (!items[i].active) break;
    if (i == nItems) { if (nItems >= MAX_ITEMS) return -1; nItems++; }
    items[i].x = x; items[i].y = y; items[i].type = type; items[i].active = 1; items[i].val = val; items[i].delay = 0;
    return i;
}

static int add_ammo(int type, int n)
{
    if (ammo[type] >= AMMO_MAX[type]) return 0;
    ammo[type] += n;
    if (ammo[type] > AMMO_MAX[type]) ammo[type] = AMMO_MAX[type];
    return 1;
}

static void select_weapon(int w)
{
    weapon = w; wcool = 8; wtime = 100;
    reloading = 0;                    /* cambiar de arma cancela la recarga */
}

static int weapons_carried(void)
{
    int i, n = 0;
    for (i = 0; i < WEAPON_COUNT; i++) n += have[i];
    return n;
}

/* Toma el arma del suelo. val = 0: nueva (cargada y con reserva); val > 0: soltada, trae mag = val - 1 */
static void take_weapon(int w, int val)
{
    have[w] = 1;
    if (WEAP[w].mag > 0) mag[w] = (val > 0) ? val - 1 : WEAP[w].mag;
    if (val == 0 && WEAP[w].atype >= 0) add_ammo(WEAP[w].atype, WGIVE[w]);
    select_weapon(w);
    grinTimer = 70;
    setmsg(WNAME[w]);
}

static int pickup(Item *it)
{
    int t = it->type, half = (it->val == 1);
    if (it->delay > 0) return 0;
    switch (t) {
    case IT_MEDKIT:
        if (health >= 100) return 0;
        health += 25; if (health > 100) health = 100;
        setmsg("MEDKIT"); return 1;
    case IT_VEST:
        if (armor >= 100) return 0;
        armor += 50; if (armor > 100) armor = 100;
        setmsg("VEST"); return 1;
    case IT_AMMO_PIS: case IT_AMMO_SMG: case IT_AMMO_CHAIN:
    case IT_AMMO_SHELL: case IT_AMMO_ROCK: {
        int k = t - IT_AMMO_PIS, n = AMMO_PICK[k];
        if (half) { n /= 2; if (n < 1) n = 1; }
        if (!add_ammo(k, n)) return 0;
        setmsg(AMMO_NAME[k]); return 1;
    }
    case IT_KEY_B: haveKey[0] = 1; setmsg("BLUE KEY"); return 1;
    case IT_KEY_Y: haveKey[1] = 1; setmsg("YELLOW KEY"); return 1;
    case IT_KEY_R: haveKey[2] = 1; setmsg("RED KEY"); return 1;
    default:
        if (t >= IT_W_KNIFE && t <= IT_W_ROCKET) {
            int w = t - IT_W_KNIFE;
            if (have[w]) {                         /* ya la tengo: solo municion */
                int n = (it->val > 0) ? it->val - 1 : WGIVE[w];
                if (WEAP[w].atype < 0 || n <= 0 || !add_ammo(WEAP[w].atype, n)) return 0;
                setmsg(AMMO_NAME[WEAP[w].atype]); return 1;
            }
            if (weapons_carried() < MAX_WEAPONS) { take_weapon(w, it->val); return 1; }
            if (msgTimer < 20) setmsg("FULL - PRESS L2 TO SWAP");
            return 0;
        }
    }
    return 0;
}

/* L2: cambia el arma actual por la que esta en el suelo; sin arma cerca, la suelta */
static void swap_or_drop(void)
{
    int i, near = -1;
    float bd = 1.0f;
    for (i = 0; i < nItems; i++) {
        float dx, dy, d;
        if (!items[i].active || items[i].type < IT_W_KNIFE || items[i].delay > 0) continue;
        if (have[items[i].type - IT_W_KNIFE]) continue;
        dx = items[i].x - px; dy = items[i].y - py; d = sqrtf(dx * dx + dy * dy);
        if (d < bd) { bd = d; near = i; }
    }
    if (near < 0 && weapons_carried() <= 1) { setmsg("LAST WEAPON"); return; }
    {
        float fx = px + cosf(pa) * 0.7f, fy = py + sinf(pa) * 0.7f;
        int old = weapon, keepVal = (WEAP[old].mag > 0) ? mag[old] + 1 : 0, k;
        int newType = -1, newVal = 0;
        if (is_wall(fx, fy)) { fx = px; fy = py; }
        if (near >= 0) { newType = items[near].type; newVal = items[near].val; items[near].active = 0; }
        k = spawn_item(fx, fy, IT_W_KNIFE + old, keepVal);
        if (k >= 0) items[k].delay = 90;      /* no se recoge de inmediato */
        have[old] = 0; mag[old] = 0;
        if (newType >= 0) take_weapon(newType - IT_W_KNIFE, newVal);
        else {
            int w;
            for (w = 0; w < WEAPON_COUNT; w++) if (have[w]) { select_weapon(w); break; }
            setmsg("DROPPED");
        }
    }
}

/* Se puede usar si la tenemos y queda municion (en el cargador o de reserva) */
static int weapon_usable(int w)
{
    if (!have[w]) return 0;
    if (WEAP[w].atype < 0) return 1;
    return mag[w] > 0 || ammo[WEAP[w].atype] > 0;
}

static void cycle_weapon(int dir)
{
    int i, w = weapon;
    for (i = 0; i < WEAPON_COUNT; i++) {
        w = (w + dir + WEAPON_COUNT) % WEAPON_COUNT;
        if (weapon_usable(w)) { select_weapon(w); return; }
    }
}

static void start_reload(void)
{
    const Weapon *w = &WEAP[weapon];
    if (reloading || w->mag <= 0) return;
    if (mag[weapon] >= w->mag) return;
    if (ammo[w->atype] <= 0) return;
    reloading = 1; rtime = 0;
}

static void finish_reload(void)
{
    const Weapon *w = &WEAP[weapon];
    int need = w->mag - mag[weapon];
    int take = ammo[w->atype] < need ? ammo[w->atype] : need;
    mag[weapon] += take;
    ammo[w->atype] -= take;
    reloading = 0; wcool = 6; wtime = 100;
}

static void damage_player(int dmg)
{
    int a = dmg / 2;
    if (a > armor) a = armor;
    armor -= a;
    health -= dmg - a;
    hurtFlash = 14;
    faceHurt = 24; faceDir = rand() & 1;
}

/* ------------------------------------------------------------------ */
/* Juego                                                               */
/* ------------------------------------------------------------------ */
static void start_level(int n, int keep)
{
    int x, y, i;
    const LevelDef *lv = &LEVELS[n];
    curLevel = n;
    nEnemies = 0; nItems = 0;
    memset(wallMap, 0, sizeof(wallMap));
    memset(doorOpen, 0, sizeof(doorOpen));
    memset(doorMoving, 0, sizeof(doorMoving));
    for (y = 0; y < MAPH; y++) {
        for (x = 0; x < MAPW; x++) {
            char c = lv->map[y][x];
            if (c >= '1' && c <= '4') wallMap[y][x] = (unsigned char)(c - '0');
            else if (c == 'd') wallMap[y][x] = 5;
            else if (c == 'b') wallMap[y][x] = 6;
            else if (c == 'y') wallMap[y][x] = 7;
            else if (c == 'r') wallMap[y][x] = 8;
            else if (c == 'X') wallMap[y][x] = 9;
            else if (c == 'P') { px = x + 0.5f; py = y + 0.5f; }
        }
    }
    for (i = 0; i < lv->nEnemies && nEnemies < MAX_ENEMIES; i++) {
        Enemy *e = &enemies[nEnemies++];
        memset(e, 0, sizeof(*e));
        e->x = e->homeX = lv->enemies[i][0] + 0.5f;
        e->y = e->homeY = lv->enemies[i][1] + 0.5f;
        e->face = e->homeFace = lv->enemies[i][2] * PI_F / 180.0f;
        e->type = lv->enemies[i][3];
        if (e->type == 3 || e->type == 4) e->gun = 1;          /* guardia azul y SS: metralleta */
        else if (e->type == 5) e->gun = 2;                      /* jefe: ametralladora y escopeta */
        else if (e->type == 2) e->gun = 0;                      /* explosivo: sin arma */
        else e->gun = (i % 3 == 1) ? 1 : 0;                     /* blancos y verdes: 1 de cada 3 */
        e->hp = (int)(EHP[e->type] * DIFF_HP[diff] + 0.5f); if (e->hp < 1) e->hp = 1;
        e->state = E_IDLE; e->frame = 0;
        e->timer = (i * 37) % 500;   /* para que no giren todos a la vez */
    }
    for (i = 0; i < lv->nItems; i++)
        spawn_item(lv->items[i][0] + 0.5f, lv->items[i][1] + 0.5f, lv->items[i][2], 0);

    pa = 0.0f; bob = 0.0f; stepAcc = 0.0f;
    memset(haveKey, 0, sizeof(haveKey));
    if (!keep) {
        health = 100; armor = 0; kills = 0;
        memset(have, 0, sizeof(have)); memset(ammo, 0, sizeof(ammo)); memset(mag, 0, sizeof(mag));
        have[0] = 1; have[1] = 1; mag[1] = WEAP[1].mag; ammo[0] = 28; weapon = 1;
    }
    reloading = 0; rtime = 0;
    wcool = 0; wtime = 100; shots = 0;
    hurtFlash = 0; faceHurt = 0; grinTimer = 0; gameState = G_PLAY;
    setmsg(n == 0 ? "FIND THE EXIT" : "LEVEL 2 - FIND THE BOSS");
    build_dmap();
    rebuild_home_maps();
}

static void reset_game(void) { start_level(0, 0); }

static int enemies_alive(void)
{
    int i, n = 0;
    for (i = 0; i < nEnemies; i++)
        if (enemies[i].state != E_DEAD) n++;
    return n;
}

static void hurt_enemy(Enemy *e, int dmg);

/* Explosion del enemigo kamikaze: daña al jugador y a los enemigos cercanos */
static void explode(Enemy *e)
{
    float dx = px - e->x, dy = py - e->y, d = sqrtf(dx * dx + dy * dy), vol;
    int i;
    vol = 1.0f - d / 22.0f; if (vol < 0.0f) vol = 0.0f;
    if (vol > 0.0f) play_sound(SND_BOOM, 0.4f + vol * 0.6f);
    if (d < 2.6f && los(e->x, e->y, px, py)) {
        int dmg = (int)((50.0f * (1.0f - d / 2.6f) + 8.0f) * DIFF_DMG[diff] + 0.5f);
        damage_player(dmg);
    }
    for (i = 0; i < nEnemies; i++) {
        Enemy *o = &enemies[i];
        float ex, ey;
        if (o == e || o->state == E_DEAD) continue;
        ex = o->x - e->x; ey = o->y - e->y;
        if (ex * ex + ey * ey < 2.0f * 2.0f) hurt_enemy(o, 6);
    }
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
        if (e->type == 2) explode(e);
        else if (e->type == 5) {
            spawn_item(e->x, e->y, IT_MEDKIT, 0);
            setmsg("BOSS DOWN - GET OUT");
        } else if (rand() % 3) spawn_item(e->x, e->y, (rand() % 4 == 0) ? IT_AMMO_SHELL : IT_AMMO_PIS, 1);
    } else if (e->type == 5) {
        /* el jefe no se aturde: solo se activa */
        if (e->state == E_IDLE || e->state == E_RETURN) { e->state = E_CHASE; e->timer = 0; }
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
        tol = (w->tol > 0.0f) ? w->tol : atanf((e->type == 5 ? 0.55f : 0.30f) / d);
        if (ang > tol) continue;
        if (!los(px, py, e->x, e->y)) continue;
        if (w->multi) {
            int dmg = w->dmg;
            if (w->falloff) {
                dmg = (int)(w->dmg * (1.0f - 0.7f * d / w->range) + 0.5f);
                if (dmg < 1) dmg = 1;
            }
            hurt_enemy(e, dmg);
        } else if (d < bestd) { bestd = d; best = i; }
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

/* Disparo de un enemigo: se oye segun la distancia (mas fuerte si hay linea de vista) */
static void enemy_shot_sound(const Enemy *e, int snd)
{
    float dx = e->x - px, dy = e->y - py;
    float d = sqrtf(dx * dx + dy * dy), vol;
    if (d > 20.0f) return;
    vol = 1.0f - d / 20.0f;
    if (!los(e->x, e->y, px, py)) vol *= 0.5f;
    play_sound(snd, 0.35f + vol * 0.65f);
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
        if (e->type == 2) {                         /* explosivo: corre hacia ti y estalla */
            if (see && d < 1.5f) { e->state = E_ATTACK; e->timer = 0; e->frame = EF_ATTACK; break; }
        } else if (see && d < (e->gun == 2 ? 10.0f : 8.0f) && e->cool == 0) {
            e->state = E_ATTACK; e->timer = 0; e->frame = EF_ATTACK; e->aux = 0;
            break;
        }
        if (d > 1.1f) {
            float tx, ty;
            if (see) { tx = px; ty = py; }
            else best_neighbor(e, dmap, &tx, &ty);
            enemy_step_toward(e, tx, ty, ESPEED[e->type]);
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
        if (e->type == 2) {                         /* se prepara (click, click) y explota */
            e->frame = EF_ATTACK + ((e->timer / 4) & 1);
            if (e->timer >= 14) hurt_enemy(e, 999);
            break;
        }
        if (e->gun == 2) {
            /* jefe: de cerca escopeta, de lejos ametralladora */
            if (e->timer == 1) e->aux = (d < 4.5f) ? 1 : 0;
            if (e->aux) {
                e->frame = 5;
                if (e->timer == 14) {
                    int chance = (int)(95.0f - d * 10.0f);
                    if (chance < 25) chance = 25;
                    enemy_shot_sound(e, SND_SHOTGUN);
                    if (los(e->x, e->y, px, py) && (rand() % 100) < chance)
                        damage_player((int)((20 + rand() % 12) * DIFF_DMG[diff] + 0.5f));
                }
                if (e->timer >= 34) { e->state = E_CHASE; e->timer = 0; e->cool = ECOOL[5] + rand() % 30; }
            } else {
                e->frame = 4 + ((e->timer / 3) & 3);
                if (e->timer >= 10 && e->timer < 42 && ((e->timer - 10) % 4) == 0) {
                    int chance = (int)(60.0f - d * 4.0f);
                    if (chance < 15) chance = 15;
                    enemy_shot_sound(e, SND_CHAIN);
                    if (los(e->x, e->y, px, py) && (rand() % 100) < chance)
                        damage_player((int)((4 + rand() % 3) * DIFF_DMG[diff] + 0.5f));
                }
                if (e->timer >= 46) { e->state = E_CHASE; e->timer = 0; e->cool = ECOOL[5] + rand() % 30; }
            }
            break;
        }
        if (e->gun) {
            /* metralleta: rafaga de 3 tiros, mas rapida y con menos dano por tiro */
            e->frame = EF_ATTACK + ((e->timer / 5) & 1 ? 2 : 1);
            if (e->timer == 12 || e->timer == 17 || e->timer == 22) {
                int chance = (int)(65.0f - d * 6.0f);
                if (chance < 12) chance = 12;
                enemy_shot_sound(e, SND_SMG);
                if (los(e->x, e->y, px, py) && (rand() % 100) < chance)
                    damage_player((int)((EDMG[e->type] * 0.55f + rand() % 3) * DIFF_DMG[diff] + 0.5f));
            }
            if (e->timer >= 30) {
                e->state = E_CHASE; e->timer = 0;
                e->cool = ECOOL[e->type] * 3 / 4 + rand() % 30;
            }
            break;
        }
        e->frame = EF_ATTACK + (e->timer / 8 > 2 ? 2 : e->timer / 8);
        if (e->timer == 16) {
            int chance = (int)(90.0f - d * 8.0f);
            if (chance < 20) chance = 20;
            enemy_shot_sound(e, SND_PISTOL);
            if (los(e->x, e->y, px, py) && (rand() % 100) < chance) damage_player((int)((EDMG[e->type] + rand() % 7) * DIFF_DMG[diff] + 0.5f));
        }
        if (e->timer >= 26) {
            e->state = E_CHASE; e->timer = 0;
            e->cool = ECOOL[e->type] + rand() % 40;
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

/* ------------------------------------------------------------------ */
/* Menus                                                               */
/* ------------------------------------------------------------------ */
static const u32 BIND_DEF[NACT] = { PAD_CROSS, PAD_R2, PAD_TRIANGLE, PAD_L1, PAD_R1, PAD_SQUARE, PAD_CIRCLE, PAD_L2 };
static const u32 BTN_LIST[8] = { PAD_CROSS, PAD_CIRCLE, PAD_SQUARE, PAD_TRIANGLE, PAD_L1, PAD_R1, PAD_L2, PAD_R2 };
static const char *BTN_NAME[8] = { "X", "CIRCULO", "CUADRADO", "TRIANGULO", "L1", "R1", "L2", "R2" };
static const char *ACT_NAME[NACT] = { "DISPARO", "DISPARO 2", "RECARGAR", "ARMA ANTERIOR", "ARMA SIGUIENTE",
                                      "LADO IZQUIERDO", "LADO DERECHO", "SOLTAR ARMA" };

static const char *btn_name(u32 m)
{
    int k;
    for (k = 0; k < 8; k++) if (BTN_LIST[k] == m) return BTN_NAME[k];
    return "?";
}

static void apply_volume(void)
{
#ifdef USE_SOUND
    if (audio_ok) audsrv_set_volume(volume * 10);
#endif
}

static int menu_count(void)
{
    switch (menuScreen) {
    case M_MAIN: return 4;
    case M_DIFF: return 4;
    case M_OPT:  return 3;
    case M_CTRL: return NACT + 2;
    default:     return 4;   /* pausa */
    }
}

static void menu_goto(int screen, int sel) { menuScreen = screen; menuSel = sel; }

static void menu_update(void)
{
    u32 pr = padPress;
    int n = menu_count(), ok = (pr & PAD_CROSS) != 0, back = (pr & PAD_CIRCLE) != 0;
    int dir = ((pr & PAD_RIGHT) ? 1 : 0) - ((pr & PAD_LEFT) ? 1 : 0);

    if (menuScreen == M_CTRL && rebind >= 0) {          /* esperando un boton nuevo */
        int k;
        if (pr & PAD_START) { rebind = -1; return; }
        for (k = 0; k < 8; k++) {
            if (pr & BTN_LIST[k]) {
                u32 nb = BTN_LIST[k];
                int a;
                for (a = 0; a < NACT; a++)
                    if (a != rebind && bind[a] == nb) { bind[a] = bind[rebind]; break; }   /* intercambia */
                bind[rebind] = nb; rebind = -1;
                return;
            }
        }
        return;
    }

    if (pr & PAD_UP)   menuSel = (menuSel + n - 1) % n;
    if (pr & PAD_DOWN) menuSel = (menuSel + 1) % n;

    switch (menuScreen) {
    case M_MAIN:
        if (ok) {
            if (menuSel == 0) menu_goto(M_DIFF, diff);
            else if (menuSel == 1) { menuBack = M_MAIN; menu_goto(M_OPT, 0); }
            else if (menuSel == 2) { menuBack = M_MAIN; menu_goto(M_CTRL, 0); }
            else Exit(0);
        }
        break;
    case M_DIFF:
        if (ok && menuSel < 3) { diff = menuSel; reset_game(); }
        else if ((ok && menuSel == 3) || back) menu_goto(M_MAIN, 0);
        break;
    case M_OPT:
        if (menuSel == 0 && dir) { sens += dir; if (sens < 1) sens = 1; if (sens > 10) sens = 10; }
        if (menuSel == 1 && dir) {
            volume += dir; if (volume < 0) volume = 0; if (volume > 10) volume = 10;
            apply_volume(); sfx_step(1.0f);
        }
        if ((ok && menuSel == 2) || back) menu_goto(menuBack, menuBack == M_MAIN ? 1 : 1);
        break;
    case M_CTRL:
        if (ok && menuSel < NACT) rebind = menuSel;
        else if (ok && menuSel == NACT) memcpy(bind, BIND_DEF, sizeof(bind));
        else if ((ok && menuSel == NACT + 1) || back) menu_goto(menuBack, 2);
        break;
    default: /* pausa */
        if ((ok && menuSel == 0) || back || (pr & PAD_START)) gameState = G_PLAY;
        else if (ok && menuSel == 1) { menuBack = M_PAUSE; menu_goto(M_OPT, 0); }
        else if (ok && menuSel == 2) { menuBack = M_PAUSE; menu_goto(M_CTRL, 0); }
        else if (ok && menuSel == 3) { gameState = G_MENU; menu_goto(M_MAIN, 0); }
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
            else if (t == 9) {
                if (curLevel + 1 < N_LEVELS) start_level(curLevel + 1, 1);
                else gameState = G_WIN;
                return;
            }
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

    if (gameState == G_MENU || gameState == G_PAUSE) { menu_update(); return; }
    if (gameState != G_PLAY) {
        if (in->start) { gameState = G_MENU; menuScreen = M_MAIN; menuSel = 0; }
        return;
    }
    if (in->start) { gameState = G_PAUSE; menuScreen = M_PAUSE; menuSel = 0; return; }

    /* giro y movimiento */
    pa += in->turn * 0.045f * (sens / 5.0f);
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
    if (in->reload) start_reload();
    if (in->swap) swap_or_drop();
    if (reloading) {
        rtime++;
        if (rtime >= WEAP[weapon].reload) finish_reload();
    } else if (wcool == 0) {
        const Weapon *w = &WEAP[weapon];
        if (w->mag > 0 && mag[weapon] == 0 && ammo[w->atype] > 0) {
            start_reload();                       /* cargador vacio: recarga sola */
        } else if (in->fire) {
            if (w->mag > 0 && mag[weapon] <= 0) {  /* sin balas en el cargador ni de reserva */
                setmsg("NO AMMO");
                cycle_weapon(-1);
                wcool = 15;
            } else {
                if (w->mag > 0) mag[weapon]--;
                player_shoot();
                if (weapon == 5) play_sound(SND_BOOM, 0.7f);
                if (weapon == 1) play_sound(SND_PISTOL, 0.9f);
                else if (weapon == 2) play_sound(SND_SHOTGUN, 1.0f);
                else if (weapon == 3) play_sound(SND_SMG, 0.8f);
                else if (weapon == 4) play_sound(SND_CHAIN, 0.8f);
                wcool = w->cd;
                wtime = 0;
                shots++;
            }
        }
    }

    /* items */
    for (i = 0; i < nItems; i++) {
        float dx, dy;
        if (!items[i].active) continue;
        if (items[i].delay > 0) items[i].delay--;
        dx = items[i].x - px; dy = items[i].y - py;
        if (dx * dx + dy * dy < 0.45f * 0.45f && pickup(&items[i])) items[i].active = 0;
    }

    /* enemigos */
    if (frameCount % 10 == 0) build_dmap();
    for (i = 0; i < nEnemies; i++) enemy_update(&enemies[i]);

    if (hurtFlash > 0) hurtFlash--;
    if (faceHurt > 0) faceHurt--;
    if (grinTimer > 0) grinTimer--;
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
        GSTEXTURE *tex;
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

        if (type >= 6 && type <= 8) open = doorOpen[my][mx];
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
        if (type == 5) {
            float p = doorOpen[my][mx];
            tex = &doortex[p < 0.25f ? 0 : (p < 0.5f ? 1 : (p < 0.75f ? 2 : 3))];
        } else tex = &walltex[type - 1];
        gsKit_prim_sprite_texture(gs, tex,
            (float)(c * COLW), y0, (float)texX, v0,
            (float)(c * COLW + COLW), y1, (float)texX + 1.0f, v1,
            1, rgba(sh, sh, sh, 0x80));
    }
}

/* Dibuja un sprite plano (billboard) con oclusion por columnas */
static void draw_billboard(GSTEXTURE *t, float u0, float v0, float tw, float th, float wx, float wy, float scale, float lift)
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
                xs, top, u0 + (xs - x0) / size * tw, v0,
                xe, bottom, u0 + (xe - x0) / size * tw, v0 + th,
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
            if (e->type == 5) {
                int bf;
                if (e->state == E_DEAD) bf = 8 + (e->timer / 8 > 3 ? 3 : e->timer / 8);
                else if (e->state == E_ATTACK) bf = (e->gun == 2 && e->aux) ? 5 : 4 + ((e->timer / 3) & 3);
                else if (e->state == E_CHASE || e->state == E_RETURN) bf = (e->timer / 8) & 3;
                else bf = 0;
                draw_billboard(&bosstex, (float)((bf % BOSS_COLS) * ENEMY_SIZE), (float)((bf / BOSS_COLS) * ENEMY_SIZE),
                               ENEMY_SIZE, ENEMY_SIZE, e->x, e->y, 1.3f, 0.0f);
            } else {
                int fi = enemy_frame_index(e);
                float sc = 1.0f;
                if (e->type == 2 && e->state == E_DEAD && e->frame <= EF_DEATH + 1) sc = 2.2f;   /* bola de fuego */
                draw_billboard(&etex[e->type], (float)((fi % ENEMY_COLS) * ENEMY_SIZE), (float)((fi / ENEMY_COLS) * ENEMY_SIZE),
                               ENEMY_SIZE, ENEMY_SIZE, e->x, e->y, sc, 0.0f);
            }
        } else {
            const Item *it = &items[list[i].idx];
            float lift = 0.0f;
            if (it->delay > 0 && (frameCount & 4)) continue;      /* parpadea al soltarla */
            if (it->type >= IT_W_KNIFE) {
                draw_billboard(&wtex[WBASE[it->type - IT_W_KNIFE] + 1], 0.0f, 0.0f, WEAPON_SIZE, WEAPON_SIZE,
                               it->x, it->y, 0.46f, 0.05f);
            } else {
                float sc = (it->val == 1 && it->type >= IT_AMMO_PIS && it->type <= IT_AMMO_ROCK) ? 0.26f : 0.38f;
                if (it->type >= IT_KEY_B) lift = 0.12f + 0.05f * sinf(frameCount * 0.08f);
                draw_billboard(&itemtex[it->type], 0.0f, 0.0f, ITEM_SIZE, ITEM_SIZE, it->x, it->y, sc, lift);
            }
        }
    }
}

static void draw_weapon(void)
{
    int frame = 1;
    float size = WEAPON_SIZE * (weapon == 2 ? 3.8f : 2.4f) * (SH / 448.0f);   /* la escopeta nueva se dibuja mas grande */
    float x, y, bx, by, sink = 0.0f;
    const Weapon *w = &WEAP[weapon];

    if (reloading) {
        float t = (float)rtime / (float)w->reload;
        if (weapon == 1 || weapon == 2) {
            /* animaciones propias: pistola y escopeta (cuadros 4..7) */
            static const float cut[2][3] = { { 0.15f, 0.45f, 0.75f }, { 0.25f, 0.55f, 0.75f } };
            const float *c = cut[weapon - 1];
            int k = (t < c[0]) ? 0 : (t < c[1]) ? 1 : (t < c[2]) ? 2 : 3;
            frame = 4 + k;
        } else {
            /* las demas armas bajan, se recargan fuera de pantalla y suben */
            float u = (t < 0.25f) ? t / 0.25f : (t > 0.75f ? (1.0f - t) / 0.25f : 1.0f);
            frame = 1;
            sink = u * size * 0.95f;
        }
    } else if (w->cd <= 8) {
        if (wtime < w->cd) frame = (shots & 1) ? 2 : 3;
    } else {
        if (wtime < 5) frame = 2; else if (wtime < 11) frame = 3;
    }
    bx = sinf(bob) * 8.0f;
    by = fabsf(cosf(bob)) * 8.0f;
    if (reloading) { bx = 0.0f; by = 0.0f; }
    x = SW / 2.0f - size / 2.0f + bx;
    y = SH - size + 10.0f + by + sink;
    gsKit_prim_sprite_texture(gs, &wtex[WBASE[weapon] + frame],
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
    case '<': return "   # " "  #  " " #   " "  #  " "   # ";
    case '>': return " #   " "  #  " "   # " "  #  " " #   ";
    case ',': return "     " "     " "     " "  #  " " #   ";
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
    int i, alive = enemies_alive(), at = WEAP[weapon].atype;
    float m = 24.0f;

    /* cara del jugador segun la vida (como en Wolfenstein) */
    {
        int lvl = (100 - health) / 15, f, col;
        float fs = 2.4f * FACE_SIZE, fx = m, fy = SH - m - 36.0f - 8.0f - fs;
        if (lvl < 0) lvl = 0;
        if (lvl > 6) lvl = 6;
        if (gameState == G_DEAD) f = 36;
        else if (faceHurt > 0) { col = 3 + faceDir; f = lvl * 5 + col; if (lvl == 6) f = lvl * 5 + (faceDir ? 1 : 0); }
        else if (grinTimer > 0) f = 35;
        else { static const int cyc[4] = { 1, 0, 1, 2 }; f = lvl * 5 + cyc[(frameCount / 50) & 3]; }
        rect(fx - 3, fy - 3, fx + fs + 3, fy + fs + 3, rgba(10, 10, 10, 0x60));
        gsKit_prim_sprite_texture(gs, &facetex, fx, fy, (float)((f % FACE_COLS) * FACE_SIZE), (float)((f / FACE_COLS) * FACE_SIZE),
                                  fx + fs, fy + fs, (float)((f % FACE_COLS) * FACE_SIZE + FACE_SIZE), (float)((f / FACE_COLS) * FACE_SIZE + FACE_SIZE),
                                  1, rgba(0x80, 0x80, 0x80, 0x80));
    }
    stat_row("HP", health, 100, SH - m - 36.0f, 210, 20, 20);
    stat_row("AR", armor, 100, SH - m - 14.0f, 50, 120, 230);

    /* municion del arma actual: cargador grande y reserva chica */
    if (at >= 0) {
        char buf[16];
        float wd;
        int low = (mag[weapon] <= WEAP[weapon].mag / 4);
        u64 col = low ? rgba(255, 90, 60, 0x80) : rgba(255, 230, 80, 0x80);
        snprintf(buf, sizeof(buf), "%d", mag[weapon]);
        wd = text_width(buf, 4.0f);
        draw_text_shadow(buf, SW - m - 78.0f - wd, SH - m - 22.0f, 4.0f, col);
        snprintf(buf, sizeof(buf), "/ %d", ammo[at]);
        draw_text_shadow(buf, SW - m - text_width(buf, 3.0f), SH - m - 20.0f, 3.0f, rgba(220, 220, 220, 0x80));
        draw_text_shadow(AMMO_NAME[at], SW - m - text_width(AMMO_NAME[at], 2.0f), SH - m - 38.0f, 2.0f, rgba(220, 220, 220, 0x80));
        if (reloading) {
            draw_text_shadow("RELOADING", SW - m - text_width("RELOADING", 2.0f), SH - m - 54.0f, 2.0f, rgba(120, 220, 255, 0x80));
        } else if (mag[weapon] == 0 && ammo[at] == 0) {
            draw_text_shadow("NO AMMO", SW - m - text_width("NO AMMO", 2.0f), SH - m - 54.0f, 2.0f, rgba(255, 90, 60, 0x80));
        } else if (low && ammo[at] > 0) {
            draw_text_shadow("RELOAD", SW - m - text_width("RELOAD", 2.0f), SH - m - 54.0f, 2.0f, rgba(255, 200, 60, 0x80));
        }
    }

    /* armas que llevamos (maximo 3) */
    {
        int k = 0;
        for (i = 0; i < WEAPON_COUNT; i++) {
            u64 c;
            if (!have[i]) continue;
            c = (i == weapon) ? rgba(255, 220, 40, 0x80) : rgba(190, 190, 190, 0x80);
            draw_text_shadow(WNAME[i], m + (i == weapon ? 10.0f : 0.0f), m + k * 15.0f, 2.0f, c);
            k++;
        }
    }
    /* llaves */
    for (i = 0; i < 3; i++) {
        float x = m + i * 28.0f, y = m + 52.0f;
        if (haveKey[i]) {
            gsKit_prim_sprite_texture(gs, &itemtex[IT_KEY_B + i], x, y, 0.0f, 0.0f, x + 24, y + 24,
                                      (float)ITEM_SIZE, (float)ITEM_SIZE, 1, rgba(0x80, 0x80, 0x80, 0x80));
        } else {
            rect(x + 2, y + 9, x + 22, y + 15, rgba(60, 60, 60, 0x50));
        }
    }
    /* enemigos que quedan */
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "EN %d", alive);
        draw_text_shadow(buf, SW - m - text_width(buf, 2.0f), m, 2.0f, rgba(255, 120, 120, 0x80));
        snprintf(buf, sizeof(buf), "LEVEL %d", curLevel + 1);
        draw_text_shadow(buf, SW - m - text_width(buf, 2.0f), m + 18.0f, 2.0f, rgba(220, 220, 220, 0x80));
    }

    /* mira */
    rect(SW / 2.0f - 1, SH / 2.0f - 9, SW / 2.0f + 1, SH / 2.0f - 3, rgba(255, 255, 255, 0x60));
    rect(SW / 2.0f - 1, SH / 2.0f + 3, SW / 2.0f + 1, SH / 2.0f + 9, rgba(255, 255, 255, 0x60));
    rect(SW / 2.0f - 9, SH / 2.0f - 1, SW / 2.0f - 3, SH / 2.0f + 1, rgba(255, 255, 255, 0x60));
    rect(SW / 2.0f + 3, SH / 2.0f - 1, SW / 2.0f + 9, SH / 2.0f + 1, rgba(255, 255, 255, 0x60));

    /* mensaje */
    if (msgTimer > 0 && msgText[0] && gameState != G_PAUSE) {
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

static void draw_centered(const char *t, float y, float sc, int sel)
{
    u64 c = sel ? rgba(255, 220, 40, 0x80) : rgba(205, 205, 205, 0x80);
    float x = (SW - text_width(t, sc)) / 2.0f;
    draw_text_shadow(t, x, y, sc, c);
    if (sel) {
        draw_text_shadow(">", x - 8.0f * sc, y, sc, c);
        draw_text_shadow("<", x + text_width(t, sc) + 3.0f * sc, y, sc, c);
    }
}

static void draw_slider(float x, float y, int v, int maxv)
{
    int i;
    for (i = 0; i < maxv; i++) {
        float bx = x + i * 15.0f;
        rect(bx, y, bx + 12, y + 15, i < v ? rgba(255, 220, 40, 0x80) : rgba(70, 70, 70, 0x80));
    }
    draw_number(v, x + maxv * 15.0f + 10.0f, y, 3.0f, rgba(255, 255, 255, 0x80));
}

static void menu_draw(void)
{
    static const char *const mainItems[4]  = { "INICIAR JUEGO", "OPCIONES", "CONTROLES", "SALIR" };
    static const char *const diffItems[4]  = { "FACIL", "MEDIO", "DIFICIL", "VOLVER" };
    static const char *const pauseItems[4] = { "CONTINUAR", "OPCIONES", "CONTROLES", "MENU PRINCIPAL" };
    const char *hint = "X ACEPTAR    O VOLVER";
    float H = (float)SH, W = (float)SW;
    int i;

    if (gameState == G_MENU) {
        gsKit_prim_sprite_texture(gs, &titletex, 0.0f, 0.0f, 0.0f, 0.0f, W, H, 512.0f, 352.0f,
                                  1, rgba(0x80, 0x80, 0x80, 0x80));
    } else {
        rect(0, 0, W, H, rgba(0, 0, 0, 0x78));
    }

    switch (menuScreen) {
    case M_MAIN: case M_PAUSE: {
        const char *const *it = (menuScreen == M_MAIN) ? mainItems : pauseItems;
        float y0 = H * 0.54f;
        if (menuScreen == M_PAUSE) { y0 = H * 0.30f; draw_centered("PAUSA", H * 0.16f, 5.0f, 0); }
        rect(W * 0.25f, y0 - 10, W * 0.75f, y0 + 4 * 34.0f + 2, rgba(0, 0, 0, 0x55));
        for (i = 0; i < 4; i++) draw_centered(it[i], y0 + i * 34.0f, 3.0f, i == menuSel);
        break;
    }
    case M_DIFF: {
        float y0 = H * 0.58f;
        rect(W * 0.25f, H * 0.49f, W * 0.75f, y0 + 4 * 32.0f, rgba(0, 0, 0, 0x55));
        draw_centered("DIFICULTAD", H * 0.51f, 2.0f, 0);
        for (i = 0; i < 4; i++) draw_centered(diffItems[i], y0 + i * 32.0f, 3.0f, i == menuSel);
        break;
    }
    case M_OPT: {
        float y0 = H * 0.58f, xl = W * 0.14f, xv = W * 0.50f;
        rect(W * 0.10f, H * 0.49f, W * 0.90f, y0 + 3 * 36.0f, rgba(0, 0, 0, 0x55));
        draw_centered("OPCIONES", H * 0.51f, 2.0f, 0);
        draw_text_shadow("SENSIBILIDAD", xl, y0, 3.0f, menuSel == 0 ? rgba(255, 220, 40, 0x80) : rgba(205, 205, 205, 0x80));
        draw_slider(xv + 40.0f, y0, sens, 10);
        draw_text_shadow("SONIDO", xl, y0 + 36.0f, 3.0f, menuSel == 1 ? rgba(255, 220, 40, 0x80) : rgba(205, 205, 205, 0x80));
        draw_slider(xv + 40.0f, y0 + 36.0f, volume, 10);
        draw_centered("VOLVER", y0 + 72.0f, 3.0f, menuSel == 2);
        hint = "ARRIBA ABAJO ELEGIR    IZQ DER CAMBIAR";
        break;
    }
    case M_CTRL: {
        float y0 = H * 0.525f, rh = 18.0f, xl = W * 0.14f, xv = W * 0.52f;
        rect(W * 0.10f, H * 0.445f, W * 0.90f, y0 + (NACT + 2) * rh + 4, rgba(0, 0, 0, 0x60));
        draw_centered("CONTROLES", H * 0.46f, 2.0f, 0);
        for (i = 0; i < NACT; i++) {
            u64 c = (i == menuSel) ? rgba(255, 220, 40, 0x80) : rgba(205, 205, 205, 0x80);
            draw_text_shadow(ACT_NAME[i], xl, y0 + i * rh, 2.0f, c);
            if (rebind == i) {
                if ((frameCount / 15) & 1) draw_text_shadow("PRESIONA UN BOTON", xv, y0 + i * rh, 2.0f, rgba(120, 220, 255, 0x80));
            } else {
                draw_text_shadow(btn_name(bind[i]), xv, y0 + i * rh, 2.0f, c);
            }
        }
        draw_centered("RESTAURAR", y0 + NACT * rh, 2.0f, menuSel == NACT);
        draw_centered("VOLVER", y0 + (NACT + 1) * rh, 2.0f, menuSel == NACT + 1);
        hint = (rebind >= 0) ? "START CANCELA" : "X CAMBIAR BOTON    O VOLVER";
        break;
    }
    }
    draw_text_shadow(hint, (W - text_width(hint, 2.0f)) / 2.0f, H - 22.0f, 2.0f, rgba(200, 200, 200, 0x80));
}

static void game_render(void)
{
    gsKit_clear(gs, rgba(0, 0, 0, 0x80));
    if (gameState == G_MENU) { menu_draw(); return; }
    draw_world();
    draw_sprites();
    draw_weapon();
    draw_hud();
    draw_overlay();
    if (gameState == G_PAUSE) menu_draw();
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

/* Texturas con paleta de 256 colores (PSM T8): 4 veces menos VRAM que RGBA32.
   Las usamos para armas y enemigos, que son muchas. */
static void load_tex8(const char *b64, int nframes, int tw, int th, GSTEXTURE *out)
{
    int blen = (int)(strlen(b64) / 4 * 3 + 8);
    int total = nframes * tw * th;
    unsigned char *raw = (unsigned char *)malloc(blen);
    unsigned char *idx = (unsigned char *)memalign(128, total);
    u32 *clut = (u32 *)memalign(128, 256 * 4);
    unsigned char *pal;
    int n, pos = 0, f, p, i;
    u32 vclut;

    n = b64_decode(b64, raw);
    pal = raw;
    for (p = 768; p + 1 < n && pos < total; p += 2) {
        int run = raw[p], val = raw[p + 1];
        while (run-- > 0 && pos < total) idx[pos++] = (unsigned char)val;
    }
    while (pos < total) idx[pos++] = 0;

    /* paleta CT32; el GS guarda las entradas con los bits 3 y 4 del indice intercambiados */
    for (i = 0; i < 256; i++) {
        int j = (i & 0xE7) | ((i & 0x08) << 1) | ((i & 0x10) >> 1);
        if (i == 0) clut[j] = 0;
        else clut[j] = (u32)pal[i * 3] | ((u32)pal[i * 3 + 1] << 8) |
                       ((u32)pal[i * 3 + 2] << 16) | (0x80u << 24);
    }
    FlushCache(0);

    vclut = gsKit_vram_alloc(gs, gsKit_texture_size(16, 16, GS_PSM_CT32), GSKIT_ALLOC_USERBUFFER);
    for (f = 0; f < nframes; f++) {
        memset(&out[f], 0, sizeof(GSTEXTURE));
        out[f].Width = tw;
        out[f].Height = th;
        out[f].PSM = GS_PSM_T8;
        out[f].ClutPSM = GS_PSM_CT32;
        out[f].Mem = (void *)(idx + f * tw * th);
        out[f].Clut = clut;
        out[f].VramClut = vclut;
        out[f].Filter = GS_FILTER_NEAREST;
        out[f].Vram = gsKit_vram_alloc(gs, gsKit_texture_size(tw, th, GS_PSM_T8), GSKIT_ALLOC_USERBUFFER);
        gsKit_texture_upload(gs, &out[f]);
    }
    free(raw);
}

/* Atlas T8: nframes de fw x fh en una sola textura de 'cols' columnas (un solo bloque de VRAM) */
static void load_atlas8(const char *b64, int nframes, int fw, int fh, int cols, GSTEXTURE *out)
{
    int blen = (int)(strlen(b64) / 4 * 3 + 8);
    int total = nframes * fw * fh;
    int rows = (nframes + cols - 1) / cols, aw = cols * fw, ah = rows * fh;
    unsigned char *raw = (unsigned char *)malloc(blen);
    unsigned char *seq = (unsigned char *)malloc(total);
    unsigned char *idx = (unsigned char *)memalign(128, aw * ah);
    u32 *clut = (u32 *)memalign(128, 256 * 4);
    unsigned char *pal;
    int n, pos = 0, f, p, i, y;

    n = b64_decode(b64, raw);
    pal = raw;
    for (p = 768; p + 1 < n && pos < total; p += 2) {
        int run = raw[p], val = raw[p + 1];
        while (run-- > 0 && pos < total) seq[pos++] = (unsigned char)val;
    }
    while (pos < total) seq[pos++] = 0;
    memset(idx, 0, aw * ah);
    for (f = 0; f < nframes; f++)
        for (y = 0; y < fh; y++)
            memcpy(idx + (size_t)((f / cols) * fh + y) * aw + (f % cols) * fw, seq + (size_t)(f * fh + y) * fw, fw);

    for (i = 0; i < 256; i++) {
        int j = (i & 0xE7) | ((i & 0x08) << 1) | ((i & 0x10) >> 1);
        if (i == 0) clut[j] = 0;
        else clut[j] = (u32)pal[i * 3] | ((u32)pal[i * 3 + 1] << 8) |
                       ((u32)pal[i * 3 + 2] << 16) | (0x80u << 24);
    }
    FlushCache(0);

    memset(out, 0, sizeof(GSTEXTURE));
    out->Width = aw; out->Height = ah;
    out->PSM = GS_PSM_T8; out->ClutPSM = GS_PSM_CT32;
    out->Mem = (void *)idx; out->Clut = clut;
    out->VramClut = gsKit_vram_alloc(gs, gsKit_texture_size(16, 16, GS_PSM_CT32), GSKIT_ALLOC_USERBUFFER);
    out->Filter = GS_FILTER_NEAREST;
    out->Vram = gsKit_vram_alloc(gs, gsKit_texture_size(aw, ah, GS_PSM_T8), GSKIT_ALLOC_USERBUFFER);
    gsKit_texture_upload(gs, out);
    free(seq);
    free(raw);
}

static void load_sheet8(const char *b64, int nframes, int size, GSTEXTURE *out)
{
    load_tex8(b64, nframes, size, size, out);
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
    u32 p, pm, edge;
    static u32 last = 0, lastm = 0;
    static int rep = 0;
    int st;
    const u32 dirs = PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT;

    memset(in, 0, sizeof(*in));
    padPress = 0;
    if (!padOk) return;
    st = padGetState(0, 0);
    if (st != PAD_STATE_STABLE && st != PAD_STATE_FINDCTP1) return;
    if (padRead(0, 0, &b) == 0) return;
    p = 0xffff ^ b.btns;
    pm = p;

    if ((b.mode >> 4) == 7) { /* DualShock: sticks analogicos activos */
        in->fwd    = -stick(b.ljoy_v);
        in->strafe =  stick(b.ljoy_h);
        in->turn   =  stick(b.rjoy_h);
        if (b.ljoy_v < 60)  pm |= PAD_UP;
        if (b.ljoy_v > 196) pm |= PAD_DOWN;
        if (b.ljoy_h < 60)  pm |= PAD_LEFT;
        if (b.ljoy_h > 196) pm |= PAD_RIGHT;
    }
    if (p & PAD_UP)     in->fwd += 1.0f;
    if (p & PAD_DOWN)   in->fwd -= 1.0f;
    if (p & PAD_LEFT)   in->turn -= 1.0f;
    if (p & PAD_RIGHT)  in->turn += 1.0f;
    if (p & bind[A_LEFT])  in->strafe -= 1.0f;
    if (p & bind[A_RIGHT]) in->strafe += 1.0f;
    if (p & (bind[A_FIRE] | bind[A_FIRE2])) in->fire = 1;

    edge = p & ~last;
    in->reload = (edge & bind[A_RELOAD]) != 0;
    in->prev   = (edge & bind[A_PREV]) != 0;
    in->next   = (edge & bind[A_NEXT]) != 0;
    in->swap   = (edge & bind[A_SWAP]) != 0;
    in->start  = (edge & PAD_START) != 0;

    /* botones para los menus (con repeticion al mantener) */
    padPress = pm & ~lastm;
    if (pm & dirs) { rep++; if (rep > 18 && (rep & 3) == 0) padPress |= pm & dirs; } else rep = 0;
    last = p; lastm = pm;

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
    SND_DATA[SND_STEP] = snd_step; SND_LEN[SND_STEP] = snd_step_len;
    SND_DATA[SND_PISTOL] = snd_pistol; SND_LEN[SND_PISTOL] = snd_pistol_len;
    SND_DATA[SND_SHOTGUN] = snd_shotgun; SND_LEN[SND_SHOTGUN] = snd_shotgun_len;
    SND_DATA[SND_SMG] = snd_smg; SND_LEN[SND_SMG] = snd_smg_len;
    SND_DATA[SND_CHAIN] = snd_chain; SND_LEN[SND_CHAIN] = snd_chain_len;
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

    {
        static const char *const wsheet[WEAPON_COUNT] = { w0_b64, w1_b64, w2_b64, w3_b64, w4_b64, w5_b64 };
        int i;
        for (i = 0; i < WEAPON_COUNT; i++) load_sheet8(wsheet[i], WNF[i], WEAPON_SIZE, &wtex[WBASE[i]]);
    }
    load_atlas8(enemy_b64,  ENEMY_FRAMES, ENEMY_SIZE, ENEMY_SIZE, ENEMY_COLS, &etex[0]);
    load_atlas8(enemy2_b64, ENEMY_FRAMES, ENEMY_SIZE, ENEMY_SIZE, ENEMY_COLS, &etex[1]);
    load_atlas8(enemy3_b64, ENEMY_FRAMES, ENEMY_SIZE, ENEMY_SIZE, ENEMY_COLS, &etex[2]);
    load_atlas8(enemy4_b64, ENEMY_FRAMES, ENEMY_SIZE, ENEMY_SIZE, ENEMY_COLS, &etex[3]);
    load_atlas8(enemy5_b64, ENEMY_FRAMES, ENEMY_SIZE, ENEMY_SIZE, ENEMY_COLS, &etex[4]);
    load_atlas8(boss_b64, BOSS_FRAMES, ENEMY_SIZE, ENEMY_SIZE, BOSS_COLS, &bosstex);
    load_atlas8(faces_b64, FACE_FRAMES, FACE_SIZE, FACE_SIZE, FACE_COLS, &facetex);
    load_sheet(walls_b64, WALL_TEXTURES, WALL_SIZE, walltex);
    load_sheet(items_b64, ITEM_TEXTURES, ITEM_SIZE, itemtex);
    load_sheet(doors_b64, DOOR_FRAMES, WALL_SIZE, doortex);
    load_tex8(title_b64, 1, TITLE_W, TITLE_H, &titletex);
    titletex.Filter = GS_FILTER_LINEAR;

    memcpy(bind, BIND_DEF, sizeof(bind));
    boom_init();
    srand(12345);
    reset_game();
    gameState = G_MENU; menuScreen = M_MAIN; menuSel = 0;

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
