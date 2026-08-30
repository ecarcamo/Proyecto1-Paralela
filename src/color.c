/* ===========================================================================
 *  color.c - HSV a RGB y asignacion de color por semilla.
 *  Proyecto 1 - Computacion Paralela y Distribuida (UVG)
 * =========================================================================== */
#include "color.h"
#include "rng.h"

#include <math.h>
#include <stddef.h>

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

/* --------------------------------------------------------------------------
 *  Constantes del pulso de saturacion. No son argumentos de CLI: son el ajuste
 *  fino del look, igual que las constantes de iluminacion de render.c.
 *
 *  El pulso solo SUMA saturacion (va de 0 a 1 y multiplica lo que le FALTA a
 *  la semilla para llegar a 1). Un pulso centrado, que restara la mitad del
 *  tiempo, mandaria las semillas hacia el gris en cada valle y la esfera se
 *  veria lavada justo en el momento contrario al que se busca: aca el valle es
 *  la saturacion base de siempre y la cresta es el color casi puro.
 * -------------------------------------------------------------------------- */
#define COLOR_SAT_PULSE  0.55f   /* cuanto sube en la cresta, 0..1            */
#define COLOR_SAT_RATIO  0.37f   /* ritmo del pulso respecto del tono         */

#define COLOR_TAU  6.28318530717958647692f

uint32_t color_for_seed_at(uint32_t index, uint64_t seed,
                           const ColorAnim *anim, double t)
{
    /* Canales independientes del mismo indice. Si se usara el mismo hash para
     * todos, tono/saturacion/brillo/ritmo quedarian correlacionados y se
     * verian bandas. */
    float h = rng_f01_ch(index, seed, 0);              /* tono: todo el circulo */
    float s = 0.55f + 0.40f * rng_f01_ch(index, seed, 1);
    float v = 0.72f + 0.28f * rng_f01_ch(index, seed, 2);

    if (anim != NULL && anim->speed != 0.0f) {
        float spread = anim->spread;
        if (spread < 0.0f) spread = 0.0f;
        if (spread > 1.0f) spread = 1.0f;

        /* Ritmo propio de esta semilla, centrado en anim->speed. El jitter en
         * [-1,1) es lo que las desfasa: sin el, las N semillas recorren el
         * circulo de tono en formacion cerrada y lo unico que se ve es la
         * paleta entera girando, no la esfera saturandose. */
        float jitter = 2.0f * rng_f01_ch(index, seed, 3) - 1.0f;
        double rate  = (double)anim->speed * (1.0 + (double)spread * jitter);

        /* Envolvimiento en double (ver el comentario de la firma en color.h)
         * y recien ahi se baja a float. */
        double hd = (double)h + rate * t;
        hd -= floor(hd);
        h = (float)hd;

        /* El pulso de saturacion NO va al mismo ritmo que el tono ni a un
         * multiplo entero de el: si lo fuera, cada vuelta de tono caeria
         * siempre en la misma fase del pulso y se veria un latido regular.
         * Con 0.37 los dos ciclos solo vuelven a coincidir cada 100 vueltas
         * de tono, o sea nunca dentro de una sesion. */
        double pd = rate * COLOR_SAT_RATIO * t + (double)rng_f01_ch(index, seed, 4);
        pd -= floor(pd);
        float pulse = 0.5f + 0.5f * sinf(COLOR_TAU * (float)pd);

        s += (1.0f - s) * COLOR_SAT_PULSE * pulse;
    }

    float r, g, b;
    color_hsv_to_rgb(h, s, v, &r, &g, &b);
    return rgb_packf(r, g, b);
}

/* El color fijo es el caso t = 0 sin animacion: una sola definicion de los
 * rangos de saturacion y brillo, imposible que las dos versiones se
 * desincronicen. */
uint32_t color_for_seed(uint32_t index, uint64_t seed)
{
    return color_for_seed_at(index, seed, NULL, 0.0);
}
