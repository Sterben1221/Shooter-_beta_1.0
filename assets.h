#ifndef ASSETS_H
#define ASSETS_H

/* Sprites comprimidos (base64 + RLE + paleta). Ver tools/make_assets.py */
extern const char w0_b64[]; /* cuchillo        4 cuadros */
extern const char w1_b64[]; /* pistola         8 cuadros (+4 de recarga) */
extern const char w2_b64[]; /* escopeta        8 cuadros (+4 de recarga) */
extern const char w3_b64[]; /* metralleta      4 cuadros */
extern const char w4_b64[]; /* ametralladora   4 cuadros */
extern const char w5_b64[]; /* lanzacohetes    4 cuadros */
extern const char enemy_b64[];   /* soldado blanco, 50 cuadros de 64x64 */
extern const char enemy2_b64[];  /* soldado verde,  50 cuadros de 64x64 */
extern const char walls_b64[];   /* 9 texturas de pared de 64x64 */
extern const char items_b64[];   /* 10 iconos de 64x64 */
extern const char doors_b64[];   /* puerta: 4 cuadros de 64x64 */
extern const char title_b64[];   /* pantalla de titulo 512x352 */

#define WEAPON_COUNT   6
#define WEAPON_SIZE    128
#define WEAPON_TOTAL   32   /* suma de cuadros de todas las armas */
#define ENEMY_FRAMES   50
#define ENEMY_SIZE     64
#define ENEMY_TYPES    2
#define WALL_TEXTURES  9
#define WALL_SIZE      64
#define ITEM_TEXTURES  10
#define ITEM_SIZE      64
#define DOOR_FRAMES    4
#define TITLE_W        512
#define TITLE_H        352

/* Cuadros de cada arma: 0 bajada, 1 reposo, 2 disparo, 3 retroceso;
   pistola y escopeta: 4..7 = recarga */

/* Enemigo: filas 0..4 (parado + 4 pasos) x 8 angulos = cuadros 0..39 (fila*8+angulo) */
#define EF_HURT    40  /* 40..41 */
#define EF_DEATH   42  /* 42..46 */
#define EF_ATTACK  47  /* 47..49 */

#endif
