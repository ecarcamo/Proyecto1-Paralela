/* bench.h - Medicion headless: K frames sin ventana. Mide UNA corrida; las 10
 * repeticiones del Anexo 3 las hace un loop de shell (docs/02 seccion 5). */
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

/* Corre cfg->bench_frames (200 si es 0) descartando el calentamiento y mide el
 * frame COMPLETO: render_frame() y, con --physics, tambien physics_step(). */
BenchStats bench_run(Framebuffer *fb, SeedSet *s, const Config *cfg);

/* Una linea con etiquetas, como en docs/02-PARAMETRO-N.md seccion 5. */
void bench_print_human(const BenchStats *st, const Config *cfg);

/* Una linea CSV sin encabezado: n,width,height,frames,media,mediana,min,max,sd,fps */
void bench_print_csv(const BenchStats *st, const Config *cfg);

#endif /* BENCH_H */
