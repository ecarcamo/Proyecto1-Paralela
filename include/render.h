/* ===========================================================================
 *  render.h - Framebuffer, camara y el punto de entrada del dibujado.
 *
 *  Este modulo NO conoce SDL. Trabaja sobre un buffer de pixeles ARGB8888 en
 *  memoria y nada mas; main.c se encarga de subir ese buffer a una textura.
 *  Asi el nucleo del render es testeable sin ventana y portable sin cambios.
 *
 *  IMPORTANTE PARA DIEGUITO: la firma de render_frame() es un contrato. En
 *  este turno su cuerpo es un dibujo de PUNTOS (rapido, suficiente para ver la
 *  esfera). Vos vas a reemplazar SOLO el cuerpo por el raycasting con Voronoi;
 *  como la firma no cambia, main.c no se toca.
 *
 *  Proyecto 1 - Computacion Paralela y Distribuida (UVG)
 * =========================================================================== */
#ifndef RENDER_H
#define RENDER_H

#include <stdint.h>

#include "config.h"
#include "sphere.h"
#include "vec3.h"

/* ---------------------------------------------------------------------------
 *  Framebuffer: ancho, alto y un arreglo lineal de w*h pixeles ARGB8888.
 *  El pixel (x, y) vive en px[y * w + x]. El formato coincide con
 *  SDL_PIXELFORMAT_ARGB8888, asi que subirlo a la textura es un memcpy.
 * ------------------------------------------------------------------------- */
typedef struct {
    int       w, h;
    uint32_t *px;
} Framebuffer;

/* Reserva w*h pixeles. Devuelve 0 si todo bien; negativo si las dimensiones
 * son invalidas o si fallo malloc (en cuyo caso deja fb en cero, sin fugas). */
int  fb_alloc(Framebuffer *fb, int w, int h);

/* Libera el buffer y deja fb en cero. Seguro sobre un fb ya limpio o nulo. */
void fb_free(Framebuffer *fb);

/* Rellena todo el framebuffer con un color solido. */
void fb_clear(Framebuffer *fb, uint32_t argb);

/* ---------------------------------------------------------------------------
 *  Camara pinhole. Se construye desde la Config (resolucion y fraccion que la
 *  esfera debe ocupar) y de ahi salen dos servicios:
 *
 *    - la proyeccion perspectiva que usa el dibujo de puntos de este turno;
 *    - camera_ray(), que da la direccion del rayo por pixel para el
 *      raycasting con Voronoi que viene despues.
 *
 *  La base {right, up, forward} es ortonormal. tan_half_fov es la tangente de
 *  la mitad del campo de vision VERTICAL; el horizontal sale multiplicando por
 *  la relacion de aspecto.
 * ------------------------------------------------------------------------- */
typedef struct {
    Vec3  origin, right, up, forward;
    float tan_half_fov;
} Camera;

/* Arma la camara mirando al origen, a la distancia justa para que la esfera
 * unitaria ocupe cfg->sphere_frac de la altura del canvas. */
Camera camera_make(const Config *cfg);

/* Direccion (normalizada) del rayo que sale del ojo y pasa por el centro del
 * pixel (i, j) de un canvas w x h. La usara el raycasting de Dieguito. */
Vec3   camera_ray(const Camera *cam, int i, int j, int w, int h);

/* ---------------------------------------------------------------------------
 *  Dibuja un frame completo. 't' es el tiempo en segundos: de ahi sale el
 *  angulo de giro (t * cfg->rot_speed).
 *
 *  Cuerpo de este turno: proyecta cada semilla, la sombrea segun su normal y
 *  pinta un disco de su color con z-buffer (las de atras no tapan a las de
 *  adelante). SIN Voronoi todavia.
 *
 *  LA FIRMA NO CAMBIA: el raycasting con Voronoi entra reemplazando el cuerpo.
 * ------------------------------------------------------------------------- */
void render_frame(Framebuffer *fb, const SeedSet *s, const Config *cfg, double t);

#endif /* RENDER_H */
