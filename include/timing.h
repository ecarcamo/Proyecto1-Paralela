/* ===========================================================================
 *  timing.h - Reloj monotonico portable.
 *
 *  Una sola funcion publica: el tiempo en segundos desde un origen arbitrario
 *  pero MONOTONICO. Monotonico importa porque medimos dt entre frames: el
 *  reloj de pared (gettimeofday / time()) puede saltar hacia atras si NTP
 *  ajusta la hora, y un dt negativo haria explotar el promedio de FPS.
 *
 *  El origen no tiene significado: solo sirven las DIFERENCIAS entre dos
 *  lecturas. Por eso el tipo es double en segundos y no una fecha.
 *
 *  Proyecto 1 - Computacion Paralela y Distribuida (UVG)
 * =========================================================================== */
#ifndef TIMING_H
#define TIMING_H

/* Segundos (con fraccion) desde un punto de partida fijo del sistema. Solo
 * las diferencias entre llamadas son significativas. Nunca retrocede. */
double now_seconds(void);

#endif /* TIMING_H */
