/* ===========================================================================
 *  bench.h - Modo headless de medicion: corre K frames sin ventana y reporta
 *  estadisticas (media, mediana, min, max, desviacion estandar, FPS).
 *
 *  Es el instrumento que exige el Anexo 3 del enunciado: "un minimo de 10
 *  mediciones por prueba". El programa mide UNA corrida; las 10 repeticiones
 *  las hace un loop externo de shell invocando el binario 10 veces (ver
 *  docs/02-PARAMETRO-N.md seccion 5).
 *
 *  Proyecto 1 - Computacion Paralela y Distribuida (UVG)
 * =========================================================================== */
#ifndef BENCH_H
#define BENCH_H

#include "config.h"
#include "render.h"
#include "sphere.h"

/* Resultado de una corrida de bench. Todos los tiempos en milisegundos. */
typedef struct {
    int    frames;   /* frames utiles medidos (ya sin el calentamiento) */
    double media;
    double mediana;
    double min;
    double max;
    double sd;       /* desviacion estandar muestral */
    double fps;       /* 1000 / media */
} BenchStats;

/* ---------------------------------------------------------------------------
 *  Corre cfg->bench_frames frames (o 200 si es 0) sobre 'fb' y 's' ya
 *  reservados, descarta los primeros SS_DEF_BENCH_WARMUP como calentamiento
 *  de cache, y devuelve las estadisticas de los que quedan.
 *
 *  Mide el frame COMPLETO: render_frame() y, si cfg->physics esta activo,
 *  tambien physics_step() -- es lo que el usuario percibe como costo real
 *  de un frame en la ventana.
 *
 *  Devuelve una BenchStats en cero (frames=0) si fb/s/cfg son NULL o si
 *  fallo la reserva de memoria interna.
 * ------------------------------------------------------------------------- */
BenchStats bench_run(Framebuffer *fb, SeedSet *s, const Config *cfg);

/* Imprime en formato humano, una linea con etiquetas (igual al de
 * docs/02-PARAMETRO-N.md seccion 5). */
void bench_print_human(const BenchStats *st, const Config *cfg);

/* Imprime una linea CSV sin encabezado:
 *   n,width,height,frames,media,mediana,min,max,sd,fps
 * Pensada para '>>' a un archivo y graficar despues. */
void bench_print_csv(const BenchStats *st, const Config *cfg);

#endif /* BENCH_H */
