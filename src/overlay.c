/* ===========================================================================
 *  overlay.c - Fuente bitmap 5x7 embebida y dibujo de texto/FPS.
 *
 *  Cada glifo son 7 filas de 5 bits. En cada fila se usan los 5 bits bajos y
 *  el bit 0x10 es la columna de mas a la izquierda:
 *
 *        0x0E = 01110      .###.
 *        0x11 = 10001      #...#
 *        ...
 *
 *  La tabla se indexa por (ASCII - 32) con inicializadores designados, asi que
 *  los caracteres que no dibujamos quedan en cero (glifo vacio) sin tener que
 *  contar posiciones a mano.
 *
 *  Proyecto 1 - Computacion Paralela y Distribuida (UVG)
 * =========================================================================== */
#include "overlay.h"

#include <stdio.h>

/* Primer caracter representable y ancho/alto del glifo. */
#define FONT_FIRST   32
#define GLYPH_W       5
#define GLYPH_H       7

static const unsigned char FONT5x7[][GLYPH_H] = {
    [' ' - FONT_FIRST] = {0,0,0,0,0,0,0},
    ['%' - FONT_FIRST] = {0x18,0x19,0x02,0x04,0x08,0x13,0x03},
    ['-' - FONT_FIRST] = {0x00,0x00,0x00,0x1F,0x00,0x00,0x00},
    ['.' - FONT_FIRST] = {0x00,0x00,0x00,0x00,0x00,0x06,0x06},
    ['/' - FONT_FIRST] = {0x01,0x02,0x02,0x04,0x08,0x08,0x10},
    [':' - FONT_FIRST] = {0x00,0x06,0x06,0x00,0x06,0x06,0x00},
    ['=' - FONT_FIRST] = {0x00,0x00,0x1F,0x00,0x1F,0x00,0x00},

    ['0' - FONT_FIRST] = {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E},
    ['1' - FONT_FIRST] = {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
    ['2' - FONT_FIRST] = {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F},
    ['3' - FONT_FIRST] = {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E},
    ['4' - FONT_FIRST] = {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},
    ['5' - FONT_FIRST] = {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},
    ['6' - FONT_FIRST] = {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E},
    ['7' - FONT_FIRST] = {0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
    ['8' - FONT_FIRST] = {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},
    ['9' - FONT_FIRST] = {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C},

    ['A' - FONT_FIRST] = {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11},
    ['B' - FONT_FIRST] = {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},
    ['C' - FONT_FIRST] = {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},
    ['D' - FONT_FIRST] = {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E},
    ['E' - FONT_FIRST] = {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},
    ['F' - FONT_FIRST] = {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
    ['G' - FONT_FIRST] = {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F},
    ['H' - FONT_FIRST] = {0x11,0x11,0x11,0x1F,0x11,0x11,0x11},
    ['I' - FONT_FIRST] = {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E},
    ['J' - FONT_FIRST] = {0x07,0x02,0x02,0x02,0x02,0x12,0x0C},
    ['K' - FONT_FIRST] = {0x11,0x12,0x14,0x18,0x14,0x12,0x11},
    ['L' - FONT_FIRST] = {0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
    ['M' - FONT_FIRST] = {0x11,0x1B,0x15,0x15,0x11,0x11,0x11},
    ['N' - FONT_FIRST] = {0x11,0x11,0x19,0x15,0x13,0x11,0x11},
    ['O' - FONT_FIRST] = {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},
    ['P' - FONT_FIRST] = {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},
    ['Q' - FONT_FIRST] = {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D},
    ['R' - FONT_FIRST] = {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
    ['S' - FONT_FIRST] = {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E},
    ['T' - FONT_FIRST] = {0x1F,0x04,0x04,0x04,0x04,0x04,0x04},
    ['U' - FONT_FIRST] = {0x11,0x11,0x11,0x11,0x11,0x11,0x0E},
    ['V' - FONT_FIRST] = {0x11,0x11,0x11,0x11,0x11,0x0A,0x04},
    ['W' - FONT_FIRST] = {0x11,0x11,0x11,0x15,0x15,0x1B,0x11},
    ['X' - FONT_FIRST] = {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
    ['Y' - FONT_FIRST] = {0x11,0x11,0x0A,0x04,0x04,0x04,0x04},
    ['Z' - FONT_FIRST] = {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F},
};

#define GLYPH_COUNT ((int)(sizeof(FONT5x7) / sizeof(FONT5x7[0])))

/* Rellena un rectangulo, recortando contra el framebuffer. */
static void fill_rect(Framebuffer *fb, int x, int y, int w, int h, uint32_t color)
{
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w;  if (x1 > fb->w) x1 = fb->w;
    int y1 = y + h;  if (y1 > fb->h) y1 = fb->h;
    for (int yy = y0; yy < y1; ++yy)
        for (int xx = x0; xx < x1; ++xx)
            fb->px[yy * fb->w + xx] = color;
}

/* Devuelve el indice en la tabla para un caracter, o -1 si no hay glifo. Las
 * minusculas se mapean a su mayuscula para no duplicar la tabla. */
static int glyph_index(char c)
{
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    int idx = (unsigned char)c - FONT_FIRST;
    if (idx < 0 || idx >= GLYPH_COUNT) return -1;
    return idx;
}

void overlay_text(Framebuffer *fb, int x, int y, const char *s,
                  uint32_t color, int scale)
{
    if (fb == NULL || fb->px == NULL || s == NULL) return;
    if (scale < 1) scale = 1;

    int pen_x = x;
    for (const char *p = s; *p != '\0'; ++p) {
        if (*p == ' ') {                        /* espacio: solo avanza */
            pen_x += (GLYPH_W + 1) * scale;
            continue;
        }

        int gi = glyph_index(*p);
        if (gi >= 0) {
            const unsigned char *rows = FONT5x7[gi];
            for (int r = 0; r < GLYPH_H; ++r) {
                unsigned char bits = rows[r];
                for (int c = 0; c < GLYPH_W; ++c) {
                    if (bits & (0x10u >> c)) {  /* bit prendido: pintar bloque */
                        fill_rect(fb,
                                  pen_x + c * scale,
                                  y + r * scale,
                                  scale, scale, color);
                    }
                }
            }
        }
        pen_x += (GLYPH_W + 1) * scale;         /* 1 pixel de separacion */
    }
}

void overlay_stats(Framebuffer *fb, const Config *cfg, double fps, int n_visible)
{
    if (fb == NULL || fb->px == NULL || cfg == NULL) return;

    const int scale = (fb->h >= 700) ? 2 : 1;   /* mas grande en canvas altos */
    const int line_h = GLYPH_H * scale + 3;
    const int x = 8;
    int y = 8;

    const uint32_t WHITE = 0xFFFFFFFFu;
    const uint32_t RED   = 0xFFFF3B30u;         /* FPS por debajo del umbral    */
    const uint32_t DIM   = 0xFFB0B0C0u;         /* rotulos secundarios          */

    char buf[64];

    /* --- Linea 1: FPS. Rojo si esta por debajo del piso del enunciado. ---- */
    uint32_t fps_col = (fps < SS_FPS_TARGET) ? RED : WHITE;
    snprintf(buf, sizeof buf, "FPS %.1f", fps);
    overlay_text(fb, x, y, buf, fps_col, scale);
    y += line_h;

    /* --- Linea 2: N y numero de hilos (la prueba de que seq y omp corren el
     *     mismo N en la presentacion). --------------------------------------- */
    if (cfg->threads > 0)
        snprintf(buf, sizeof buf, "N=%d  HILOS=%d", cfg->n, cfg->threads);
    else
        snprintf(buf, sizeof buf, "N=%d  HILOS=MAX", cfg->n);
    overlay_text(fb, x, y, buf, DIM, scale);
    y += line_h;

    /* --- Linea 3: modo de dibujo y semillas visibles este frame. ---------- */
    snprintf(buf, sizeof buf, "%s  VIS=%d",
             cfg->voronoi ? "VORONOI" : "PUNTOS", n_visible);
    overlay_text(fb, x, y, buf, DIM, scale);
}
