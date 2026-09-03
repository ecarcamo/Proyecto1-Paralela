/* render.h - Framebuffer, camara y entrada del dibujado; no conoce SDL. */
#ifndef RENDER_H
#define RENDER_H

#include <stdint.h>

#include "config.h"
#include "sphere.h"
#include "vec3.h"

/* w*h pixeles ARGB8888 en px[y*w + x]: subirlo a la textura es un memcpy. */
typedef struct {
    int       w, h;
    uint32_t *px;
} Framebuffer;

/* Reserva w*h pixeles: 0 si todo bien, negativo si fallo (fb queda en cero). */
int  fb_alloc(Framebuffer *fb, int w, int h);

/* Libera y deja fb en cero. Seguro sobre un fb ya limpio o nulo. */
void fb_free(Framebuffer *fb);

/* Rellena todo el framebuffer con un color solido. */
void fb_clear(Framebuffer *fb, uint32_t argb);

/* Camara pinhole; tan_half_fov es la mitad del FOV VERTICAL. */
typedef struct {
    Vec3  origin, right, up, forward;
    float tan_half_fov;
} Camera;

/* Camara al origen, a la distancia que da cfg->sphere_frac de altura. */
Camera camera_make(const Config *cfg);

/* Direccion normalizada del rayo por el centro del pixel (i, j). */
Vec3   camera_ray(const Camera *cam, int i, int j, int w, int h);

/* Dibuja un frame en el instante 't'; despacha segun --voronoi / --raster. */
void render_frame(Framebuffer *fb, const SeedSet *s, const Config *cfg, double t);

#endif /* RENDER_H */
