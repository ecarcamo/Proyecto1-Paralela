/* color.c - HSV a RGB y color animado por semilla. */
#include "color.h"
#include "rng.h"

#include <math.h>
#include <stddef.h>

/* HSV -> RGB por sectores (Travis, 1991): 6 sectores de 60 grados. */
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

/* Pulso de saturacion: solo SUMA (un pulso centrado lavaria el color). */
#define COLOR_SAT_PULSE  0.55f   /* cuanto sube en la cresta, 0..1    */
#define COLOR_SAT_RATIO  0.37f   /* ritmo del pulso respecto del tono */

#define COLOR_TAU  6.28318530717958647692f

uint32_t color_for_seed_at(uint32_t index, uint64_t seed,
                           const ColorAnim *anim, double t)
{
    /* Un canal de hash por atributo: con uno solo se verian bandas. */
    float h = rng_f01_ch(index, seed, 0);              /* tono: todo el circulo */
    float s = 0.55f + 0.40f * rng_f01_ch(index, seed, 1);
    float v = 0.72f + 0.28f * rng_f01_ch(index, seed, 2);

    if (anim != NULL && anim->speed != 0.0f) {
        float spread = anim->spread;
        if (spread < 0.0f) spread = 0.0f;
        if (spread > 1.0f) spread = 1.0f;

        /* Ritmo propio de cada semilla: sin el jitter giran todas en formacion. */
        float jitter = 2.0f * rng_f01_ch(index, seed, 3) - 1.0f;
        double rate  = (double)anim->speed * (1.0 + (double)spread * jitter);

        /* Envolvimiento en double y recien ahi se baja a float (ver color.h). */
        double hd = (double)h + rate * t;
        hd -= floor(hd);
        h = (float)hd;

        /* Ritmo no entero respecto del tono: si no, se veria un latido regular. */
        double pd = rate * COLOR_SAT_RATIO * t + (double)rng_f01_ch(index, seed, 4);
        pd -= floor(pd);
        float pulse = 0.5f + 0.5f * sinf(COLOR_TAU * (float)pd);

        s += (1.0f - s) * COLOR_SAT_PULSE * pulse;
    }

    float r, g, b;
    color_hsv_to_rgb(h, s, v, &r, &g, &b);
    return rgb_packf(r, g, b);
}

/* El color fijo es el caso t = 0 sin animacion: una sola definicion. */
uint32_t color_for_seed(uint32_t index, uint64_t seed)
{
    return color_for_seed_at(index, seed, NULL, 0.0);
}
