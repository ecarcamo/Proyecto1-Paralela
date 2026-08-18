/* ===========================================================================
 *  overlay.h - Texto sobre el framebuffer y el contador de FPS.
 *
 *  El enunciado EXIGE desplegar los FPS, asi que necesitamos escribir texto
 *  sin depender de ninguna libreria de fuentes (SDL_ttf traeria una dependencia
 *  pesada solo para unos digitos). La solucion es una fuente bitmap 5x7
 *  embebida como un array de bytes: cero dependencias, portable, y suficiente
 *  para numeros y rotulos en mayuscula.
 *
 *  Proyecto 1 - Computacion Paralela y Distribuida (UVG)
 * =========================================================================== */
#ifndef OVERLAY_H
#define OVERLAY_H

#include "config.h"
#include "render.h"

/* Escribe 's' arrancando en (x, y), en el color dado, cada pixel de la fuente
 * agrandado 'scale' veces. Las minusculas se dibujan como mayusculas; los
 * caracteres sin glifo se saltan como espacios. Recorta contra el framebuffer. */
void overlay_text(Framebuffer *fb, int x, int y, const char *s,
                  uint32_t color, int scale);

/* Dibuja el bloque de estadisticas arriba a la izquierda: FPS, N y numero de
 * hilos. Los FPS salen en ROJO cuando caen por debajo de SS_FPS_TARGET (30),
 * que es el umbral del enunciado: asi el trabon de la demo no hay que
 * explicarlo, se ve. 'n_visible' es cuantas semillas se dibujaron este frame.
 *
 * Mostrar N y hilos en pantalla es la prueba, en la presentacion, de que el
 * binario secuencial y el paralelo corren EXACTAMENTE el mismo N. */
void overlay_stats(Framebuffer *fb, const Config *cfg, double fps, int n_visible);

#endif /* OVERLAY_H */
