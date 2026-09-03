/* color.h - Color por semilla y empaquetado ARGB8888 (el formato de SDL). */
#ifndef COLOR_H
#define COLOR_H

#include <stdint.h>

/* Empaqueta tres canales de 8 bits en un pixel ARGB8888 opaco. */
static inline uint32_t rgb_pack(uint8_t r, uint8_t g, uint8_t b)
{
    return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

/* Igual desde flotantes en [0,1]; el +0.5f es redondeo, sin el todo oscurece. */
static inline uint32_t rgb_packf(float r, float g, float b)
{
    int ri = (int)(r * 255.0f + 0.5f);
    int gi = (int)(g * 255.0f + 0.5f);
    int bi = (int)(b * 255.0f + 0.5f);
    ri = ri < 0 ? 0 : (ri > 255 ? 255 : ri);
    gi = gi < 0 ? 0 : (gi > 255 ? 255 : gi);
    bi = bi < 0 ? 0 : (bi > 255 ? 255 : bi);
    return rgb_pack((uint8_t)ri, (uint8_t)gi, (uint8_t)bi);
}

/* Desempaqueta a flotantes en [0,1]. */
static inline void rgb_unpackf(uint32_t argb, float *r, float *g, float *b)
{
    *r = (float)((argb >> 16) & 0xFFu) * (1.0f / 255.0f);
    *g = (float)((argb >>  8) & 0xFFu) * (1.0f / 255.0f);
    *b = (float)( argb        & 0xFFu) * (1.0f / 255.0f);
}

/* Multiplica un color por un escalar (iluminacion difusa). */
static inline uint32_t rgb_mul(uint32_t argb, float k)
{
    float r, g, b;
    rgb_unpackf(argb, &r, &g, &b);
    return rgb_packf(r * k, g * k, b * k);
}

/* Interpola entre dos colores: t=0 da 'a', t=1 da 'b'. */
static inline uint32_t rgb_lerp(uint32_t a, uint32_t b, float t)
{
    if (t <= 0.0f) return a;
    if (t >= 1.0f) return b;

    float ar, ag, ab, br, bg, bb;
    rgb_unpackf(a, &ar, &ag, &ab);
    rgb_unpackf(b, &br, &bg, &bb);
    return rgb_packf(ar + (br - ar) * t, ag + (bg - ag) * t, ab + (bb - ab) * t);
}

/* Conversion HSV -> RGB. h, s, v en [0,1]; salida en [0,1]. */
void color_hsv_to_rgb(float h, float s, float v, float *r, float *g, float *b);

/* Color de la semilla i: puro, asi el paralelo da los mismos colores. Va en
 * HSV y no en RGB al azar porque r,g,b sueltos dan colores lodosos. */
uint32_t color_for_seed(uint32_t index, uint64_t seed);

/* Deriva de color: speed = vueltas de tono por segundo (0 la congela) y
 * spread = cuanto varia ese ritmo entre semillas (0 = todas en bloque). */
typedef struct {
    float speed;
    float spread;
} ColorAnim;

/* El color de la semilla i en el instante t; anim NULL = color_for_seed().
 * 't' es double porque speed*t crece sin tope y en float el tono saltaria. */
uint32_t color_for_seed_at(uint32_t index, uint64_t seed,
                           const ColorAnim *anim, double t);

#endif /* COLOR_H */
