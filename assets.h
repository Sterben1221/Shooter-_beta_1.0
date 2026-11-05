#ifndef ASSETS_H
#define ASSETS_H

/* Sprites comprimidos (base64 + RLE + paleta). Ver tools/make_assets.py */
extern const char weapons_b64[]; /* 6 armas x 3 cuadros (reposo, disparo, retroceso), 128x128 */
extern const char enemy_b64[];   /* 15 cuadros de 64x64 */

#define WEAPON_COUNT   6
#define WEAPON_FRAMES  3
#define WEAPON_SIZE    128
#define ENEMY_FRAMES   15
#define ENEMY_SIZE     64

/* Cuadros del enemigo */
#define EF_STAND   0
#define EF_WALK    1   /* 1..4  */
#define EF_PAIN    5   /* 5..6  */
#define EF_DEATH   7   /* 7..11 */
#define EF_ATTACK  12  /* 12..14 */

#endif
