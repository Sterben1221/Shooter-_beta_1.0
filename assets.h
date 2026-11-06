#ifndef ASSETS_H
#define ASSETS_H

/* Sprites comprimidos (base64 + RLE + paleta). Ver tools/make_assets.py */
extern const char weapons_b64[]; /* 6 armas x 3 cuadros (reposo, disparo, retroceso), 128x128 */
extern const char enemy_b64[];   /* 50 cuadros de 64x64 */
extern const char walls_b64[];   /* 9 texturas de pared de 64x64 */
extern const char items_b64[];   /* 8 iconos de 32x32 */

#define WEAPON_COUNT   6
#define WEAPON_FRAMES  3
#define WEAPON_SIZE    128
#define ENEMY_FRAMES   50
#define ENEMY_SIZE     64
#define WALL_TEXTURES  9
#define WALL_SIZE      64
#define ITEM_TEXTURES  8
#define ITEM_SIZE      32

/* Enemigo: filas 0..4 (parado + 4 pasos) x 8 angulos = cuadros 0..39 (fila*8+angulo) */
#define EF_HURT    40  /* 40..41 */
#define EF_DEATH   42  /* 42..46 */
#define EF_ATTACK  47  /* 47..49 */

#endif
