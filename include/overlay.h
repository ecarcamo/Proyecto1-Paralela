/* overlay.h - Texto y FPS sobre el framebuffer, con una fuente 5x7 embebida. */
#ifndef OVERLAY_H
#define OVERLAY_H

#include "config.h"
#include "render.h"

/* Escribe 's' en (x, y) con la fuente agrandada 'scale' veces. */
void overlay_text(Framebuffer *fb, int x, int y, const char *s,
                  uint32_t color, int scale);

/* FPS y N arriba a la izquierda; los FPS en ROJO por debajo de SS_FPS_TARGET. */
void overlay_stats(Framebuffer *fb, const Config *cfg, double fps, int n_visible);

/* 4ta linea: el angulo medio, que con --physics 1 converge solo a 137.5. */
void overlay_physics(Framebuffer *fb, const Config *cfg, double divergence_deg);

#endif /* OVERLAY_H */
