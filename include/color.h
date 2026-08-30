/* ===========================================================================
 *  color.h - Color pseudoaleatorio por semilla y empaquetado ARGB.
 *
 *  El formato es ARGB8888 (0xAARRGGBB) porque es el que espera
 *  SDL_PIXELFORMAT_ARGB8888, asi que el framebuffer se sube a la textura con
 *  un memcpy directo, sin conversion por pixel.
 *
 *  Proyecto 1 - Computacion Paralela y Distribuida (UVG)
 * =========================================================================== */
#ifndef COLOR_H
#define COLOR_H

#include <stdint.h>

/* Empaqueta tres canales de 8 bits en un pixel ARGB8888 opaco. */
static inline uint32_t rgb_pack(uint8_t r, uint8_t g, uint8_t b)
{
    return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

/* Igual, tomando flotantes en [0,1] y con saturacion (clamp) incluida.
 * El +0.5f es redondeo al entero mas cercano; sin el, todo el rango queda
 * sesgado hacia abajo y la imagen sale medio tono mas oscura. */
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

/* Desempaqueta a flotantes en [0,1]. La usa el sombreado para modular un
 * color base por la iluminacion. */
static inline void rgb_unpackf(uint32_t argb, float *r, float *g, float *b)
{
    *r = (float)((argb >> 16) & 0xFFu) * (1.0f / 255.0f);
    *g = (float)((argb >>  8) & 0xFFu) * (1.0f / 255.0f);
    *b = (float)( argb        & 0xFFu) * (1.0f / 255.0f);
}

/* Multiplica un color por un escalar (para iluminacion difusa). */
static inline uint32_t rgb_mul(uint32_t argb, float k)
{
    float r, g, b;
    rgb_unpackf(argb, &r, &g, &b);
    return rgb_packf(r * k, g * k, b * k);
}

/* Interpola entre dos colores. t=0 da 'a', t=1 da 'b'. Se usa para fundir el
 * borde de una celda de Voronoi hacia el color de fondo (antialiasing). */
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

/* ---------------------------------------------------------------------------
 *  El color de la semilla i. Pseudoaleatorio, determinista y PURO (sin estado):
 *  se puede llamar desde cualquier hilo sin sincronia, y la version secuencial
 *  y la paralela producen exactamente los mismos colores, lo que permite
 *  validar el paralelo comparando el framebuffer bit a bit.
 *
 *  Se trabaja en HSV y no en RGB directo porque tomar r, g, b al azar por
 *  separado produce muchos colores lodosos y grises. Fijando saturacion y
 *  brillo en rangos altos y dejando solo el TONO al azar, todos los colores
 *  salen vivos y bien diferenciados entre si.
 * ------------------------------------------------------------------------- */
uint32_t color_for_seed(uint32_t index, uint64_t seed);

/* ---------------------------------------------------------------------------
 *  ColorAnim - la deriva de color de las semillas que YA estan en la esfera.
 *
 *  El color no se vuelve a sortear ni se guarda de un frame al siguiente: se
 *  RECALCULA como una funcion del tiempo. Eso mantiene la propiedad que hace
 *  util a color_for_seed() -- pura, sin estado, misma salida en cualquier
 *  orden -- y por lo tanto la version paralela sigue produciendo el mismo
 *  framebuffer bit a bit que la secuencial en el mismo instante t.
 *
 *    speed   vueltas COMPLETAS del circulo de tono por segundo. 0 congela el
 *            color (comportamiento anterior). 0.06 = una vuelta cada ~17 s,
 *            que es lento como para leerse como una transicion y no como un
 *            parpadeo.
 *    spread  cuanto varia ese ritmo ENTRE semillas, en [0,1]. Con 0 todas
 *            recorren el circulo a la vez y la esfera entera cambia de tono
 *            en bloque, que se ve como un filtro encima. Con spread > 0 cada
 *            semilla lleva su propio ritmo, se desfasan solas y la esfera se
 *            va poblando de tonos distintos: es lo que la satura.
 * ------------------------------------------------------------------------- */
typedef struct {
    float speed;
    float spread;
} ColorAnim;

/* ---------------------------------------------------------------------------
 *  El color de la semilla i en el instante t (segundos). Igual de pura que
 *  color_for_seed(): depende solo de (index, seed, anim, t).
 *
 *  't' es double y no float a proposito. El tono acumulado es speed*t y crece
 *  sin tope mientras el programa corre; en float, a la media hora t ya vale
 *  ~1e3 y la parte fraccionaria -- que es justo la que da el tono -- se queda
 *  sin bits y el color avanza a saltos visibles. El envolvimiento a [0,1) se
 *  hace en double y recien despues se baja a float.
 *
 *  Con anim == NULL o anim->speed == 0 devuelve exactamente lo mismo que
 *  color_for_seed(index, seed).
 * ------------------------------------------------------------------------- */
uint32_t color_for_seed_at(uint32_t index, uint64_t seed,
                           const ColorAnim *anim, double t);

#endif /* COLOR_H */
