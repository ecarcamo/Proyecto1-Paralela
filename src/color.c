/* ===========================================================================
 *  color.c - HSV a RGB y asignacion de color por semilla.
 *  Proyecto 1 - Computacion Paralela y Distribuida (UVG)
 * =========================================================================== */
#include "color.h"
#include "rng.h"

#include <math.h>

/* Conversion HSV -> RGB por sectores (Travis, 1991).
 * El circulo de tono se divide en 6 sectores de 60 grados; dentro de cada uno
 * dos canales son constantes y el tercero interpola linealmente. */
void color_hsv_to_rgb(float h, float s, float v, float *r, float *g, float *b)
{
    if (s <= 0.0f) { *r = *g = *b = v; return; }   /* gris */

    h = h - floorf(h);                 /* envolver a [0,1) */
    float sector = h * 6.0f;
    int   i      = (int)sector;
    float f      = sector - (float)i;  /* posicion dentro del sector */

    float p = v * (1.0f - s);
    float q = v * (1.0f - s * f);
    float t = v * (1.0f - s * (1.0f - f));

    switch (i % 6) {
        case 0: *r = v; *g = t; *b = p; break;
        case 1: *r = q; *g = v; *b = p; break;
        case 2: *r = p; *g = v; *b = t; break;
        case 3: *r = p; *g = q; *b = v; break;
        case 4: *r = t; *g = p; *b = v; break;
        default:*r = v; *g = p; *b = q; break;
    }
}

uint32_t color_for_seed(uint32_t index, uint64_t seed)
{
    /* Tres canales independientes del mismo indice. Si se usara el mismo hash
     * para los tres, tono/saturacion/brillo quedarian correlacionados y se
     * verian bandas. */
    float h = rng_f01_ch(index, seed, 0);              /* tono: todo el circulo */
    float s = 0.55f + 0.40f * rng_f01_ch(index, seed, 1);
    float v = 0.72f + 0.28f * rng_f01_ch(index, seed, 2);

    float r, g, b;
    color_hsv_to_rgb(h, s, v, &r, &g, &b);
    return rgb_packf(r, g, b);
}
